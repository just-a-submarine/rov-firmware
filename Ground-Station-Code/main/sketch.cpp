// =============================================================================
//  ROV 潛水艇專案 —— 地面站（Ground Station）主程式
//
//  職責：Wi-Fi AP、WebSocket 推遙測、HTTP 收航點、ESP-NOW 轉發、Xbox 手把。
//  不負責影像（手機 <img> 直連 ROV 192.168.4.100）。
//  初始化順序依 doc/04 §七。
// =============================================================================
#include <Arduino.h>
#include "config.h"
#include "packets.h"
#include "net_wifi.h"
#include "espnow_link.h"
#include "web_server.h"
#include "gamepad.h"
#include "control.h"

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
    setupGamepad();      // Xbox 手把（藍牙 Bluepad32）
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

    delay(1);  // 讓出 CPU 給 Wi-Fi / 藍牙協議堆疊
}
