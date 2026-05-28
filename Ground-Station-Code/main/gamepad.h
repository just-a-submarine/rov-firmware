#pragma once
#include <stdint.h>

// =============================================================================
//  Xbox 手把（藍牙 BLE / Bluepad32）讀取模組
//
//  目標：原廠 Xbox Series 手把走藍牙，經 Bluepad32（BTstack）連線。
//  與 Wi-Fi/ESP-NOW 同晶片共存（ESP-IDF Wi-Fi/BLE coexistence）。
//
//  ⚠ 配對注意：首次配對時，附近其他曾配對過此手把的裝置（尤其開發用 PC）
//    的藍牙必須關閉，否則手把會被搶先連回去。配對成功後會綁定，之後自動重連。
//
//  對外只暴露搖桿軸值與按鍵狀態，內部藍牙細節完全封裝。
//  軸值已轉為文件慣例：前推為負、右為正，範圍 -32767 ～ +32767。
// =============================================================================

enum GpButton {
    GP_A, GP_B, GP_X, GP_Y,
    GP_LB, GP_RB,
    GP_START, GP_BACK,
};

void setupGamepad();
void pollGamepad();          // 主迴圈高頻呼叫（驅動 Bluepad32 更新）
bool gamepadConnected();

int16_t gpAxisLX();          // 左搖桿 X（右 = +）
int16_t gpAxisLY();          // 左搖桿 Y（前推 = -）
int16_t gpAxisRY();          // 右搖桿 Y（前推 = -）

bool gpButton(GpButton b);
