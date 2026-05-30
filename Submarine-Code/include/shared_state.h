#pragma once
#include <Arduino.h>
#include "packets.h"
#include "config.h"

// 跨 FreeRTOS 任務（controlTask / networkTask / streamTask / ESP-NOW callback）
// 共享的狀態，全部以單一 mutex 保護。存取一律走複製語意，避免持鎖期間做重活。

struct Waypoint {
    double  lat;
    double  lng;
    uint8_t order;
};

// 感測器快照：感測器讀取後寫入，遙測任務讀出打包
struct TelemetrySnapshot {
    float   lat = 0;
    float   lng = 0;
    float   depthM = 0;
    float   currentA = 0;
    float   powerW = 0;
    float   headingDeg = 0;
    float   magX = 0;       // 原始（未校正）磁場 X/Y（Gauss），供手機端羅盤校準
    float   magY = 0;
    uint8_t batPct = 0;
    int8_t  rssi = 0;
    uint8_t navWaypointIdx = NAV_IDX_NONE;
    float   navDistanceM = 0;
    bool    gpsValid = false;
};

void sharedStateInit();

// --- 控制封包（ESP-NOW recv 寫；controlTask 讀）---
void          setControl(const ControlPacket& pkt);
ControlPacket getControl();
uint32_t      lastControlMs();          // 最近一次收到控制封包的時間（millis）

// --- 遙測快照（感測器寫；networkTask 讀）---
void              setTelemetry(const TelemetrySnapshot& t);
TelemetrySnapshot getTelemetry();

// --- 航點（ESP-NOW 航點塊組裝後寫；navigation 讀）---
void setWaypoints(const Waypoint* wps, int count);
int  getWaypoints(Waypoint* out, int maxOut);   // 回傳實際複製數量
int  getWaypointCount();
