#pragma once
#include <Arduino.h>
#include "packets.h"

// Wi-Fi（STA 連地面站 / 單機模式自開 AP）+ ESP-NOW 收發（doc/04）

bool   setupComms();                       // Wi-Fi + ESP-NOW 初始化
void   sendTelemetry(const TelemetryPacket& pkt);
int8_t currentRssi();                      // ROV 對 AP 的 RSSI（dBm）
void   logWifiDiag();                       // 每 2s 印 STA 狀態/RSSI/通道/IP/TX 功率（天線診斷，config.h WIFI_DIAG）
void   wifiReconnectWatchdog();             // STA 斷線自癒：未關聯時定期重連（被 deauth/干擾後自動恢復）

// 是否有新航點批次（航點塊組裝完成後置位，navigation 取用後清除）
bool   waypointsUpdated();
void   clearWaypointsUpdated();
