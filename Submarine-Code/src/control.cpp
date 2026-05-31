#include "control.h"
#include "config.h"
#include "shared_state.h"
#include "motors.h"
#include "sensors.h"
#include "comms.h"
#include "camera_stream.h"
#include "navigation.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {

void applyControl(const ControlPacket& pkt, const TelemetrySnapshot& snap,
                  NavResult& navOut) {
    // 1. 緊急停車最高優先（doc/06 §八）
    //    emergencyStop() 會拉低 MCP 的 EN 腳；解除時若不重新致能，馬達會永遠不動。
    static bool wasEStop = false;
    if (pkt.emergencyStop) {
        emergencyStop();
        wasEStop = true;
        navOut = NavResult();        // active=false, idx=NONE
        return;
    }
    if (wasEStop) {                  // 急停 → 正常：重新致能馬達 EN（再按一次 Start 解鎖）
        enableMotors();
        wasEStop = false;
    }

    // 2. 串流模式 / 拍照旗標轉交相機
    cameraSetStreamMode(pkt.streamMode);
    if (pkt.takePhoto) cameraRequestPhoto();

    // 3. LED：Y 鍵狀態（pkt.ledOn）最高優先；否則深度 > 5cm 自動開（doc/06 §七）
    setLed(pkt.ledOn || (snap.depthM > DEPTH_LED_THRESH_M));

    // 4. 垂直馬達：右搖桿永遠有效，放開時 PID 維持深度
    setVertMotor(computeVertMotor(snap.depthM, pkt.vertMotor));

    // 5. 水平馬達：自動模式由導航算（左搖桿鎖定），否則用搖桿差速值
    if (pkt.autoMode) {
        navOut = computeNavigation(snap.lat, snap.lng, snap.headingDeg, snap.gpsValid);
        setLeftMotor(navOut.left);
        setRightMotor(navOut.right);
    } else {
        navOut = NavResult();
        setLeftMotor(pkt.leftMotor);
        setRightMotor(pkt.rightMotor);
    }
}

// 失控保護：太久沒收到控制封包 → 三馬達停（垂直交 PID 維持深度）
void applyFailsafe(const TelemetrySnapshot& snap) {
    setLeftMotor(0);
    setRightMotor(0);
    setVertMotor(computeVertMotor(snap.depthM, 0));
}

void controlTask(void*) {
    const TickType_t period = pdMS_TO_TICKS(1000 / CONTROL_HZ);
    TickType_t last = xTaskGetTickCount();

    for (;;) {
        TelemetrySnapshot snap = getTelemetry();
        readAllSensors(snap);

        // 新航點批次 → 重置導航進度
        if (waypointsUpdated()) { navResetProgress(); clearWaypointsUpdated(); }

        NavResult nav;
        if (millis() - lastControlMs() > CONTROL_LOST_MS) {
            applyFailsafe(snap);
        } else {
            applyControl(getControl(), snap, nav);
        }

        // 把導航狀態併入快照供遙測
        snap.navWaypointIdx = nav.active ? nav.wpIdx : NAV_IDX_NONE;
        snap.navDistanceM   = nav.distM;
        setTelemetry(snap);

        vTaskDelayUntil(&last, period);
    }
}

void networkTask(void*) {
    const TickType_t period = pdMS_TO_TICKS(1000 / TELEMETRY_HZ);
    TickType_t last = xTaskGetTickCount();

    for (;;) {
        wifiReconnectWatchdog();                   // STA 斷線自癒（被 deauth/GS 重啟後重連）
        int8_t rssi = currentRssi();
        cameraSetRssi(rssi);                       // 供 streamTask 做 RSSI 降級
        logWifiDiag();                             // 天線診斷（WIFI_DIAG，每 2s）

        TelemetrySnapshot snap = getTelemetry();
        TelemetryPacket pkt = {};
        pkt.lat            = snap.lat;
        pkt.lng            = snap.lng;
        pkt.depthM         = snap.depthM;
        pkt.currentA       = snap.currentA;
        pkt.powerW         = snap.powerW;
        pkt.batPct         = snap.batPct;
        pkt.rssi           = rssi;
        pkt.streamMode     = cameraGetStreamMode();
        pkt.headingDeg     = snap.headingDeg;   // 校正後航向（前端 marker 朝向）
        pkt.magX           = snap.magX;         // 原始磁場（手機端羅盤校準收 min/max）
        pkt.magY           = snap.magY;
        pkt.navWaypointIdx = snap.navWaypointIdx;
        pkt.navDistanceM   = snap.navDistanceM;
        // [診斷] 手動模式下借 navDistanceM 回傳相機狀態：-1=串流停用，否則=已發佈影格數
        if (snap.navWaypointIdx == NAV_IDX_NONE)
            pkt.navDistanceM = cameraStreamActive() ? (float)cameraFrameSeq() : -1.0f;
        pkt.photoAck       = cameraConsumePhotoAck();
        pkt.ledOn          = ledIsOn();
        pkt.msgType        = MSG_TELEMETRY;
        sendTelemetry(pkt);

        vTaskDelayUntil(&last, period);
    }
}
}  // namespace

void startControlTasks() {
    // 優先級（doc/05 §排程優先級）：控制(5) ≥ 遙測(4) > 相機HTTP(2) > 相機擷取(1)。
    // controlTask 獨佔 Core1（相機 HTTP 已釘 Core0，不再搶佔控制迴圈）；
    // networkTask 在 Core0 高於相機，確保 ESP-NOW 控制/遙測不被 MJPEG 串流卡住。
    xTaskCreatePinnedToCore(controlTask, "Control", 8192, nullptr, 5, nullptr, 1);
    xTaskCreatePinnedToCore(networkTask, "Network", 8192, nullptr, 4, nullptr, 0);
}
