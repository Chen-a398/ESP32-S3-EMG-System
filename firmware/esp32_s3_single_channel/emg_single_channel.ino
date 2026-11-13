#include <Arduino.h>
#include <math.h>

// ============================================================
// 单通道 ESP32-S3 肌电采集
//
// 模拟肌电输出 -> GPIO4 (ADC)
// 采样率：1000 Hz
// 数字处理：50 Hz 陷波 + 20~250 Hz 带通
// 特征：100 ms RMS 窗口，每 20 ms 更新
//
// 串口绘图输出：
// >rms:数值,base:静息基线,thr:握拳阈值,state:状态
// state=0：静息
// state=1：检测到肌肉活动
// ============================================================

constexpr int EMG_PIN = 3;

// 滤波器参数
constexpr float SAMPLE_RATE_HZ = 1000.0f;
constexpr float NOTCH_HZ       = 50.0f;
constexpr float NOTCH_Q        = 8.0f;
constexpr float HP_HZ          = 20.0f;
constexpr float LP_HZ          = 250.0f;

// 100 ms RMS 窗口，1000 Hz 下共 100 个采样
constexpr int RMS_WINDOW_SIZE = 100;

// 每 20 个样本输出一次，即每 20 ms 输出一次
constexpr int UPDATE_EVERY_SAMPLES = 20;

// 上电后前 2 秒用于测量静息 RMS 基线
constexpr uint32_t CALIBRATION_MS = 2000;

// 阈值倍率和偏移量可按实际数据调整
constexpr float GRIP_THRESHOLD_RATIO = 1.30f;
constexpr float GRIP_THRESHOLD_OFFSET = 5.0f;

// 释放阈值低于握拳阈值，形成回差，减少状态抖动
constexpr float RELEASE_THRESHOLD_RATIO = 1.15f;
constexpr float RELEASE_THRESHOLD_OFFSET = 3.0f;

// 连续满足若干次再切换状态，避免单个窗口误触发
constexpr int GRIP_CONFIRM_WINDOWS = 3;
constexpr int RELEASE_CONFIRM_WINDOWS = 5;


// ------------------------- 二阶滤波器 -------------------------

struct Biquad {
    float b0 = 1.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;

    float x1 = 0.0f;
    float x2 = 0.0f;
    float y1 = 0.0f;
    float y2 = 0.0f;

    float process(float x) {
        const float y = b0 * x + b1 * x1 + b2 * x2
                      - a1 * y1 - a2 * y2;

        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;

        return y;
    }
};

Biquad notchFilter;
Biquad highPassFilter;
Biquad lowPassFilter;

// 配置二阶陷波器
void setupNotch(Biquad &f, float frequency, float q, float sampleRate) {
    const float w0 = 2.0f * PI * frequency / sampleRate;
    const float c = cosf(w0);
    const float alpha = sinf(w0) / (2.0f * q);

    const float a0 = 1.0f + alpha;

    f.b0 = 1.0f / a0;
    f.b1 = (-2.0f * c) / a0;
    f.b2 = 1.0f / a0;
    f.a1 = (-2.0f * c) / a0;
    f.a2 = (1.0f - alpha) / a0;
}

// 配置二阶 Butterworth 高通滤波器
void setupHighPass(Biquad &f, float frequency, float sampleRate) {
    const float w0 = 2.0f * PI * frequency / sampleRate;
    const float c = cosf(w0);
    const float alpha = sinf(w0) / (2.0f * 0.70710678f);

    const float a0 = 1.0f + alpha;

    f.b0 = ((1.0f + c) * 0.5f) / a0;
    f.b1 = (-(1.0f + c)) / a0;
    f.b2 = ((1.0f + c) * 0.5f) / a0;
    f.a1 = (-2.0f * c) / a0;
    f.a2 = (1.0f - alpha) / a0;
}

// 配置二阶 Butterworth 低通滤波器
void setupLowPass(Biquad &f, float frequency, float sampleRate) {
    const float w0 = 2.0f * PI * frequency / sampleRate;
    const float c = cosf(w0);
    const float alpha = sinf(w0) / (2.0f * 0.70710678f);

    const float a0 = 1.0f + alpha;

    f.b0 = ((1.0f - c) * 0.5f) / a0;
    f.b1 = (1.0f - c) / a0;
    f.b2 = ((1.0f - c) * 0.5f) / a0;
    f.a1 = (-2.0f * c) / a0;
    f.a2 = (1.0f - alpha) / a0;
}


// ------------------------- RMS 与状态判断 -------------------------

float squaredSamples[RMS_WINDOW_SIZE] = {0};
int sampleIndex = 0;
int samplesSinceUpdate = 0;
double sumSquares = 0.0;

