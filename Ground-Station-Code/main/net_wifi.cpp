#include "net_wifi.h"
#include "config.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>

void setupWifi_GS() {
    // 純 AP 模式：避免 APSTA 模式下 STA home channel 強制覆蓋 AP channel 的陷阱
    printf("[WiFi] setting AP mode...\n");
    WiFi.mode(WIFI_AP);
    bool ok = WiFi.softAP(AP_SSID, AP_PASS, WIFI_CHANNEL);
    printf("[WiFi] softAP(%s, ch%d) -> %s\n", AP_SSID, WIFI_CHANNEL, ok ? "OK" : "FAIL");
    esp_wifi_set_ps(WIFI_PS_NONE);  // 關閉省電，防 ESP-NOW / WebSocket 漏包

    printf("[WiFi] GS AP IP : %s\n", WiFi.softAPIP().toString().c_str());
    printf("[WiFi] GS AP MAC: %s\n", WiFi.softAPmacAddress().c_str());  // 填入 ROV 端 gs_ap_mac
}
