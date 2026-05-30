#pragma once
#include <Arduino.h>
#include "shared_state.h"

// 感測器：QMC5883P 羅盤、MS5837 深度、INA260 功耗、Neo-M8N GPS（doc/06）

bool  setupSensors();              // I²C 裝置 + GPS UART 初始化（回報各裝置狀態）

void  gpsPoll();                   // 餵 UART 位元組給 TinyGPS，需頻繁呼叫
float getDepthM();                 // 當前深度（公尺）
float getCorrectedHeading();       // 校正後航向 0～360°（doc/06 §五）
bool  getMagRaw(float& x, float& y);  // 原始（未校正）磁場 X/Y（Gauss）；供手機端羅盤校準

// 讀取全部感測器並填入遙測快照（lat/lng/depth/current/power/heading/bat/gpsValid）
void  readAllSensors(TelemetrySnapshot& snap);
