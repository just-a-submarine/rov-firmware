#pragma once
#include "packets.h"

// 啟動 HTTP + WebSocket 伺服器，並掛載 LittleFS 內的手機網頁。
void setupWebServer();

// 把一筆遙測序列化成 JSON 推給所有 WebSocket 客戶端（doc/04 §四）。
void broadcastTelemetry(const TelemetryPacket& pkt);

// 主迴圈定期呼叫，清理斷線的 WebSocket 客戶端。
void webServerLoop();
