#pragma once
#include <Arduino.h>
#include "esp_camera.h"

// SD 卡 AVI/MJPEG 錄影（doc/05 §七）。完整 RIFF/AVI 容器，可被一般播放器解析。

bool recorderBeginSD();              // SD_MMC 掛載（1-bit 模式）
bool recorderIsCardReady();

bool startRecording(int width, int height, int fps);
void writeFrame(camera_fb_t* fb);    // 每個 JPEG 影格寫入 movi
void stopRecording();
bool isRecording();
