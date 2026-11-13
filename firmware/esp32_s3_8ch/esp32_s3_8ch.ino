#include <Arduino.h>
#include <SPI.h>
#include <math.h>

constexpr uint8_t ADC_CS_PIN = 6;
constexpr uint8_t ADC_DRDY_PIN = 7;
constexpr uint8_t ADC_SYNC_RESET_PIN = 3;
constexpr uint8_t ADC_MOSI_PIN = 4;
constexpr uint8_t ADC_MISO_PIN = 5;
constexpr uint8_t ADC_SCLK_PIN = 10;
constexpr uint32_t ADC_SPI_HZ = 1000000;
constexpr uint32_t UART_BAUD = 921600;
constexpr uint8_t CHANNELS = 8;
constexpr uint16_t WINDOW_SAMPLES = 100;
constexpr uint16_t UPDATE_SAMPLES = 20;

struct Biquad {
  float b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
  float process(float x) {
    float y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2;
    x2=x1; x1=x; y2=y1; y1=y; return y;
  }
};

Biquad notch[CHANNELS], highPass[CHANNELS], lowPass[CHANNELS];
float squares[CHANNELS][WINDOW_SAMPLES] = {};
double sums[CHANNELS] = {};
float calibrationSum[CHANNELS] = {};
float baseline[CHANNELS] = {};
bool active[CHANNELS] = {};
uint8_t onCount[CHANNELS] = {}, offCount[CHANNELS] = {};
uint16_t ringIndex=0, updateCount=0, calibrationWindows=0;
uint32_t startMs=0, sequenceNumber=0;
bool calibrated=false;

void designNotch(Biquad &f,float hz,float q) {
  constexpr float fs=1000.0f;
  float w=2*PI*hz/fs,c=cosf(w),alpha=sinf(w)/(2*q),a0=1+alpha;
  f.b0=1/a0; f.b1=-2*c/a0; f.b2=1/a0; f.a1=-2*c/a0; f.a2=(1-alpha)/a0;
}
void designHighPass(Biquad &f,float hz) {
  constexpr float fs=1000.0f;
  float w=2*PI*hz/fs,c=cosf(w),alpha=sinf(w)/(2*0.70710678f),a0=1+alpha;
  f.b0=((1+c)*0.5f)/a0; f.b1=-(1+c)/a0; f.b2=f.b0; f.a1=-2*c/a0; f.a2=(1-alpha)/a0;
}
void designLowPass(Biquad &f,float hz) {
  constexpr float fs=1000.0f;
  float w=2*PI*hz/fs,c=cosf(w),alpha=sinf(w)/(2*0.70710678f),a0=1+alpha;
  f.b0=((1-c)*0.5f)/a0; f.b1=(1-c)/a0; f.b2=f.b0; f.a1=-2*c/a0; f.a2=(1-alpha)/a0;
}

void adcReset() {
  pinMode(ADC_SYNC_RESET_PIN,OUTPUT);
  digitalWrite(ADC_SYNC_RESET_PIN,HIGH); delay(2);
  digitalWrite(ADC_SYNC_RESET_PIN,LOW); delay(2);
  digitalWrite(ADC_SYNC_RESET_PIN,HIGH); delay(10);
}

bool readAdcFrame(int32_t values[CHANNELS]) {
  uint8_t tx[30]={};
  uint8_t rx[30]={};
  if (digitalRead(ADC_DRDY_PIN)!=LOW) return false;
  SPI.beginTransaction(SPISettings(ADC_SPI_HZ,MSBFIRST,SPI_MODE1));
  digitalWrite(ADC_CS_PIN,LOW);
  for (uint8_t i=0;i<sizeof(tx);i++) rx[i]=SPI.transfer(tx[i]);
  digitalWrite(ADC_CS_PIN,HIGH);
  SPI.endTransaction();
  for(uint8_t ch=0;ch<CHANNELS;ch++) {
    uint8_t p=3+ch*3;
    int32_t v=((int32_t)rx[p]<<16)|((int32_t)rx[p+1]<<8)|rx[p+2];
    if(v&0x800000) v|=0xFF000000;
    values[ch]=v;
  }
  return true;
}

void emitFrame(const float rms[CHANNELS]) {
  Serial.print(millis());
  for(uint8_t ch=0;ch<CHANNELS;ch++){ Serial.print(','); Serial.print(rms[ch],1); }
  Serial.print(',');
  for(uint8_t ch=0;ch<CHANNELS;ch++){ Serial.print(active[ch]?1:0); if(ch+1<CHANNELS) Serial.print(':'); }
  Serial.println();
}

void setup() {
  Serial.begin(UART_BAUD);
  pinMode(ADC_CS_PIN,OUTPUT); digitalWrite(ADC_CS_PIN,HIGH);
  pinMode(ADC_DRDY_PIN,INPUT_PULLUP);
  SPI.begin(ADC_SCLK_PIN,ADC_MISO_PIN,ADC_MOSI_PIN,ADC_CS_PIN);
  adcReset();
  for(uint8_t ch=0;ch<CHANNELS;ch++){
    designNotch(notch[ch],50,8); designHighPass(highPass[ch],20); designLowPass(lowPass[ch],250);
  }
  startMs=millis();
  Serial.println("#EMG8CSV,t_ms,rms0,rms1,rms2,rms3,rms4,rms5,rms6,rms7,state0:state1:state2:state3:state4:state5:state6:state7");
}

void loop() {
  int32_t raw[CHANNELS];
  if(!readAdcFrame(raw)) return;
  float old[CHANNELS], filtered[CHANNELS];
  for(uint8_t ch=0;ch<CHANNELS;ch++){
    float x=(float)raw[ch];
    x=notch[ch].process(x); x=highPass[ch].process(x); filtered[ch]=lowPass[ch].process(x);
    old[ch]=squares[ch][ringIndex];
    float sq=filtered[ch]*filtered[ch]; squares[ch][ringIndex]=sq; sums[ch]+=sq-old[ch];
  }
  ringIndex=(ringIndex+1)%WINDOW_SAMPLES;
  if(++updateCount<UPDATE_SAMPLES) return;
  updateCount=0;
  float rms[CHANNELS];
  for(uint8_t ch=0;ch<CHANNELS;ch++) rms[ch]=sqrtf((float)(sums[ch]/WINDOW_SAMPLES));
  if(!calibrated){
    for(uint8_t ch=0;ch<CHANNELS;ch++) calibrationSum[ch]+=rms[ch];
    if(++calibrationWindows>=100){
      for(uint8_t ch=0;ch<CHANNELS;ch++) baseline[ch]=calibrationSum[ch]/calibrationWindows;
      calibrated=true;
    }
  } else {
    for(uint8_t ch=0;ch<CHANNELS;ch++){
      float on=baseline[ch]*1.30f+5.0f, off=baseline[ch]*1.15f+3.0f;
      if(!active[ch]){onCount[ch]=rms[ch]>on?onCount[ch]+1:0;if(onCount[ch]>=3){active[ch]=true;onCount[ch]=0;}}
      else {offCount[ch]=rms[ch]<off?offCount[ch]+1:0;if(offCount[ch]>=5){active[ch]=false;offCount[ch]=0;}}
    }
  }
  emitFrame(rms);
}
