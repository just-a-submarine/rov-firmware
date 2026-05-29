#pragma once
#include <Arduino.h>

// OV5640 相機 + MJPEG HTTP 串流 + RSSI 降級 + 720p 拍照 + 串流/錄影同步（doc/05）

bool setupCamera();                  // 相機 init + 啟動 HTTP server
void startCameraTask();              // 建立 streamTask（Core 0，唯一影格擷取者）

void    cameraSetStreamMode(uint8_t mode);   // 0=純串流 / 1=串流+錄影（控制套用）
uint8_t cameraGetStreamMode();
void    cameraRequestPhoto();        // 觸發單張 720p 拍照（模式 0 才有效）
bool    cameraConsumePhotoAck();     // 取出並清除拍照完成旗標（遙測用）
void    cameraSetRssi(int8_t rssi);  // 提供 RSSI 給 streamTask 做動態降級
uint32_t cameraFrameSeq();           // 已發佈影格序號（診斷：是否有產生影格）
bool     cameraStreamActive();       // 串流是否啟用（診斷）
