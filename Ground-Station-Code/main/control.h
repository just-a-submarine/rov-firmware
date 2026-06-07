#pragma once

struct MotorPair { int left; int right; };

// 極座標歸一化差速轉向（doc/06 §一之一）
//  輸入：lx, ly ∈ [-32767, +32767]（lx 右為正、ly 前推為負）
//  輸出：left, right ∈ [-1023, +1023]
MotorPair computeDifferential(int lx, int ly);

// 讀手把 → 混控 → 邊緣觸發狀態機 → 打包 ControlPacket → ESP-NOW 送出。
// 主迴圈高頻呼叫（doc/04 §七 loop）。
void readXboxAndSend();

// 急停 latch 狀態（供 Web 顯示橫幅）。
bool controlEstopLatched();

// 最近一次送出的馬達指令（-1023~1023）；供 Web 狀態列顯示左/右/垂直轉速 %。
int controlLastMotorL();
int controlLastMotorR();
int controlLastMotorV();
