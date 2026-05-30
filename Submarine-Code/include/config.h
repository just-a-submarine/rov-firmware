#pragma once
#include <Arduino.h>

// =====================================================================
// 潛水艇 (ROV) 全域設定 — GOOUUU ESP32-S3-CAM N16R8（Freenove 接腳相容）
// 接腳與位址出處：doc/03、doc/05、doc/06
// =====================================================================

// --------------------- Wi-Fi / 網路 ---------------------
#define AP_SSID         "ROV_GS"          // 地面站 AP SSID（doc/04）
#define AP_PASS         "rov12345"
#define WIFI_CHANNEL    1
#define ROV_IP          192, 168, 4, 100  // ROV 固定 IP（手機 <img> 不失效）
#define ROV_GATEWAY     192, 168, 4, 1    // = 地面站
#define ROV_SUBNET      255, 255, 255, 0

// 地面站 AP MAC（doc/04 §三 ESP-NOW peer）。實機填入：16:C1:9F:29:EA:AC
static const uint8_t GS_AP_MAC[6] = {0x14, 0xC1, 0x9F, 0x29, 0xEA, 0xAD};  // 地面站改 ESP-IDF 後 AP MAC 改變，非舊 16:..:AC

// --------------------- I²C（共用匯流排） ---------------------
#define I2C_SDA_PIN     1                 // GPIO1（doc/03）
#define I2C_SCL_PIN     2                 // GPIO2
#define ADDR_QMC5883P   0x2C
#define ADDR_MS5837     0x76
#define ADDR_INA260     0x40
#define ADDR_MCP23017   0x20

// --------------------- 馬達 PWM（ESP32 直出，20kHz 10-bit） ---------------------
#define PWM_FREQ        20000
#define PWM_RES         10                // 0～1023
#define PWM_MAX         1023

#define PIN_LEFT_RPWM   14                // 左馬達（doc/03、doc/06）
#define PIN_LEFT_LPWM   21
#define PIN_RIGHT_RPWM  47                // 右馬達
#define PIN_RIGHT_LPWM  46                // 內部下拉，開機 LOW = 停
#define PIN_VERT_RPWM   41                // 垂直馬達（doc/06 §六的 44/46 為筆誤，正確為 41/42）
#define PIN_VERT_LPWM   42

// 左馬達裝「反槳」：要產生與右馬達相同方向的推力，電氣命令需反向。
// 1 = 反向（前進時左馬達自動反轉，修正左右推進方向不一致）。改裝正槳則設 0。
#define LEFT_MOTOR_INVERT   1

// --------------------- MCP23017 輸出腳（GPA0～GPA6，數位 EN / 繼電器） ---------------------
#define MCP_LEFT_R_EN   0                 // GPA0
#define MCP_LEFT_L_EN   1                 // GPA1
#define MCP_RIGHT_R_EN  2                 // GPA2
#define MCP_RIGHT_L_EN  3                 // GPA3
#define MCP_VERT_R_EN   4                 // GPA4
#define MCP_VERT_L_EN   5                 // GPA5
#define MCP_RELAY_LED   6                 // GPA6（12V LED 繼電器，高電位觸發）
#define MCP_EN_FIRST    0
#define MCP_EN_LAST     5                 // GPA0～5 為馬達 EN

// --------------------- GPS（UART1 / GPIO43/44） ---------------------
// ⚠ 本板 CH340 偵錯線（COM3）接在 GPIO43/44。啟用 GPS 後 43/44 改由 UART1 驅動，
//   CH340 偵錯失效 → 必須改用 S3「原生 USB」連線偵錯/燒錄（log 會自動導到原生 USB）。
//   GPS 走 UART1（非 UART0），不會再像舊版那樣重設主控台而開機崩潰。
#define ENABLE_GPS      1                 // 啟用 GPS（需改接 S3 原生 USB 偵錯）
#define GPS_RX_PIN      43                // ← Neo-M8N TX（doc/03）
#define GPS_TX_PIN      44                // → Neo-M8N RX
#define GPS_BAUD        9600

