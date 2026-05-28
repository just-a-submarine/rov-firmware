#pragma once
#include <Arduino.h>

// 控制整合：套用控制封包→馬達/LED/相機，並承載兩個 FreeRTOS 任務（doc/06 §十）

void startControlTasks();     // 建立 controlTask（Core1）與 networkTask（Core0）
