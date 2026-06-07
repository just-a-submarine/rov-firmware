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
static bool    s_startLast  = false;
static bool    s_estop      = false;     // 急停 latch：Start 邊緣觸發切換（再按一次解鎖）
static int     s_lastL = 0, s_lastR = 0, s_lastV = 0;   // 最近送出的馬達指令（供 Web 狀態列顯示轉速 %）

MotorPair computeDifferential(int lx, int ly) {
    // 1. 正規化至 [-1, 1]；Y 軸反向（搖桿前推 ly 為負 → 前進為正）
    float x =  (float)lx / 32767.0f;
    float y = -(float)ly / 32767.0f;

    // 2. 死區
    if (fabsf(x) < DEADZONE_RATIO) x = 0.0f;
    if (fabsf(y) < DEADZONE_RATIO) y = 0.0f;

    // 3. 搖桿大小（速度命令），最大限制 1.0
    float r = fminf(hypotf(x, y), 1.0f);

    // 4. 差速混合（教科書 arcade 慣例，與 ROV computeNavigation 的 left=fwd+turn 一致）：
    //    左推 x<0 → rawLeft 變小、rawRight 變大 → 右馬達 > 左馬達 → 左轉。✔ 標準接線
    //    （2026-05-29 曾因實機看似反向改成 y−x/y+x；2026-06-07 經確認左右馬達未接反、
    //      且與自動導航慣例不一致 → 改回教科書式。日後若馬達實體左右對調才需再對調。）
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

    // Start 邊緣觸發：急停 toggle（按一次鎖定停車，再按一次解鎖恢復）
    if (btnStart && !s_startLast) {
        s_estop = !s_estop;
        printf("[GS] emergencyStop %s\n", s_estop ? "LATCHED" : "released");
    }
    s_startLast = btnStart;

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
    pkt.emergencyStop = s_estop;
    pkt.autoMode      = gpAuto();          // 手機「啟動自動」開關（WS auto 欄）；ROV 才會跑 computeNavigation
    pkt.streamMode    = s_streamMode;
    pkt.takePhoto     = takePhoto;
    pkt.msgType       = MSG_CONTROL;

    s_lastL = mp.left; s_lastR = mp.right; s_lastV = depth;   // 供遙測回傳給 Web 顯示轉速 %
    sendControl(pkt);
}

bool controlEstopLatched() { return s_estop; }
int  controlLastMotorL()   { return s_lastL; }
int  controlLastMotorR()   { return s_lastR; }
int  controlLastMotorV()   { return s_lastV; }
