#include "gamepad.h"
#include "config.h"
#include <Arduino.h>

// =============================================================================
//  控制來源 = 手機瀏覽器 Gamepad API → WebSocket → 地面站（gamepadSetRemote）。
//
//  GS 完全不使用藍牙：手把改配對到「手機」，網頁讀軸值經 WS 上行。藍牙的停用發生在
//  進入點 main.c（app_main 不初始化 BTstack/Bluepad32 並釋放 BT RAM，BT 控制器永不
//  開機 → 與 Wi-Fi 零共存衝突）。本檔僅保留 WS 控制狀態與對外搖桿介面。
// =============================================================================

// ---- 遙控狀態（由 web_server 收 WS 控制封包後餵入）----
static int16_t  s_lx = 0, s_ly = 0, s_ry = 0;
static uint16_t s_btns  = 0;
static bool     s_auto  = false;     // 手機「啟動自動」開關（WS auto 欄）
static uint32_t s_epoch = 0;         // 手機 UTC 紀元秒（WS ts 欄）→ 轉發給 ROV 設時鐘
static uint8_t  s_photoSeq = 0;      // 手機拍照單調序號（WS ph 欄）→ 轉發給 ROV
static uint32_t s_lastMs = 0;

static inline bool ctrlFresh() {
    return s_lastMs != 0 && (millis() - s_lastMs) < GP_WS_TIMEOUT_MS;
}

void gamepadSetRemote(int16_t lx, int16_t ly, int16_t ry, uint16_t buttons) {
    s_lx = lx; s_ly = ly; s_ry = ry; s_btns = buttons;
    s_lastMs = millis();
}

void gamepadSetAuto(bool autoMode) { s_auto = autoMode; }
bool gpAuto() { return ctrlFresh() && s_auto; }   // 控制斷線即自動關閉（failsafe）

void     gamepadSetEpoch(uint32_t epochS) { s_epoch = epochS; }
uint32_t gpEpoch() { return s_epoch; }            // 手機 UTC 紀元秒（0=未提供）

void    gamepadSetPhotoSeq(uint8_t seq) { s_photoSeq = seq; }
uint8_t gpPhotoSeq() { return s_photoSeq; }       // 手機拍照單調序號（不受 ctrlFresh 影響，序號變即拍）

// 藍牙已停用，恆為 false（保留供診斷列印用）。
bool btControllerConnected() { return false; }

void setupGamepad() {
    // 藍牙已在 main.c app_main 停用（BT 控制器不開機）；此處無需初始化任何東西。
    printf("[Gamepad] 控制走手機 WebSocket；GS 藍牙已停用（BT 不開機）\n");
}

void pollGamepad() {
    // 藍牙停用，無 BTstack 可服務 → no-op（保留介面給 control.cpp 呼叫）。
}

bool gamepadConnected() { return ctrlFresh(); }

int16_t gpAxisLX() { return ctrlFresh() ? s_lx : 0; }
int16_t gpAxisLY() { return ctrlFresh() ? s_ly : 0; }
int16_t gpAxisRY() { return ctrlFresh() ? s_ry : 0; }

bool gpButton(GpButton b) { return ctrlFresh() && ((s_btns >> (int)b) & 0x1u); }
