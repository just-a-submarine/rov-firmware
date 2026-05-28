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
    if (pkt.emergencyStop) {
        emergencyStop();
        navOut = NavResult();        // active=false, idx=NONE
        return;
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
        int8_t rssi = currentRssi();
        cameraSetRssi(rssi);                       // 供 streamTask 做 RSSI 降級

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
        pkt.navWaypointIdx = snap.navWaypointIdx;
        pkt.navDistanceM   = snap.navDistanceM;
        pkt.photoAck       = cameraConsumePhotoAck();
        pkt.msgType        = MSG_TELEMETRY;
        sendTelemetry(pkt);

        vTaskDelayUntil(&last, period);
    }
}
}  // namespace

void startControlTasks() {
    xTaskCreatePinnedToCore(controlTask, "Control", 8192, nullptr, 3, nullptr, 1);
    xTaskCreatePinnedToCore(networkTask, "Network", 8192, nullptr, 2, nullptr, 0);
}