float baselineRms = 0.0f;
float thresholdOn = 0.0f;
float thresholdOff = 0.0f;

bool calibrated = false;
bool gripping = false;

float calibrationSum = 0.0f;
int calibrationCount = 0;

int aboveThresholdCount = 0;
int belowThresholdCount = 0;

uint32_t nextSampleUs = 0;
uint32_t startMs = 0;

void updateThresholds() {
    thresholdOn = baselineRms * GRIP_THRESHOLD_RATIO
                + GRIP_THRESHOLD_OFFSET;

    thresholdOff = baselineRms * RELEASE_THRESHOLD_RATIO
                 + RELEASE_THRESHOLD_OFFSET;
}

void printPlotLine(float rms) {
    Serial.print(">rms:");
    Serial.print(rms, 1);

    Serial.print(",base:");
    Serial.print(baselineRms, 1);

    Serial.print(",thr:");
    Serial.print(thresholdOn, 1);

    Serial.print(",state:");
    Serial.println(gripping ? 1 : 0);
}

void processRmsWindow(float rms) {
    // 上电后的前 2 秒默认用户保持静息，用来建立基线
    if (!calibrated) {
        calibrationSum += rms;
        calibrationCount++;

        baselineRms = calibrationSum / calibrationCount;
        updateThresholds();

        if (millis() - startMs >= CALIBRATION_MS) {
            calibrated = true;
            aboveThresholdCount = 0;
            belowThresholdCount = 0;

            Serial.println("静息基线校准完成；现在可以测试握拳");
        }

        printPlotLine(rms);
        return;
    }

    // 握拳：RMS 连续超过开启阈值才进入握拳状态
    if (!gripping) {
        if (rms > thresholdOn) {
            aboveThresholdCount++;
        } else {
            aboveThresholdCount = 0;
        }

        if (aboveThresholdCount >= GRIP_CONFIRM_WINDOWS) {
            gripping = true;
            aboveThresholdCount = 0;
            belowThresholdCount = 0;
            Serial.println("握拳");
        }
    }
    // 静息：RMS 连续低于释放阈值才退出握拳状态
    else {
        if (rms < thresholdOff) {
            belowThresholdCount++;
        } else {
            belowThresholdCount = 0;
        }

        if (belowThresholdCount >= RELEASE_CONFIRM_WINDOWS) {
            gripping = false;
            belowThresholdCount = 0;
            aboveThresholdCount = 0;
            Serial.println("静息");
        }
    }

    printPlotLine(rms);
}


void setup() {
    Serial.begin(115200);
    delay(500);

    analogReadResolution(12);
    analogSetPinAttenuation(EMG_PIN, ADC_11db);

    setupNotch(notchFilter, NOTCH_HZ, NOTCH_Q, SAMPLE_RATE_HZ);
    setupHighPass(highPassFilter, HP_HZ, SAMPLE_RATE_HZ);
    setupLowPass(lowPassFilter, LP_HZ, SAMPLE_RATE_HZ);

    startMs = millis();
    nextSampleUs = micros();

    Serial.println("单通道肌电采集启动");
    Serial.println("请先放松并保持 2 秒，完成静息基线校准");
}


void loop() {
    // 固定 1000 Hz 采样节拍
    const uint32_t nowUs = micros();

    if ((int32_t)(nowUs - nextSampleUs) < 0) {
        return;
    }

    nextSampleUs += 1000000UL / (uint32_t)SAMPLE_RATE_HZ;

    // ADC 原始值，ESP32-S3 12 位范围约为 0~4095
    const int raw = analogRead(EMG_PIN);

    // 级联处理：50 Hz 陷波 -> 20 Hz 高通 -> 250 Hz 低通
    float filtered = (float)raw;
    filtered = notchFilter.process(filtered);
    filtered = highPassFilter.process(filtered);
    filtered = lowPassFilter.process(filtered);

    // 滑动 RMS：移除窗口最旧样本的平方，加入新样本的平方
    const float oldValue = squaredSamples[sampleIndex];
    const float newValue = filtered * filtered;

    squaredSamples[sampleIndex] = newValue;
    sumSquares += (double)newValue - (double)oldValue;

    sampleIndex++;
    if (sampleIndex >= RMS_WINDOW_SIZE) {
        sampleIndex = 0;
    }

    samplesSinceUpdate++;

    // 每 20 ms 计算并输出一次 100 ms 窗口 RMS
    if (samplesSinceUpdate >= UPDATE_EVERY_SAMPLES) {
        samplesSinceUpdate = 0;

        const float rms = sqrtf((float)(sumSquares / RMS_WINDOW_SIZE));
        processRmsWindow(rms);
    }
}