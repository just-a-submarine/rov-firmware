#include "net_wifi.h"
#include "config.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_coexist.h>

void setupWifi_GS() {
    // 純 AP 模式：避免 APSTA 模式下 STA home channel 強制覆蓋 AP channel 的陷阱
    printf("[WiFi] setting AP mode...\n");
    WiFi.mode(WIFI_AP);
    bool ok = WiFi.softAP(AP_SSID, AP_PASS, WIFI_CHANNEL);
    printf("[WiFi] softAP(%s, ch%d) -> %s\n", AP_SSID, WIFI_CHANNEL, ok ? "OK" : "FAIL");
    esp_wifi_set_ps(WIFI_PS_NONE);  // 關閉省電，防 ESP-NOW / WebSocket 漏包

    // Wi-Fi/BLE 共存：給 Wi-Fi 較高 RF 優先權，讓 AP beacon / WPA2 握手不被 BT 餓死。
    // （實測：顯式 coex_init()/coex_enable() 反而讓 STA 掉線，故不呼叫，僅設偏好。）
    esp_err_t cx = esp_coex_preference_set(ESP_COEX_PREFER_WIFI);
    // 最大化 AP 發射功率，改善覆蓋與弱客戶端（手機）連線。
    esp_wifi_set_max_tx_power(84);
    int8_t txp = 0; esp_wifi_get_max_tx_power(&txp);
    printf("[WiFi] coex_pref=%d  max_tx_power=%d (0.25dBm units)\n", (int)cx, (int)txp);

    printf("[WiFi] GS AP IP : %s\n", WiFi.softAPIP().toString().c_str());
    printf("[WiFi] GS AP MAC: %s\n", WiFi.softAPmacAddress().c_str());  // 填入 ROV 端 gs_ap_mac
}
