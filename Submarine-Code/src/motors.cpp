#include "motors.h"
#include "config.h"
#include <Wire.h>
#include <Adafruit_MCP23X17.h>

namespace {
Adafruit_MCP23X17 mcp;
bool g_mcpReady = false;

const uint8_t kPwmPins[] = {
    PIN_LEFT_RPWM,  PIN_LEFT_LPWM,
    PIN_RIGHT_RPWM, PIN_RIGHT_LPWM,
    PIN_VERT_RPWM,  PIN_VERT_LPWM,
};
}  // namespace

bool setupMotors() {
    // 1. PWM：6 路 20kHz 10-bit，預設輸出 0
    for (uint8_t pin : kPwmPins) {
        ledcAttach(pin, PWM_FREQ, PWM_RES);
        ledcWrite(pin, 0);
    }

    // 2. MCP23017：GPA0～6 全設輸出 + LOW（EN 全關、繼電器不動作）
    g_mcpReady = mcp.begin_I2C(ADDR_MCP23017, &Wire);
    if (g_mcpReady) {
        for (int i = 0; i <= MCP_RELAY_LED; i++) {
            mcp.pinMode(i, OUTPUT);
            mcp.digitalWrite(i, LOW);
        }
    } else {
        log_e("MCP23017 初始化失敗（位址 0x%02X）", ADDR_MCP23017);
    }
    return g_mcpReady;
}

// speed >= 0：正轉走 RPWM；speed < 0：反轉走 LPWM（doc/06 §三）
void setMotor(uint8_t rpwmPin, uint8_t lpwmPin, int speed) {
    speed = constrain(speed, -PWM_MAX, PWM_MAX);
    if (speed >= 0) {
        ledcWrite(rpwmPin, speed);
        ledcWrite(lpwmPin, 0);
    } else {
        ledcWrite(rpwmPin, 0);
        ledcWrite(lpwmPin, -speed);
    }
}

void setLeftMotor(int speed)  { setMotor(PIN_LEFT_RPWM,  PIN_LEFT_LPWM,  speed); }
void setRightMotor(int speed) { setMotor(PIN_RIGHT_RPWM, PIN_RIGHT_LPWM, speed); }
void setVertMotor(int speed)  { setMotor(PIN_VERT_RPWM,  PIN_VERT_LPWM,  speed); }

void enableMotors() {
    if (!g_mcpReady) return;
    for (int i = MCP_EN_FIRST; i <= MCP_EN_LAST; i++) mcp.digitalWrite(i, HIGH);
}

void emergencyStop() {
    for (uint8_t pin : kPwmPins) ledcWrite(pin, 0);
    if (!g_mcpReady) return;
    for (int i = MCP_EN_FIRST; i <= MCP_EN_LAST; i++) mcp.digitalWrite(i, LOW);
}

void setLed(bool on) {
    if (!g_mcpReady) return;
    mcp.digitalWrite(MCP_RELAY_LED, on ? HIGH : LOW);
}
