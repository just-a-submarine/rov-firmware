#include "web_server.h"
#include "espnow_link.h"
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
    }
    // 手機端只接收遙測，不回傳資料，故不處理 WS_EVT_DATA
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

    server.serveStatic("/", LittleFS, "/www/")
          .setDefaultFile("index.html")
          .setCacheControl("max-age=600");

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

    String out;
    serializeJson(doc, out);
    ws.textAll(out);
}

void webServerLoop() {
    ws.cleanupClients();
}
