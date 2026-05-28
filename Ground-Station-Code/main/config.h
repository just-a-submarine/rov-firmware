#pragma once
#include <stdint.h>

// =============================================================================
//  地面站全域設定
// =============================================================================

// ---- Wi-Fi AP ----
#define WIFI_CHANNEL  1
#define AP_SSID       "ROV_GS"
#define AP_PASS       "rov12345"

// ---- ESP-NOW 配對 ----
// ROV 的 STA MAC（14:C1:9F:29:E0:B8）。
// 開機時序列埠會印出本機（地面站）的 AP MAC，記得填到 ROV 端的 gs_ap_mac。
static const uint8_t ROV_STA_MAC[6] = {0x14, 0xC1, 0x9F, 0x29, 0xE0, 0xB8};

// ---- 遙測推送節流（WebSocket → 手機）----
// 5–10Hz；超過 20Hz 會擠壓 ESP-NOW 無線電時間（doc/04 §四）
#define TELEMETRY_PUSH_INTERVAL_MS  150   // ≈ 6.7Hz

// ---- 手把 ----
#define DEADZONE_RATIO  0.06f             // 死區（≈ 2000/32767）

// ---- 航點 ----
#define MAX_WAYPOINTS   50
#define WP_PER_CHUNK    14
