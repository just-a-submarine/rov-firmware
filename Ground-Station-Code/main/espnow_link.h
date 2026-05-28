#pragma once
#include "packets.h"

// 航點型別（手機上傳後暫存於地面站）。
struct Waypoint {
    double  lat;
    double  lng;
    uint8_t order;
};

// ESP-NOW 連結層：與 ROV 雙向溝通。
// 必須在 Wi-Fi AP 啟動【之後】初始化（doc/04 §七）。
void setupESPNOW_GS();

// 送一筆控制指令給 ROV。
bool sendControl(const ControlPacket& pkt);

// 把航點陣列分塊（每塊 14 點）送給 ROV（doc/04 §五）。
void sendWaypointsToROV(const Waypoint* wps, int count);

// 取最新一筆遙測；若自上次取用後有收到新資料則回傳 true。
bool takeLatestTelemetry(TelemetryPacket& out);
