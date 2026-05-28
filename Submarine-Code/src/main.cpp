#include <Arduino.h>
#include <Wire.h>
#include "config.h"
#include "shared_state.h"
#include "motors.h"
#include "sensors.h"
#include "comms.h"
#include "camera_stream.h"
#include "recorder.h"
#include "navigation.h"
#include "control.h"

// 潛水艇 (ROV) 主程式 — 初始化順序依 doc/06 §十一
// 安全要點：enableMotors() 必須最後呼叫，確保開機期間馬達電路斷開。

void setup() {
    Serial.begin(115200);          // USB CDC（USB CDC On Boot 啟用）
    delay(300);
#ifdef ENABLE_GPS
    // GPS 佔用 43/44（UART0 偵錯腳）→ 把 log 導到原生 USB，否則 CH340 看不到。
    Serial.setDebugOutput(true);
#endif
    log_i("=== ROV 啟動 ===");

    sharedStateInit();
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

    setupMotors();                 // MCP23017：EN 預設關、繼電器不動作
    setupSensors();                // 羅盤 / 深度 / 電流（GPS 見 config.h ENABLE_GPS）
    setupDepthPID();

    // 網路必須先於相機 HTTP server：httpd_start 需要 lwIP/TCP-IP 堆疊已就緒
    setupComms();                  // Wi-Fi STA（或單機 AP）+ ESP-NOW

    if (!recorderBeginSD()) log_w("SD 卡未就緒，錄影/拍照停用");
    if (!setupCamera())     log_e("相機初始化失敗");

    startCameraTask();             // streamTask（Core0）
    startControlTasks();           // controlTask（Core1）+ networkTask（Core0）

    enableMotors();                // 最後才啟用馬達 EN（doc/03 安全啟動）
    log_i("=== 初始化完成 ===");
}

void loop() {
    // 所有工作都在 FreeRTOS 任務中執行；主迴圈僅閒置。
    vTaskDelay(pdMS_TO_TICKS(1000));
}
