#pragma once

struct MotorPair { int left; int right; };

// 極座標歸一化差速轉向（doc/06 §一之一）
//  輸入：lx, ly ∈ [-32767, +32767]（lx 右為正、ly 前推為負）
//  輸出：left, right ∈ [-1023, +1023]
MotorPair computeDifferential(int lx, int ly);

// 讀手把 → 混控 → 邊緣觸發狀態機 → 打包 ControlPacket → ESP-NOW 送出。
// 主迴圈高頻呼叫（doc/04 §七 loop）。
void readXboxAndSend();
