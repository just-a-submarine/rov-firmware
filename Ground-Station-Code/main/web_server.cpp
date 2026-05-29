#include "web_server.h"
#include "espnow_link.h"
#include "control.h"
#include "gamepad.h"
#include "config.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");

// 航點暫存
static Waypoint s_waypoints[MAX_WAYPOINTS];
static int      s_waypointCount = 0;

// 航點 POST body 累積緩衝（payload 可能分多次抵達）
static String   s_postBody;

static void onWSEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                      AwsEventType type, void* arg, uint8_t* data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        printf("[WS] client #%u connected\n", client->id());
    } else if (type == WS_EVT_DISCONNECT) {
        printf("[WS] client #%u disconnected\n", client->id());
    } else if (type == WS_EVT_DATA) {
        // 手機端上行：手把控制 {"t":"c","lx":..,"ly":..,"ry":..,"b":bitmask}
        AwsFrameInfo* info = (AwsFrameInfo*)arg;
        if (!info->final || info->index != 0 || info->len != len) return;  // 只收單幀小封包
        JsonDocument doc;
        if (deserializeJson(doc, data, len)) return;                       // 解析失敗忽略
        if (doc["t"] != "c") return;
        int lx = doc["lx"] | 0, ly = doc["ly"] | 0, ry = doc["ry"] | 0;
        uint16_t b = (uint16_t)(doc["b"] | 0);
        gamepadSetRemote((int16_t)constrain(lx, -32767, 32767),
                         (int16_t)constrain(ly, -32767, 32767),
                         (int16_t)constrain(ry, -32767, 32767), b);
    }
}

static void handleWaypointBody(AsyncWebServerRequest* req, uint8_t* data,
                               size_t len, size_t index, size_t total) {
    if (index == 0) s_postBody = "";
    s_postBody.concat((const char*)data, len);
    if (index + len < total) return;  // 尚未收完

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, s_postBody);
    s_postBody = "";
    if (err) {
        req->send(400, "application/json", "{\"error\":\"bad json\"}");
        return;
    }

    JsonArray wps = doc["waypoints"].as<JsonArray>();
    s_waypointCount = 0;
    for (JsonObject wp : wps) {
        if (s_waypointCount >= MAX_WAYPOINTS) break;
        s_waypoints[s_waypointCount].lat   = wp["lat"]   | 0.0;
        s_waypoints[s_waypointCount].lng   = wp["lng"]   | 0.0;
        s_waypoints[s_waypointCount].order = wp["order"] | s_waypointCount;
        s_waypointCount++;
    }

    sendWaypointsToROV(s_waypoints, s_waypointCount);
    String resp = String("{\"status\":\"ok\",\"count\":") + s_waypointCount + "}";
    req->send(200, "application/json", resp);
}

void setupWebServer() {
    if (!LittleFS.begin(true)) {
        printf("[FS] LittleFS mount failed\n");
    }

    // no-cache：每次載入用 ETag 重新驗證（未變動回 304，仍省頻寬），
    // 避免改版後手機載到舊快取（max-age 會卡住更新最長 10 分鐘）。
    server.serveStatic("/", LittleFS, "/www/")
          .setDefaultFile("index.html")
          .setCacheControl("no-cache");

    server.on("/api/waypoints", HTTP_POST,
        [](AsyncWebServerRequest* req) {},   // 完成回應在 body handler 內送出
        nullptr,
        handleWaypointBody);

    ws.onEvent(onWSEvent);
    server.addHandler(&ws);
    server.begin();
    printf("[HTTP] server started on :80\n");
}

void broadcastTelemetry(const TelemetryPacket& pkt) {
    if (ws.count() == 0) return;  // 沒人在聽就別浪費 CPU

    JsonDocument doc;
    doc["lat"]        = pkt.lat;
    doc["lng"]        = pkt.lng;
    doc["depth"]      = pkt.depthM;
    doc["current"]    = pkt.currentA;
    doc["power"]      = pkt.powerW;
    doc["bat"]        = pkt.batPct;
    doc["rssi"]       = pkt.rssi;
    doc["streamMode"] = pkt.streamMode;
    doc["navWpIdx"]   = pkt.navWaypointIdx;
    doc["navDistM"]   = pkt.navDistanceM;
    doc["photoAck"]   = pkt.photoAck;
    doc["led"]        = pkt.ledOn;               // 潛水艇燈實際狀態（手機狀態列顯示）
    doc["estop"]      = controlEstopLatched();   // 地面站本機急停 latch 狀態

    String out;
    serializeJson(doc, out);
    ws.textAll(out);
}

void webServerLoop() {
    ws.cleanupClients();
}
