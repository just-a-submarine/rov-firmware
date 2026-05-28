#pragma once
#include <Arduino.h>
#include "packets.h"

// 自動導航（Potential Field + Geofence）與 PID 深度控制（doc/06 §五、§六）

struct NavResult {
    int     left  = 0;
    int     right = 0;
    uint8_t wpIdx = NAV_IDX_NONE;
    float   distM = 0;
    bool    active = false;       // 是否正在導航（仍有未到達航點）
};

void setupDepthPID();
void navResetProgress();          // 收到新航點批次時呼叫，從第 0 點重新導航

// 自動水平導航：依當前 GPS 與航向算出左右馬達差速
NavResult computeNavigation(double lat, double lng, float headingDeg, bool gpsValid);

// 垂直馬達：右搖桿輸入優先；放開時 PID 維持當時深度（doc/06 §六）
int computeVertMotor(float currentDepthM, int rightStickInput);
