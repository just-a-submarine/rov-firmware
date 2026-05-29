#include "comms.h"
#include "config.h"
#include "shared_state.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

namespace {
volatile bool g_waypointsUpdated = false;

// 航點塊組裝暫存
Waypoint g_wpAssembly[MAX_WAYPOINTS];
int      g_wpAssembled = 0;
uint8_t  g_wpTotalChunks = 0;
uint8_t  g_wpReceivedMask = 0;     // 已收到的 chunk bitmask（最多支援 8 塊 = 112 航點，足夠）

void handleControl(const uint8_t* data, int len) {
    if (len < (int)sizeof(ControlPacket)) return;
    ControlPacket pkt;
    memcpy(&pkt, data, sizeof(pkt));
    setControl(pkt);
}

void handleWaypointChunk(const uint8_t* data, int len) {
    if (len < (int)sizeof(WaypointChunk)) return;
    WaypointChunk chunk;
    memcpy(&chunk, data, sizeof(chunk));
    if (chunk.chunkIndex >= 8 || chunk.totalChunks == 0) return;

    // 新批次（第 0 塊）→ 重置組裝狀態
    if (chunk.chunkIndex == 0) {
        g_wpAssembled   = 0;
        g_wpReceivedMask = 0;
        g_wpTotalChunks = chunk.totalChunks;
    }

    int base = chunk.chunkIndex * WP_PER_CHUNK;
    int n = chunk.wpCount;
    if (n > WP_PER_CHUNK) n = WP_PER_CHUNK;
    for (int i = 0; i < n && (base + i) < MAX_WAYPOINTS; i++) {
        g_wpAssembly[base + i].lat   = chunk.wps[i].lat;
        g_wpAssembly[base + i].lng   = chunk.wps[i].lng;
        g_wpAssembly[base + i].order = chunk.wps[i].order;
        if (base + i + 1 > g_wpAssembled) g_wpAssembled = base + i + 1;
    }
    g_wpReceivedMask |= (1 << chunk.chunkIndex);

    // 所有塊到齊 → 寫入共享狀態
    uint8_t fullMask = (1 << g_wpTotalChunks) - 1;
    if (g_wpReceivedMask == fullMask) {
        setWaypoints(g_wpAssembly, g_wpAssembled);
        g_waypointsUpdated = true;
        log_i("航點批次接收完成：%d 點", g_wpAssembled);
    }
}

void onDataRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    // 以封包長度分流，最不易誤判（ControlPacket=12B、WaypointChunk=243B）。
    if (len == (int)sizeof(WaypointChunk) && data[0] == MSG_WAYPOINT) {
        handleWaypointChunk(data, len);
    } else if (len == (int)sizeof(ControlPacket)) {
        handleControl(data, len);
    }
}

void onDataSent(const uint8_t* mac, esp_now_send_status_t status) {
    if (status != ESP_NOW_SEND_SUCCESS) {
        // 靜默失敗多半是 peer MAC / channel 不符（doc/04 §三）
        static uint32_t lastWarn = 0;
        if (millis() - lastWarn > 2000) {
            log_w("ESP-NOW 送出失敗（檢查 GS_AP_MAC / 頻道）");
            lastWarn = millis();
        }
    }
}

bool initEspNow() {
    if (esp_now_init() != ESP_OK) {
        log_e("esp_now_init 失敗");
        return false;
    }
    esp_now_register_recv_cb(onDataRecv);
    esp_now_register_send_cb(onDataSent);

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, GS_AP_MAC, 6);
    peer.channel = 0;                  // 跟隨本機頻道（最安全）
    peer.ifidx   = WIFI_IF_STA;        // ROV 用 STA 介面（doc/04）
    peer.encrypt = false;
    if (esp_now_add_peer(&peer) != ESP_OK) {
        log_e("esp_now_add_peer 失敗");
        return false;
    }
    return true;
}
}  // namespace

