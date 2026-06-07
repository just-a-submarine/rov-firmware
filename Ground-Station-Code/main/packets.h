#pragma once
#include <stdint.h>

// =============================================================================
//  ESP-NOW 封包定義（地面站 GS 與潛水艇 ROV 共用）
//
//  ⚠ 重要：這個檔案必須【一字不差】複製到 ROV 端專案。
//     兩邊 struct 的 layout 必須完全一致，ESP-NOW 才能正確解析。
//     任何欄位增刪、型別變更、順序調整都要兩邊同步。
//     全部使用 __attribute__((packed)) 避免編譯器自動對齊造成 layout 不一致。
//
//  封包上限：ESP-NOW 單包 250 bytes。
// =============================================================================

// 地面站 → ROV：控制指令（msgType = 0）
struct __attribute__((packed)) ControlPacket {
    int16_t  leftMotor;      // -1023 ～ +1023
    int16_t  rightMotor;
    int16_t  vertMotor;
    bool     ledOn;
    bool     emergencyStop;
    bool     autoMode;
    uint8_t  streamMode;     // 0 = 純串流，1 = 串流 + SD 錄影
    uint8_t  photoSeq;       // 單調拍照序號：序號一變 ROV 就拍一張（模式 0 有效）。取代舊 bool takePhoto，
                             //   免邊緣偵測→任何取樣率/丟包都漏不掉、連點不合併。
    uint32_t epochS;         // 手機 UTC 紀元秒（ROV 收到設一次系統時鐘；0=未提供）。解 SD 檔 1980 問題。
    uint8_t  msgType;        // 0 = 控制
};

// ROV → 地面站：遙測（msgType = 3）
struct __attribute__((packed)) TelemetryPacket {
    float    lat;
    float    lng;
    float    depthM;
    float    currentA;
    float    powerW;
    uint8_t  batPct;
    int8_t   rssi;
    uint8_t  streamMode;     // ROV 確認回傳
    uint8_t  navWaypointIdx; // 自動導航目標航點索引（0xFF = 未導航）
    float    navDistanceM;   // 距目標航點距離（公尺）
    float    headingDeg;     // 校正後羅盤航向 0～360°（前端 marker 朝向；手機羅盤校準也讀）
    float    magX;           // 原始（未校正）磁場 X，單位 Gauss — 供手機端自動校準收 min/max
    float    magY;           // 原始（未校正）磁場 Y，單位 Gauss
    bool     photoAck;       // 拍照完成確認
    bool     ledOn;          // 目前燈實際狀態（Y 鍵開或深度>5cm 自動開）
    uint8_t  msgType;        // 3 = 遙測
};

// 地面站 → ROV：航點分塊（msgType = 1）
// 每塊最多 14 個航點：14 × 17 bytes = 238 + 5 標頭 = 243 bytes < 250
struct __attribute__((packed)) WaypointChunk {
    uint8_t  msgType;        // 1 = 航點塊
    uint8_t  seqNum;
    uint8_t  totalChunks;
    uint8_t  chunkIndex;
    uint8_t  wpCount;
    struct __attribute__((packed)) {
        double  lat;
        double  lng;
        uint8_t order;
    } wps[14];
};

enum MsgType : uint8_t {
    MSG_CONTROL   = 0,
    MSG_WAYPOINT  = 1,
    MSG_ACK       = 2,
    MSG_TELEMETRY = 3,
};
