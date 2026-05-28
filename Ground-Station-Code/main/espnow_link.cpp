#include "espnow_link.h"
#include "config.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>

static uint8_t s_rovMac[6];

// 最新遙測（在收包 callback 寫入，主迴圈讀出）
static volatile bool       s_telemFresh = false;
static TelemetryPacket     s_latestTelem;
static portMUX_TYPE        s_telemMux = portMUX_INITIALIZER_UNLOCKED;

// ESP-NOW 收包 callback。Arduino core 2.x 與 3.x 簽名不同，以版本保護相容。
#if ESP_ARDUINO_VERSION_MAJOR >= 3
static void onDataRecv_GS(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
#else
static void onDataRecv_GS(const uint8_t* mac, const uint8_t* data, int len) {
#endif
    if (len < (int)sizeof(uint8_t)) return;
    uint8_t msgType = data[len - 1];  // msgType 為各封包最後一個欄位
    if (msgType == MSG_TELEMETRY && len == (int)sizeof(TelemetryPacket)) {
        portENTER_CRITICAL(&s_telemMux);
        memcpy((void*)&s_latestTelem, data, sizeof(TelemetryPacket));
        s_telemFresh = true;
        portEXIT_CRITICAL(&s_telemMux);
    }
}

static void onDataSent_GS(const uint8_t* mac, esp_now_send_status_t status) {
    // 靜默：成功/失敗統計可在偵錯時開啟
    // if (status != ESP_NOW_SEND_SUCCESS) Serial.println("[ESP-NOW] send fail");
}

void setupESPNOW_GS() {
    memcpy(s_rovMac, ROV_STA_MAC, 6);

    if (esp_now_init() != ESP_OK) {
        printf("[ESP-NOW] init failed\n");
        return;
    }
    esp_now_register_recv_cb(onDataRecv_GS);
    esp_now_register_send_cb(onDataSent_GS);

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, s_rovMac, 6);
    peer.channel = 0;             // 0 = 跟隨本機當前頻道，最安全
    peer.ifidx   = WIFI_IF_AP;    // 地面站用 AP 介面
    peer.encrypt = false;
    if (esp_now_add_peer(&peer) != ESP_OK) {
        printf("[ESP-NOW] add_peer failed (ROV_STA_MAC 是否已填？)\n");
    } else {
        printf("[ESP-NOW] ready\n");
    }
}

bool sendControl(const ControlPacket& pkt) {
    return esp_now_send(s_rovMac, (const uint8_t*)&pkt, sizeof(pkt)) == ESP_OK;
}

void sendWaypointsToROV(const Waypoint* wps, int count) {
    if (count <= 0) return;
    int totalChunks = (count + WP_PER_CHUNK - 1) / WP_PER_CHUNK;
    for (int c = 0; c < totalChunks; c++) {
        WaypointChunk chunk = {};
        chunk.msgType     = MSG_WAYPOINT;
        chunk.seqNum      = c;
        chunk.totalChunks = totalChunks;
        chunk.chunkIndex  = c;
        int remain = count - c * WP_PER_CHUNK;
        chunk.wpCount = remain < WP_PER_CHUNK ? remain : WP_PER_CHUNK;
        for (int i = 0; i < chunk.wpCount; i++) {
            const Waypoint& w = wps[c * WP_PER_CHUNK + i];
            chunk.wps[i].lat   = w.lat;
            chunk.wps[i].lng   = w.lng;
            chunk.wps[i].order = w.order;
        }
        esp_now_send(s_rovMac, (const uint8_t*)&chunk, sizeof(chunk));
        delay(50);  // 給接收端與無線電喘息空間
    }
    printf("[ESP-NOW] sent %d waypoints in %d chunk(s)\n", count, totalChunks);
}

bool takeLatestTelemetry(TelemetryPacket& out) {
    bool fresh = false;
    portENTER_CRITICAL(&s_telemMux);
    if (s_telemFresh) {
        memcpy(&out, (const void*)&s_latestTelem, sizeof(TelemetryPacket));
        s_telemFresh = false;
        fresh = true;
    }
    portEXIT_CRITICAL(&s_telemMux);
    return fresh;
}
