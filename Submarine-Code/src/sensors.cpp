#include "sensors.h"
#include "config.h"
#include <Wire.h>
#include <Adafruit_QMC5883P.h>
#include <MS5837.h>
#include <Adafruit_INA260.h>
#include <TinyGPSPlus.h>

namespace {
Adafruit_QMC5883P compass;
MS5837            depthSensor;
Adafruit_INA260   ina260;
TinyGPSPlus       gps;

#ifdef ENABLE_GPS
// GPS 專用 UART1（避開 UART0 偵錯主控台，防止重設 UART0 當機）。
// 注意：43/44 此時改由 UART1 驅動，CH340 偵錯失效，須改用 S3 原生 USB 偵錯。
HardwareSerial gpsSerial(1);
#endif

bool g_compassOk = false;
bool g_depthOk   = false;
bool g_inaOk     = false;

// --- 羅盤校正參數（doc/06 §五）---
// 已實機校正：2026-05-31 板子攤平、水平轉滿一圈，由手機端 🧭 校準收 X/Y min/max 算出。
// （馬達動態補償 motorComp 仍為 TODO，待實測確認真有需要再建表。）
float g_offsetX = -0.237f, g_offsetY = -0.135f;   // 硬鐵偏移（圓心）
float g_scaleX  = 1.0484f, g_scaleY  = 0.9559f;   // 軟鐵比例（橢圓→圓）

// 單顆 18650 開路電壓(OCV) → 剩餘電量(%) 查表。放電曲線非線性，相鄰點線性內插即可。
// 數值取自典型 18650 靜置電壓對 SoC（中段平、兩端陡）。
struct OcvSoc { float v; float pct; };
const OcvSoc kCellCurve[] = {
    {4.20f, 100.f}, {4.10f, 90.f}, {4.00f, 80.f}, {3.93f, 70.f}, {3.87f, 60.f},
    {3.80f, 50.f},  {3.73f, 40.f}, {3.69f, 30.f}, {3.61f, 20.f}, {3.50f, 10.f},
    {3.27f, 5.f},   {3.00f, 0.f},
};

float cellOcvToPct(float v) {
    const int n = sizeof(kCellCurve) / sizeof(kCellCurve[0]);
    if (v >= kCellCurve[0].v)     return 100.0f;
    if (v <= kCellCurve[n - 1].v) return 0.0f;
    for (int i = 1; i < n; i++) {
        if (v >= kCellCurve[i].v) {
            const OcvSoc& hi = kCellCurve[i - 1];
            const OcvSoc& lo = kCellCurve[i];
            float t = (v - lo.v) / (hi.v - lo.v);
            return lo.pct + t * (hi.pct - lo.pct);
        }
    }
    return 0.0f;
}

// 3S 18650 電量估算（doc/03 / config.h）。
// 修正「動作時掉、放開彈回 100」的電壓垂降問題：
//   1) 內阻補償：OCV ≈ V_bus + |I| × R_int，抵銷馬達負載造成的壓降。
//   2) 由單顆 OCV 查放電曲線換算 SoC（比線性準）。
//   3) 顯示值立即下降、僅能極慢回升，避免油門放開時回彈。
uint8_t estimateBatteryPct(float busVoltage, float currentA) {
    float ocvPack = busVoltage + fabsf(currentA) * BATTERY_IR_OHM;  // 回推開路電壓
    float inst    = cellOcvToPct(ocvPack / BATTERY_CELLS);          // 每顆 OCV → SoC

    static bool     init   = false;
    static float    disp   = 100.0f;
    static uint32_t lastMs = 0;
    uint32_t now = millis();
    if (!init) { init = true; disp = inst; lastMs = now; }

    float dt = (now - lastMs) / 1000.0f;
    lastMs = now;
    if (inst < disp) {                                  // 立即下降（反映真實耗電）
        disp = inst;
    } else {                                            // 僅能緩慢回升（防彈跳）
        disp += fminf(inst - disp, BATTERY_RISE_PCT_S * dt);
    }
    return (uint8_t)constrain((int)(disp + 0.5f), 0, 100);
}
}  // namespace

