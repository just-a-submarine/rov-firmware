// =============================================================================
//  ROV 潛水艇專案 —— 地面站（Ground Station）主程式
//
//  職責：Wi-Fi AP、WebSocket 推遙測、HTTP 收航點、ESP-NOW 轉發、Xbox 手把。
//  不負責影像（手機 <img> 直連 ROV 192.168.4.100）。
//  初始化順序依 doc/04 §七。
// =============================================================================
#include <Arduino.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include "diag.h"
#include "config.h"
#include "packets.h"
#include "net_wifi.h"
#include "espnow_link.h"
#include "web_server.h"
#include "gamepad.h"
#include "control.h"

// [診斷] GS→潛水艇 stream 探測：確認 ROV 的 MJPEG 端到端可達且有資料（排除相機問題）。
// GS 是 AP、ROV 是其 STA，這條路徑必通；若手機(client)看不到但這裡通 → 是 AP client↔client 轉發問題。
static void streamProbeTask(void*) {
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(8000));
        wifi_sta_list_t sl = {}; esp_wifi_ap_get_sta_list(&sl);
        if (sl.num == 0) { printf("[PROBE] 跳過（無關聯 STA）\n"); continue; }
        WiFiClient c;
        printf("[PROBE] 連線 192.168.4.100:80 /stream …\n");
        if (!c.connect(IPAddress(192, 168, 4, 100), 80, 4000)) {
            printf("[PROBE] connect 失敗（GS 連不到 ROV stream）\n");
            continue;
        }
        c.print("GET /stream HTTP/1.1\r\nHost: 192.168.4.100\r\nConnection: close\r\n\r\n");
        uint32_t t0 = millis(); int total = 0, li = 0; char line[160]; bool gotStatus = false;
        while (c.connected() && millis() - t0 < 4000 && total < 6000) {
            while (c.available()) {
                int ch = c.read(); total++;
                if (!gotStatus) {
                    if (ch == '\n') { line[li] = 0; printf("[PROBE] 狀態列: %s\n", line); gotStatus = true; }
                    else if (li < 158 && ch != '\r') line[li++] = ch;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        printf("[PROBE] 收到 %d bytes（>0＝ROV stream 可達且有影像資料）\n", total);
        c.stop();
    }
}

void setup() {
    // 不呼叫 Serial.begin()：它會重設 UART0、把 IDF console 接管掉，導致 printf 失效。
    // 全程用 printf 走 IDF console（UART0 → COM4）。
    printf("\n[GS] >>> setup() start\n");

    setupWifi_GS();      // SoftAP 啟動（必須在 ESP-NOW 之前）
    printf("[GS] wifi done\n");
    setupESPNOW_GS();    // ESP-NOW 初始化
    printf("[GS] espnow done\n");
    setupWebServer();    // LittleFS + HTTP + WebSocket
    printf("[GS] web done\n");
    setupGamepad();      // 控制改由手機 WS；GS 藍牙停用（BT 不開機，釋放 RAM）
    xTaskCreate(streamProbeTask, "probe", 4096, nullptr, 1, nullptr);  // [診斷] GS→ROV stream 探測
    printf("[GS] >>> setup() done, ready.\n");
}

void loop() {
    static uint32_t lastControlMs = 0;
    static uint32_t lastPushMs    = 0;
    static TelemetryPacket telemCache = {};
    static bool haveTelem = false;

    webServerLoop();     // 清理斷線 WebSocket 客戶端

    // 控制指令 ≈ 100Hz（讀手把 + ESP-NOW 送出）
    uint32_t now = millis();
    if (now - lastControlMs >= 10) {
        lastControlMs = now;
        readXboxAndSend();
    }

    // 收到的最新遙測先快取
    if (takeLatestTelemetry(telemCache)) haveTelem = true;

    // 遙測推送節流（5–10Hz，避免擠壓 ESP-NOW 無線電時間）
    if (haveTelem && now - lastPushMs >= TELEMETRY_PUSH_INTERVAL_MS) {
        lastPushMs = now;
        broadcastTelemetry(telemCache);
    }

#if defined(GS_DIAG_STA) && GS_DIAG_STA
    // [診斷] 每 3 秒印出已關聯的 AP 客戶端數 + 是否收到 ROV 遙測
    static uint32_t lastStaMs = 0;
    if (now - lastStaMs >= 3000) {
        lastStaMs = now;
        wifi_sta_list_t sl = {};
        esp_wifi_ap_get_sta_list(&sl);
        printf("[DIAG] AP stations=%d  haveTelem=%d  wsCtrl=%d  btGamepad=%d\n",
               sl.num, (int)haveTelem, (int)gamepadConnected(), (int)btControllerConnected());
        for (int i = 0; i < sl.num; i++) {
            const uint8_t* m = sl.sta[i].mac;
            printf("       sta%d %02X:%02X:%02X:%02X:%02X:%02X rssi=%d (GS聽ROV)\n",
                   i, m[0], m[1], m[2], m[3], m[4], m[5], sl.sta[i].rssi);
        }
        // 潛水艇自量(ROV 聽 GS)的 RSSI + 電量/電流（天線/電量診斷）
        printf("       ROV自量: rssi=%d dBm  bat=%d%%  cur=%.2fA  depth=%.2fm  cam(影格/-1停用)=%.0f\n",
               (int)telemCache.rssi, telemCache.batPct, telemCache.currentA, telemCache.depthM,
               telemCache.navDistanceM);
    }
#endif

    delay(1);  // 讓出 CPU 給 Wi-Fi / 藍牙協議堆疊
}
