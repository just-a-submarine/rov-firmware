#include "gamepad.h"
#include <Arduino.h>
#include <Bluepad32.h>

// Bluepad32 搖桿軸範圍 -512..511；放大到文件用的 -32767..32767
static inline int16_t scaleAxis(int v) {
    long s = (long)v * 64;
    if (s > 32767) s = 32767;
    if (s < -32767) s = -32767;
    return (int16_t)s;
}

static ControllerPtr s_ctl = nullptr;

static void onConnected(ControllerPtr c) {
    if (s_ctl == nullptr) {
        s_ctl = c;
        ControllerProperties p = c->getProperties();
        printf("[Gamepad] connected: %s VID=0x%04x PID=0x%04x\n",
               c->getModelName().c_str(), p.vendor_id, p.product_id);
    }
}

static void onDisconnected(ControllerPtr c) {
    if (s_ctl == c) {
        s_ctl = nullptr;
        printf("[Gamepad] disconnected\n");
    }
}

void setupGamepad() {
    BP32.setup(&onConnected, &onDisconnected);
    BP32.enableVirtualDevice(false);
    // 不呼叫 forgetBluetoothKeys()：保留綁定，配對過後自動重連
    printf("[Gamepad] Bluepad32 ready (手把進入配對模式，且關閉 PC 藍牙)\n");
}

void pollGamepad() {
    BP32.update();
}

bool gamepadConnected() {
    return s_ctl != nullptr && s_ctl->isConnected();
}

// 文件慣例：右為正、前推為負。Bluepad32 axisY 上(前)為負，正好相符。
int16_t gpAxisLX() { return s_ctl ? scaleAxis(s_ctl->axisX())  : 0; }
int16_t gpAxisLY() { return s_ctl ? scaleAxis(s_ctl->axisY())  : 0; }
int16_t gpAxisRY() { return s_ctl ? scaleAxis(s_ctl->axisRY()) : 0; }

bool gpButton(GpButton b) {
    if (!s_ctl) return false;
    switch (b) {
        case GP_A:     return s_ctl->a();
        case GP_B:     return s_ctl->b();
        case GP_X:     return s_ctl->x();
        case GP_Y:     return s_ctl->y();
        case GP_LB:    return s_ctl->l1();
        case GP_RB:    return s_ctl->r1();
        case GP_START: return s_ctl->miscStart();
        case GP_BACK:  return s_ctl->miscSelect();
    }
    return false;
}