// --------------------- SD_MMC（1-bit，doc/05） ---------------------
// ESP32-S3 需顯式 setPins(CLK, CMD, D0)。doc/03 列 GPIO38/39/40。
// TODO(實機)：若掛載失敗，對調 CLK/CMD 並以序列輸出確認實際對應。
#define SD_CLK_PIN      39
#define SD_CMD_PIN      38
#define SD_D0_PIN       40

// --------------------- 相機（OV5640，Freenove ESP32-S3 接腳，doc/05） ---------------------
#define CAM_PIN_XCLK     15
#define CAM_PIN_SIOD     4
#define CAM_PIN_SIOC     5
#define CAM_PIN_D0       11
#define CAM_PIN_D1       9
#define CAM_PIN_D2       8
#define CAM_PIN_D3       10
#define CAM_PIN_D4       12
#define CAM_PIN_D5       18
#define CAM_PIN_D6       17
#define CAM_PIN_D7       16
#define CAM_PIN_VSYNC    6
#define CAM_PIN_HREF     7
#define CAM_PIN_PCLK     13
#define CAM_PIN_PWDN     -1
#define CAM_PIN_RESET    -1
// XCLK：20MHz 實測出現「垂直條紋」（FPC 在高速 PCLK 下訊號完整性差／位元錯誤，非掉幀）→
// 降回 10MHz 畫面乾淨。幀率較低但無條紋；想在 20MHz 跑乾淨需換更短/更好的排線或改善接觸/焊接。
#define CAM_XCLK_FREQ    10000000

// 相機畫面方向（模組實裝方向修正）：1=翻轉、0=不翻。
// 實機畫面上下顛倒 → VFLIP=1。若同時左右鏡像（文字反）再把 HFLIP 也設 1（＝整體旋轉 180°）。
#define CAM_VFLIP        1
#define CAM_HFLIP        0

// --------------------- 導航 / 控制常數（doc/06） ---------------------
#define WP_REACH_RADIUS_M   4.0f          // 航點到達半徑
#define WP_MIN_SPACING_M    5.0f          // 最小航點間距（GPS 誤差限制）
#define DEPTH_LED_THRESH_M  0.05f         // 深度 > 5cm 自動開燈
#define MAX_WAYPOINTS       50

// PID 深度控制（doc/06 §六起始值，TODO(實機)：實測微調）
#define DEPTH_KP            2.0f
#define DEPTH_KI            0.5f
#define DEPTH_KD            0.1f
#define DEPTH_SAMPLE_US     100000         // 100ms = 10Hz

// --------------------- 電量估算（3 串 18650，doc/03） ---------------------
// 18650 滿電 4.2V / 截止 ~3.0V，但放電曲線非線性（中段 ~3.7V 很平、兩端陡）。
// 故不用線性，改以「單顆 OCV → SoC」查表內插（sensors.cpp kCellCurve）。
// 另加兩道修正解決實測問題：
//   1) 內阻補償：OCV ≈ V_bus + |I|×R_int，抵銷馬達負載壓降（修「動作就掉」）。
//   2) 顯示值立即下降、僅能極慢回升（修「放開油門彈回 100%」）。
#define BATTERY_CELLS       3       // 串聯顆數（3S）
#define BATTERY_IR_OHM      0.15f   // 電池+線路等效內阻（粗估；實測 ΔV/ΔI 可校正）
#define BATTERY_RISE_PCT_S  0.2f    // 顯示電量每秒最多回升（防彈跳；換電池後緩慢校正）

// --------------------- 診斷 ---------------------
// 每 2s 由 networkTask 印 STA 狀態/RSSI/通道/IP/TX 功率（天線/弱訊號診斷）。確認後可設 0。
#define WIFI_DIAG       1

// --------------------- 任務頻率 ---------------------
#define CONTROL_HZ          100
#define TELEMETRY_HZ        5              // 5～10Hz（doc/04）
#define CONTROL_LOST_MS     1000           // 超過此時間沒收到控制封包 → 視為失聯保護

// --------------------- 單機測試模式（doc 計畫 §七） ---------------------
// 由 platformio.ini build_flags 開關。開啟時 ROV 自開 SoftAP，手邊無地面站也能測。
#ifdef STANDALONE_TEST
  #define STANDALONE_AP_SSID  "ROV_TEST"
  #define STANDALONE_AP_PASS  "rovtest123"
#endif
