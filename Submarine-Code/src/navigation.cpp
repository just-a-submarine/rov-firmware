#include "navigation.h"
#include "config.h"
#include "shared_state.h"
#include <QuickPID.h>
#include <math.h>

namespace {
// --- 深度 PID ---
float    g_depthInput = 0, g_depthOutput = 0, g_depthSetpoint = 0;
QuickPID depthPID(&g_depthInput, &g_depthOutput, &g_depthSetpoint);
bool     g_holdInit = false;
constexpr int RIGHT_STICK_DEADZONE = 60;     // ~6% of 1023

// --- 導航進度 ---
int g_currentWp = 0;

constexpr double EARTH_R = 6371000.0;        // 公尺

double toRad(double d) { return d * M_PI / 180.0; }

// haversine 距離（公尺）
double distanceM(double lat1, double lng1, double lat2, double lng2) {
    double dLat = toRad(lat2 - lat1), dLng = toRad(lng2 - lng1);
    double a = sin(dLat / 2) * sin(dLat / 2) +
               cos(toRad(lat1)) * cos(toRad(lat2)) * sin(dLng / 2) * sin(dLng / 2);
    return EARTH_R * 2 * atan2(sqrt(a), sqrt(1 - a));
}

// 初始方位角（度，0=北，順時針）
double bearingDeg(double lat1, double lng1, double lat2, double lng2) {
    double y = sin(toRad(lng2 - lng1)) * cos(toRad(lat2));
    double x = cos(toRad(lat1)) * sin(toRad(lat2)) -
               sin(toRad(lat1)) * cos(toRad(lat2)) * cos(toRad(lng2 - lng1));
    double b = atan2(y, x) * 180.0 / M_PI;
    if (b < 0) b += 360.0;
    return b;
}
}  // namespace

void setupDepthPID() {
    depthPID.SetTunings(DEPTH_KP, DEPTH_KI, DEPTH_KD);
    depthPID.SetOutputLimits(-PWM_MAX, PWM_MAX);
    depthPID.SetSampleTimeUs(DEPTH_SAMPLE_US);
    depthPID.SetMode(depthPID.Control::automatic);
}

void navResetProgress() { g_currentWp = 0; }

NavResult computeNavigation(double lat, double lng, float headingDeg, bool gpsValid) {
    NavResult r;
    Waypoint wps[MAX_WAYPOINTS];
    int n = getWaypoints(wps, MAX_WAYPOINTS);

    if (n == 0 || !gpsValid || g_currentWp >= n) return r;   // 不導航 → active=false

    // 吸引力：朝當前目標航點
    double tgtLat = wps[g_currentWp].lat, tgtLng = wps[g_currentWp].lng;
    double dist = distanceM(lat, lng, tgtLat, tgtLng);

    // 到達 → 切下一點
    if (dist < WP_REACH_RADIUS_M) {
        g_currentWp++;
        if (g_currentWp >= n) return r;     // 全部完成
        tgtLat = wps[g_currentWp].lat; tgtLng = wps[g_currentWp].lng;
        dist = distanceM(lat, lng, tgtLat, tgtLng);
    }

    double desired = bearingDeg(lat, lng, tgtLat, tgtLng);
    // TODO(實機)：Geofence 排斥力——目前未設邊界座標，先只用吸引力。
    //            邊界確定後在此將排斥向量與吸引向量相加，再轉成 desired heading。

    // 航向誤差 → [-180, 180]
    float err = desired - headingDeg;
    while (err > 180.0f)  err -= 360.0f;
    while (err < -180.0f) err += 360.0f;

    // 誤差越大轉越急、前進越慢（差速混控）
    float turn = constrain(err / 90.0f, -1.0f, 1.0f);          // ±90° 內線性
    float fwd  = (1.0f - fabsf(turn)) * 0.6f;                  // 對準才全力前進，最高 60% 推力
    float left  = fwd + turn;
    float right = fwd - turn;
    float peak  = fmaxf(fabsf(left), fabsf(right));
    if (peak > 1.0f) { left /= peak; right /= peak; }

    r.left   = (int)(left  * PWM_MAX);
    r.right  = (int)(right * PWM_MAX);
    r.wpIdx  = (uint8_t)g_currentWp;
    r.distM  = (float)dist;
    r.active = true;
    return r;
}

int computeVertMotor(float currentDepthM, int rightStickInput) {
    g_depthInput = currentDepthM;

    // 右搖桿有輸入 → 直接控制，並把當前深度設為保持點（放開後維持）
    if (abs(rightStickInput) > RIGHT_STICK_DEADZONE) {
        g_depthSetpoint = currentDepthM;
        g_holdInit = true;
        return constrain(rightStickInput, -PWM_MAX, PWM_MAX);
    }

    // 未達入水門檻（水面/空中）→ 不做深度保持：歸零並清 PID 狀態。
    // 在空氣中 PID 永遠達不到目標深度 → 積分捲繞(windup) → 垂直馬達會自己越轉越快
    // （桌面通電/放開搖桿/解除急停後尤明顯）。下次入水再重新捕捉保持點。
    if (currentDepthM < DEPTH_HOLD_MIN_M) {
        g_holdInit    = false;
        g_depthOutput = 0;
        depthPID.Reset();          // 清 pTerm/iTerm/dTerm/outputSum，杜絕 windup
        return 0;
    }

    // 已入水且放開搖桿 → PID 維持深度
    if (!g_holdInit) { g_depthSetpoint = currentDepthM; g_holdInit = true; }
    depthPID.Compute();
    return (int)g_depthOutput;
}
