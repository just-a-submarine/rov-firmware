#include "shared_state.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>

namespace {
SemaphoreHandle_t g_mutex = nullptr;

ControlPacket     g_control = {};
uint32_t          g_lastControlMs = 0;

TelemetrySnapshot g_telemetry = {};

Waypoint          g_waypoints[MAX_WAYPOINTS];
int               g_waypointCount = 0;

struct Lock {
    Lock()  { if (g_mutex) xSemaphoreTake(g_mutex, portMAX_DELAY); }
    ~Lock() { if (g_mutex) xSemaphoreGive(g_mutex); }
};
}  // namespace

void sharedStateInit() {
    if (!g_mutex) g_mutex = xSemaphoreCreateMutex();
}

void setControl(const ControlPacket& pkt) {
    Lock lk;
    g_control = pkt;
    g_lastControlMs = millis();
}

ControlPacket getControl() {
    Lock lk;
    return g_control;
}

uint32_t lastControlMs() {
    Lock lk;
    return g_lastControlMs;
}

void setTelemetry(const TelemetrySnapshot& t) {
    Lock lk;
    g_telemetry = t;
}

TelemetrySnapshot getTelemetry() {
    Lock lk;
    return g_telemetry;
}

void setWaypoints(const Waypoint* wps, int count) {
    if (count < 0) count = 0;
    if (count > MAX_WAYPOINTS) count = MAX_WAYPOINTS;
    Lock lk;
    memcpy(g_waypoints, wps, sizeof(Waypoint) * count);
    g_waypointCount = count;
}

int getWaypoints(Waypoint* out, int maxOut) {
    Lock lk;
    int n = (g_waypointCount < maxOut) ? g_waypointCount : maxOut;
    memcpy(out, g_waypoints, sizeof(Waypoint) * n);
    return n;
}

int getWaypointCount() {
    Lock lk;
    return g_waypointCount;
}
