#include "net_wifi.h"
#include "config.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include <esp_coexist.h>

void setupWifi_GS() {
    // 在 WiFi 初始化「之前」釘住 SoftAP 介面 MAC：替換板 base MAC 不同，但 ROV 端把
    // GS_AP_MAC 寫死（include/config.h），釘成固定值讓任何替換板 drop-in、ROV 免改免重燒。
    // 必須在 esp_wifi_init() 之前呼叫——setupWifi_GS 是開機第一個碰 WiFi 的函式，符合條件。
    // （不用 esp_wifi_set_mac()：那需 stop/start，會打亂 Arduino WiFi 狀態機 → softAP 設定失效。）
    esp_err_t me = esp_iface_mac_addr_set(GS_AP_MAC, ESP_MAC_WIFI_SOFTAP);
    printf("[WiFi] pin AP MAC %02X:%02X:%02X:%02X:%02X:%02X -> %s\n",
           GS_AP_MAC[0], GS_AP_MAC[1], GS_AP_MAC[2], GS_AP_MAC[3], GS_AP_MAC[4], GS_AP_MAC[5],
           me == ESP_OK ? "OK" : esp_err_to_name(me));

    // 純 AP 模式：避免 APSTA 模式下 STA home channel 強制覆蓋 AP channel 的陷阱
    printf("[WiFi] setting AP mode...\n");
    WiFi.mode(WIFI_AP);
    bool ok = WiFi.softAP(AP_SSID, AP_PASS, WIFI_CHANNEL);
    printf("[WiFi] softAP(%s, ch%d) -> %s\n", AP_SSID, WIFI_CHANNEL, ok ? "OK" : "FAIL");
    esp_wifi_set_ps(WIFI_PS_NONE);  // 關閉省電，防 ESP-NOW / WebSocket 漏包

    // 強制 AP 走 HT20（20MHz）。開機 log 實測 STA 會以 40U(HT40) 關聯：HT40 在已壅塞的
    // 2.4GHz 把發射功率攤到兩倍頻寬 → 接收靈敏度約 -3dB、且更易被鄰台干擾，弱訊號（~-65dBm）
    // 下 beacon/WPA2 握手最先掉包 → 「時而掃不到 / 掃到連不上」。鎖 HT20 換回約 3dB 靈敏度與
    // 抗干擾餘裕，把失敗門檻推遠（ESP-NOW 固定低速率不受影響；STA 頻寬跟隨 AP，ROV 端免改）。
    esp_err_t bw = esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);
    printf("[WiFi] set AP bandwidth HT20 -> %s\n", bw == ESP_OK ? "OK" : "FAIL");

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