bool setupComms() {
#ifdef STANDALONE_TEST
    // 單機測試：ROV 自開 SoftAP，手邊無地面站也能連線測串流/感測器
    WiFi.onEvent([](arduino_event_id_t e, arduino_event_info_t) {
        log_i("[STANDALONE] 有裝置連上 AP（事件 %d）", (int)e);
    }, ARDUINO_EVENT_WIFI_AP_STACONNECTED);
    WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t info) {
        log_i("[STANDALONE] 已配發 IP 給裝置：%d.%d.%d.%d",
              info.wifi_ap_staipassigned.ip.addr & 0xFF,
              (info.wifi_ap_staipassigned.ip.addr >> 8) & 0xFF,
              (info.wifi_ap_staipassigned.ip.addr >> 16) & 0xFF,
              (info.wifi_ap_staipassigned.ip.addr >> 24) & 0xFF);
    }, ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED);

    WiFi.mode(WIFI_MODE_AP);
    WiFi.softAP(STANDALONE_AP_SSID, STANDALONE_AP_PASS, WIFI_CHANNEL);
    esp_wifi_set_ps(WIFI_PS_NONE);
    log_i("[STANDALONE] SoftAP 啟動：%s  IP=%s",
          STANDALONE_AP_SSID, WiFi.softAPIP().toString().c_str());
    return true;   // 單機模式不啟用 ESP-NOW（無地面站）
#else
    // 正式：純 STA 連入地面站 AP，固定 IP
    WiFi.mode(WIFI_MODE_STA);
    WiFi.config(IPAddress(ROV_IP), IPAddress(ROV_GATEWAY), IPAddress(ROV_SUBNET));
    WiFi.begin(AP_SSID, AP_PASS);
    esp_wifi_set_ps(WIFI_PS_NONE);     // 關省電防漏包（doc/04）
    esp_wifi_set_max_tx_power(84);     // 最大發射功率（~20dBm，單位 0.25dBm）改善弱訊號

    log_i("ROV STA MAC: %s（填入地面站 peer_addr）", WiFi.macAddress().c_str());

    // 等待連上 AP（最多 ~10 秒；連不上也繼續，之後背景重連）
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) delay(200);
    if (WiFi.status() == WL_CONNECTED)
        log_i("已連上地面站 AP，IP=%s", WiFi.localIP().toString().c_str());
    else
        log_w("尚未連上地面站 AP（背景持續重連）");

    return initEspNow();
#endif
}

void sendTelemetry(const TelemetryPacket& pkt) {
#ifndef STANDALONE_TEST
    esp_now_send(GS_AP_MAC, (const uint8_t*)&pkt, sizeof(pkt));
#endif
}

int8_t currentRssi() {
#ifdef STANDALONE_TEST
    return 0;
#else
    return (WiFi.status() == WL_CONNECTED) ? (int8_t)WiFi.RSSI() : -127;
#endif
}

void wifiReconnectWatchdog() {
#ifndef STANDALONE_TEST
    // STA 被 deauth/干擾後可能卡住不自動重連；未關聯時每 5s 重發 begin() 自癒。
    static uint32_t lastTry = 0;
    if (WiFi.status() == WL_CONNECTED) return;
    if (millis() - lastTry < 5000) return;
    lastTry = millis();
    WiFi.disconnect();
    WiFi.begin(AP_SSID, AP_PASS);
    log_w("[WiFi] STA 未關聯，重連 %s …", AP_SSID);
#endif
}

void logWifiDiag() {
#if defined(WIFI_DIAG) && WIFI_DIAG && !defined(STANDALONE_TEST)
    static uint32_t last = 0;
    if (millis() - last < 2000) return;
    last = millis();
    wl_status_t st = WiFi.status();
    int8_t txp = 0;
    esp_wifi_get_max_tx_power(&txp);
    // 兩板靠近(~20cm)時 rssi 仍 < -65 → 高度懷疑天線(0Ω 切換)不良。
    log_i("[WiFiDiag] status=%d(%s) rssi=%d dBm ch=%d ip=%s txpwr=%d(x0.25dBm)",
          (int)st, (st == WL_CONNECTED ? "CONNECTED" : "NOT_CONN"),
          (int)WiFi.RSSI(), WiFi.channel(),
          WiFi.localIP().toString().c_str(), (int)txp);
#endif
}

bool waypointsUpdated()      { return g_waypointsUpdated; }
void clearWaypointsUpdated() { g_waypointsUpdated = false; }
