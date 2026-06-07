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
static const uint8_t ROV_STA_MAC[6] = {0x14, 0xC1, 0x9F, 0x29, 0xE0, 0xB8};

// 本機（地面站）AP MAC：開機時用 esp_wifi_set_mac() 強制釘成此固定值。
// ROV 端把 GS_AP_MAC 寫死（include/config.h），故釘住後任何替換板都 drop-in、ROV 免改免重燒。
// 第一位元組 bit0 必須為 0（單播），0x14 符合。
static const uint8_t GS_AP_MAC[6] = {0x14, 0xC1, 0x9F, 0x29, 0xEA, 0xAD};

// ---- 遙測推送節流（WebSocket → 手機）----
// 5–10Hz；超過 20Hz 會擠壓 ESP-NOW 無線電時間（doc/04 §四）
#define TELEMETRY_PUSH_INTERVAL_MS  150   // ≈ 6.7Hz

// ---- 手把（控制來源 = 手機 Gamepad API → WebSocket）----
#define DEADZONE_RATIO  0.06f             // 死區（≈ 2000/32767）
// WS 控制逾時（毫秒）：超過此時間沒收到手機控制封包 → 視為失聯，軸值歸零（中立）。
#define GP_WS_TIMEOUT_MS  500

// ---- 航點 ----
#define MAX_WAYPOINTS   50
#define WP_PER_CHUNK    14
