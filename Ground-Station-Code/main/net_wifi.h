#pragma once

// 啟動地面站純 AP 模式（192.168.4.1），關閉省電防漏包。
// 必須在 ESP-NOW 初始化【之前】呼叫（doc/04 §七）。
void setupWifi_GS();