bool setupSensors() {
    // QMC5883P 羅盤：連續量測模式
    g_compassOk = compass.begin(ADDR_QMC5883P, &Wire);
    if (g_compassOk) {
        compass.setMode(QMC5883P_MODE_CONTINUOUS);
        compass.setODR(QMC5883P_ODR_100HZ);
        compass.setOSR(QMC5883P_OSR_8);
        compass.setRange(QMC5883P_RANGE_8G);
    } else {
        log_e("QMC5883P 初始化失敗（0x%02X）", ADDR_QMC5883P);
    }

    // MS5837-02BA 水壓：本專案淡水、深度 1m 內
    depthSensor.setModel(MS5837::MS5837_02BA);
    g_depthOk = depthSensor.init();
    if (g_depthOk) {
        depthSensor.setFluidDensity(997.0f);   // 淡水
    } else {
        log_e("MS5837 初始化失敗（0x%02X）", ADDR_MS5837);
    }

    // INA260 電流計
    g_inaOk = ina260.begin(ADDR_INA260, &Wire);
    if (!g_inaOk) {
        log_e("INA260 初始化失敗（0x%02X，I2C 無回應）", ADDR_INA260);
    } else {
        // 初始化即讀一次，確認不只 ACK、暫存器也讀得到（USB 供電無電池時 V/I≈0 屬正常）。
        log_i("INA260 OK：V=%.3fV  I=%.1fmA  P=%.1fmW",
              ina260.readBusVoltage() / 1000.0f, ina260.readCurrent(), ina260.readPower());
    }

    // ⚠ 感測器狀態行必須在 GPS 接管 GPIO43/44（=CH340 console 腳）之前印，否則啟用 GPS 的板子
    //   永遠在 CH340 看不到它（GPS 一 begin 就切斷 UART0 console 並沖掉 FIFO，半行截斷）。
    log_i("感測器狀態：compass=%d depth=%d ina260=%d", g_compassOk, g_depthOk, g_inaOk);

    // GPS：UART1 @ GPIO43/44（見 config.h 對 CH340 腳位衝突的說明）
#ifdef ENABLE_GPS
    delay(120);   // 讓上面的狀態/診斷行排空到 CH340，再讓 UART1 接管 43/44
    gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    log_i("[sensors] GPS(UART1) 啟用 @43/44");
#else
    log_w("[sensors] GPS 停用（config.h ENABLE_GPS 未開）");
#endif

    return g_compassOk && g_depthOk && g_inaOk;
}

void gpsPoll() {
#ifdef ENABLE_GPS
    while (gpsSerial.available()) gps.encode(gpsSerial.read());
#endif
}

float getDepthM() {
    if (!g_depthOk) return 0.0f;
    depthSensor.read();
    return depthSensor.depth();
}

float getCorrectedHeading() {
    if (!g_compassOk) return 0.0f;
    float gx, gy, gz;
    if (!compass.getGaussField(&gx, &gy, &gz)) return 0.0f;

    // 硬鐵 + 軟鐵校正
    float x = (gx - g_offsetX) * g_scaleX;
    float y = (gy - g_offsetY) * g_scaleY;
    // TODO(實機)：馬達干擾補償（依當前 PWM 查表減偏移）

    float heading = atan2f(y, x) * 180.0f / PI;
    if (heading < 0) heading += 360.0f;
    return heading;
}

// 原始（未校正）磁場 X/Y（Gauss）。手機端校準要的就是「未扣 offset/scale」的值，
// 才能由 360° 旋轉的 min/max 反算硬鐵偏移與軟鐵比例。讀失敗回 false。
bool getMagRaw(float& x, float& y) {
    if (!g_compassOk) return false;
    float gx, gy, gz;
    if (!compass.getGaussField(&gx, &gy, &gz)) return false;
    x = gx;
    y = gy;
    return true;
}

void readAllSensors(TelemetrySnapshot& snap) {
    gpsPoll();

    snap.depthM     = getDepthM();
    snap.headingDeg = getCorrectedHeading();
    getMagRaw(snap.magX, snap.magY);   // 原始磁場（校準失敗則維持上次值，不致命）

    if (g_inaOk) {
        snap.currentA = ina260.readCurrent() / 1000.0f;     // mA → A
        snap.powerW   = ina260.readPower()   / 1000.0f;     // mW → W（INA260 內建 power 暫存器，實測 V×I）
        snap.batPct   = estimateBatteryPct(ina260.readBusVoltage() / 1000.0f, snap.currentA);  // mV → V
    }

    snap.gpsValid = gps.location.isValid();
    if (snap.gpsValid) {
        snap.lat = gps.location.lat();
        snap.lng = gps.location.lng();
    }
}
