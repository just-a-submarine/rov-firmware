#pragma once
#include <stdint.h>

// =============================================================================
//  手把控制模組 —— 控制來源：手機瀏覽器 Gamepad API → WebSocket → 地面站
//
//  ⚠ 為何不用 GS 藍牙：Xbox 手把走 classic BT，連線中會餓死 Wi-Fi AP
//    （實測 gamepad 連線 → AP stations=0、潛水艇 -127、影像連不上）；即使閒置，雙模
//    BT 控制器仍與 Wi-Fi 分時共用 2.4GHz 無線電。故手把改配對到「手機」，網頁讀軸值經
//    WS 上行；GS 端【完全不初始化藍牙】（BT 控制器不開機，並釋放其 RAM）。
//
//  對外仍暴露相同的搖桿軸值/按鍵介面（給 control.cpp），只是資料源換成 WS。
//  軸值慣例：前推為負、右為正，範圍 -32767 ～ +32767。
// =============================================================================
#include <stdint.h>

enum GpButton {
    GP_A, GP_B, GP_X, GP_Y,
    GP_LB, GP_RB,
    GP_START, GP_BACK,
};

void setupGamepad();         // 停用藍牙（BT 控制器不開機）+ 釋放 BT RAM
void pollGamepad();          // 藍牙已停用 → no-op（保留介面給 control.cpp 呼叫）
bool gamepadConnected();     // WS 控制是否新鮮（GP_WS_TIMEOUT_MS 內）
bool btControllerConnected();// 藍牙已停用，恆為 false（保留供診斷列印）

// 由 web_server 在收到手機 WS 控制封包時呼叫，更新遙控狀態。
// buttons 位元：bit0=A,1=B,2=X,3=Y,4=LB,5=RB,6=Start,7=Back（對應 GpButton 順序）。
void gamepadSetRemote(int16_t lx, int16_t ly, int16_t ry, uint16_t buttons);

int16_t gpAxisLX();          // 左搖桿 X（右 = +）
int16_t gpAxisLY();          // 左搖桿 Y（前推 = -）
int16_t gpAxisRY();          // 右搖桿 Y（前推 = -）

bool gpButton(GpButton b);
