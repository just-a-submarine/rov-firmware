#include "gamepad.h"
#include "config.h"
#include <Arduino.h>
#include <Bluepad32.h>
#include <esp_coexist.h>

// =============================================================================
//  控制來源 = 手機瀏覽器 Gamepad API → WebSocket → 地面站（gamepadSetRemote）。
//
//  Xbox 手把走 classic BT，連線中會餓死 Wi-Fi AP（實測：手把連線 → AP stations=0、
//  潛水艇 -127、影像連不上）。故 GS 不再接受手把藍牙連線（保持閒置，閒置 BT 不影響
//  Wi-Fi），手把改配對到「手機」。本檔保留 Bluepad32 初始化以維持既有 BTstack 入口
//  （setup()/loop() 由其 run loop 驅動），但停用配對、清除舊綁定。
// =============================================================================

// ---- 遙控狀態（由 web_server 收 WS 控制封包後餵入）----
static int16_t  s_lx = 0, s_ly = 0, s_ry = 0;
static uint16_t s_btns  = 0;
static uint32_t s_lastMs = 0;

static inline bool ctrlFresh() {
    return s_lastMs != 0 && (millis() - s_lastMs) < GP_WS_TIMEOUT_MS;
}

void gamepadSetRemote(int16_t lx, int16_t ly, int16_t ry, uint16_t buttons) {
    s_lx = lx; s_ly = ly; s_ry = ry; s_btns = buttons;
    s_lastMs = millis();
}

// GS 不接受手把連線：任何連上的手把立即斷開（控制改走手機 WS）。
static volatile bool s_btConn = false;
static void onConnected(ControllerPtr c) {
    s_btConn = true;
    c->disconnect();                            // 立刻踢掉，避免 BT 連線餓死 Wi-Fi AP
    printf("[Gamepad] 拒絕手把藍牙連線並斷開（請改配對到手機）\n");
}
static void onDisconnected(ControllerPtr) { s_btConn = false; }

bool btControllerConnected() { return s_btConn; }

void setupGamepad() {
    BP32.setup(&onConnected, &onDisconnected);
    BP32.enableVirtualDevice(false);
    BP32.forgetBluetoothKeys();                 // 清除與 GS 的舊綁定 → 手把改配對到手機
    BP32.enableNewBluetoothConnections(false);  // 不接受手把連線（避免餓死 Wi-Fi AP）
    esp_coex_preference_set(ESP_COEX_PREFER_WIFI);
    printf("[Gamepad] 控制改由手機 WebSocket；GS 藍牙閒置不配對手把\n");
}

void pollGamepad() {
    BP32.update();   // 維持 BTstack 服務（無手把連線時等同 no-op）
}

bool gamepadConnected() { return ctrlFresh(); }

int16_t gpAxisLX() { return ctrlFresh() ? s_lx : 0; }
int16_t gpAxisLY() { return ctrlFresh() ? s_ly : 0; }
int16_t gpAxisRY() { return ctrlFresh() ? s_ry : 0; }

bool gpButton(GpButton b) { return ctrlFresh() && ((s_btns >> (int)b) & 0x1u); }
