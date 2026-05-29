#pragma once
#include <Arduino.h>

// 馬達 PWM（ESP32 LEDC 直出）+ MCP23017 數位 EN / 繼電器控制（doc/03、doc/06）

bool setupMotors();          // PWM + MCP23017 初始化；MCP 失敗回 false
void setMotor(uint8_t rpwmPin, uint8_t lpwmPin, int speed);  // speed: -1023～+1023

void setLeftMotor(int speed);
void setRightMotor(int speed);
void setVertMotor(int speed);

void enableMotors();         // setup() 最後才呼叫，拉高 GPA0～5（doc/03 安全啟動）
void emergencyStop();        // 三馬達 PWM 歸零 + EN 斷開

void setLed(bool on);        // 繼電器控制 12V LED（GPA6，高電位觸發）
bool ledIsOn();              // 目前燈命令狀態（供遙測回傳）
