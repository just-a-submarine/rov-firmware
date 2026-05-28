#include "control.h"
#include "gamepad.h"
#include "espnow_link.h"
#include "config.h"
#include "packets.h"
#include <Arduino.h>
#include <cmath>

// ---- 控制狀態（跨呼叫保留）----
static bool    s_ledState   = false;
static uint8_t s_streamMode = 0;     // 0 純串流 / 1 串流+錄影
static bool    s_rbLast     = false;
static bool    s_lbLast     = false;
static bool    s_yLast      = false;

MotorPair computeDifferential(int lx, int ly) {
    // 1. 正規化至 [-1, 1]；Y 軸反向（搖桿前推 ly 為負 → 前進為正）
    float x =  (float)lx / 32767.0f;
    float y = -(float)ly / 32767.0f;

    // 2. 死區
    if (fabsf(x) < DEADZONE_RATIO) x = 0.0f;
    if (fabsf(y) < DEADZONE_RATIO) y = 0.0f;

    // 3. 搖桿大小（速度命令），最大限制 1.0
    float r = fminf(hypotf(x, y), 1.0f);

    // 4. 差速混合
    float rawLeft  = y + x;
    float rawRight = y - x;

    // 5. 歸一化：讓輸出峰值等於搖桿大小 r（保持速度比例正確）
    float peak = fmaxf(fabsf(rawLeft), fabsf(rawRight));
    float leftPow, rightPow;
    if (peak > 1e-6f) {
        leftPow  = rawLeft  * (r / peak);
        rightPow = rawRight * (r / peak);
    } else {
        leftPow = rightPow = 0.0f;
    }

    // 6. 轉換至 PWM 整數範圍
    return {
        constrain((int)(leftPow  * 1023.0f), -1023, 1023),
        constrain((int)(rightPow * 1023.0f), -1023, 1023)
    };
}

void readXboxAndSend() {
    pollGamepad();

    int lx = gpAxisLX();
    int ly = gpAxisLY();
    int ry = gpAxisRY();

    bool btnY     = gpButton(GP_Y);
    bool btnRB    = gpButton(GP_RB);
    bool btnLB    = gpButton(GP_LB);
    bool btnStart = gpButton(GP_START);

    // RB 邊緣觸發：串流模式 0 / 1 切換
    if (btnRB && !s_rbLast) s_streamMode = (s_streamMode == 0) ? 1 : 0;
    s_rbLast = btnRB;

    // LB 邊緣觸發：僅模式 0 送出單張拍照
    bool takePhoto = false;
    if (btnLB && !s_lbLast && s_streamMode == 0) takePhoto = true;
    s_lbLast = btnLB;

    // Y 邊緣觸發：LED toggle（最高優先）
    if (btnY && !s_yLast) s_ledState = !s_ledState;
    s_yLast = btnY;

    // 右搖桿 Y → 深度（前推為負 → 映射為正，永遠有效）
    if (abs(ry) < 2000) ry = 0;
    int depth = map(ry, -32767, 32767, 1023, -1023);

    // 左搖桿 → 差速混控
    MotorPair mp = computeDifferential(lx, ly);

    ControlPacket pkt = {};
    pkt.leftMotor     = mp.left;
    pkt.rightMotor    = mp.right;
    pkt.vertMotor     = depth;
    pkt.ledOn         = s_ledState;
    pkt.emergencyStop = btnStart;
    pkt.autoMode      = false;
    pkt.streamMode    = s_streamMode;
    pkt.takePhoto     = takePhoto;
    pkt.msgType       = MSG_CONTROL;
    sendControl(pkt);
}
