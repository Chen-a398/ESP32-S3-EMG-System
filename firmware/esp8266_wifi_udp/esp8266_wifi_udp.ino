#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

constexpr uint32_t UART_BAUD = 921600;
constexpr uint16_t UDP_PORT = 9000;
constexpr uint32_t WIFI_RETRY_MS = 10000;
const char *WIFI_SSID = "CHANGE_ME_SSID";
const char *WIFI_PASSWORD = "CHANGE_ME_PASSWORD";
IPAddress UDP_HOST(192,168,1,100);

WiFiUDP udp;
char lineBuffer[256];
size_t lineLength=0;
uint32_t droppedLines=0;
uint32_t lastReconnectAttempt=0;

void setup(){
  Serial.begin(UART_BAUD);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID,WIFI_PASSWORD);
  udp.begin(UDP_PORT);
}

void loop(){
  while(Serial.available()){
    char ch=(char)Serial.read();
    if(ch=='\r') continue;
    if(ch=='\n'){
      if(lineLength && lineBuffer[0]!='#' && WiFi.status()==WL_CONNECTED){
        if(udp.beginPacket(UDP_HOST,UDP_PORT)==1){
          size_t written=udp.write((const uint8_t*)lineBuffer,lineLength);
          if(written!=lineLength || udp.endPacket()!=1) droppedLines++;
        } else droppedLines++;
      } else if(lineLength && lineBuffer[0]!='#') droppedLines++;
      lineLength=0;
    } else if(lineLength+1<sizeof(lineBuffer)) lineBuffer[lineLength++]=ch;
    else { lineLength=0; droppedLines++; }
  }
  if(WiFi.status()!=WL_CONNECTED && millis()-lastReconnectAttempt>=WIFI_RETRY_MS){
    lastReconnectAttempt=millis();
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID,WIFI_PASSWORD);
  }
}
