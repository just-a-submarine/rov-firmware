#include "camera_stream.h"
#include "config.h"
#include "recorder.h"
#include "esp_camera.h"
#include "esp_http_server.h"
#include <SD_MMC.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {
constexpr size_t MAX_JPEG = 100 * 1024;     // SVGA JPEG 上限餘裕

// 最新影格共享緩衝（streamTask 生產，HTTP handler 消費）
uint8_t*          g_frameBuf = nullptr;
size_t            g_frameLen = 0;
volatile uint32_t g_frameSeq = 0;
SemaphoreHandle_t g_frameMutex = nullptr;

httpd_handle_t    g_httpd = nullptr;

// 相機設定保存供自癒重初始化（OV5640 無 RESET/PWDN 腳，偶發開機壞狀態需 deinit+reinit）
camera_config_t   g_camConfig = {};

// 相機是否已判定壞死（軟體 reinit 救不回；OV5640 無 RESET/PWDN 腳，需實體斷電才清得掉）。
// 判死後 streamTask 進入閒置，不再狂刷 fb_get/reinit；控制與遙測續行。
bool g_camDead = false;

// 套用畫面方向（模組實裝方向修正）。每次 init/reinit 後都要重設（sensor 重置會回預設）。
void applyCamOrientation(sensor_t* s) {
    if (!s) return;
    s->set_vflip(s, CAM_VFLIP ? 1 : 0);
    s->set_hmirror(s, CAM_HFLIP ? 1 : 0);
}

// 重新初始化相機（deinit→init）。回傳 true 表示 init 成功。
bool reinitCamera() {
    esp_camera_deinit();
    vTaskDelay(pdMS_TO_TICKS(150));
    esp_err_t e = esp_camera_init(&g_camConfig);
    vTaskDelay(pdMS_TO_TICKS(150));
    if (e != ESP_OK) { log_e("[CAM] reinit 失敗：0x%x", e); return false; }
    sensor_t* s = esp_camera_sensor_get();
    if (s) { s->set_framesize(s, FRAMESIZE_VGA); applyCamOrientation(s); }
    return true;
}

// 控制旗標（控制任務寫，streamTask 讀）
volatile uint8_t  g_streamMode = 0;          // 0/1
volatile bool     g_photoReq   = false;
volatile bool     g_photoAck   = false;
volatile int8_t   g_rssi       = 0;
volatile bool     g_streamActive = true;

// --- MJPEG 串流 handler（多段 multipart）---
const char* STREAM_CT = "multipart/x-mixed-replace;boundary=frame";
const char* PART_HDR  = "\r\n--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

esp_err_t streamHandler(httpd_req_t* req) {
    log_i("[HTTP] 收到 /stream 請求");
    // 相機已判死：立刻結束連線，讓手機端 <img> 觸發 error → 顯示「影像中斷，重連中」並每 3s 重連，
    // 不要握著連線空等（否則手機永遠卡在「影像連線中…」不動）。相機復原（實體斷電重開後）會自動接上。
    if (g_camDead) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "camera offline"); return ESP_FAIL; }
    if (httpd_resp_set_type(req, STREAM_CT) != ESP_OK) return ESP_FAIL;
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    uint8_t* sendBuf = (uint8_t*)ps_malloc(MAX_JPEG);
    if (!sendBuf) return ESP_FAIL;

    uint32_t lastSeq = 0;
    char part[80];
    esp_err_t res = ESP_OK;

    while (res == ESP_OK) {
        if (g_camDead) break;   // 串流中相機判死 → 收掉這條連線，手機改顯示「影像中斷」並重連
        if (g_frameSeq == lastSeq) { vTaskDelay(pdMS_TO_TICKS(5)); continue; }

        size_t len = 0;
        xSemaphoreTake(g_frameMutex, portMAX_DELAY);
        len = g_frameLen;
        if (len > 0 && len <= MAX_JPEG) memcpy(sendBuf, g_frameBuf, len);
        else len = 0;
        lastSeq = g_frameSeq;
        xSemaphoreGive(g_frameMutex);
        if (len == 0) { vTaskDelay(pdMS_TO_TICKS(5)); continue; }

        int hlen = snprintf(part, sizeof(part), PART_HDR, (unsigned)len);
        res = httpd_resp_send_chunk(req, part, hlen);
        if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char*)sendBuf, len);
    }

    free(sendBuf);
    return res;
}

esp_err_t rootHandler(httpd_req_t* req) {
    log_i("[HTTP] 收到 / 請求");
    const char* html =
        "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
        "<body style='margin:0;background:#000'>"
        "<img src='/stream' style='width:100%;height:auto'></body>";
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

void startHttpServer() {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 80;
    cfg.ctrl_port   = 32768;
    cfg.lru_purge_enable = true;
    // 影像降優先權（控制/遙測優先）：HTTPD 預設 task 優先級=5 且無核綁定（tskNO_AFFINITY）→
    // 會在 Core1 搶佔 controlTask(pri5 前為3)、在 Core0 搶佔 networkTask(遙測)。改釘 Core0 並降到
    // pri2，讓控制迴圈與 ESP-NOW 遙測永遠先於相機串流取得 CPU（doc/05 §排程優先級）。
    cfg.core_id       = 0;
    cfg.task_priority = 2;
    if (httpd_start(&g_httpd, &cfg) != ESP_OK) { log_e("httpd_start 失敗"); return; }

    httpd_uri_t stream = {"/stream", HTTP_GET, streamHandler, nullptr};
    httpd_uri_t root   = {"/",       HTTP_GET, rootHandler,   nullptr};
    httpd_register_uri_handler(g_httpd, &stream);
    httpd_register_uri_handler(g_httpd, &root);
    log_i("MJPEG HTTP server 已啟動（:80/stream）");
}

// RSSI 降級（doc/05 §四）：流暢優先版——固定 VGA 解析度，只隨訊號調 JPEG 壓縮率，
// 只有「極弱」才暫停。解析度不切換（避免每次切換相機重設造成卡頓），且 band 轉移加遲滯
// （降級早、升級需更強訊號），避免在門檻附近抖動反覆切換 → 消除「自己頓住又活過來」。
void applyQualityIfChanged() {
    static int band = 0;            // 0=一般 1=弱(更壓縮) 2=極弱(暫停)；皆維持 VGA
    int8_t rssi = g_rssi;
    int prev = band;
    switch (band) {
        case 0: if (rssi <= -70) band = 1;                         break;  // 一般→弱
        case 1: if (rssi >  -64) band = 0; else if (rssi <= -86) band = 2; break;  // 弱↔一般／弱→暫停
        case 2: if (rssi >  -82) band = 1;                         break;  // 暫停→弱（回升）
    }
    if (band == prev) return;

    sensor_t* s = esp_camera_sensor_get();
    if (!s) return;
    if      (band == 0) { s->set_quality(s, 10); g_streamActive = true; }   // 一般：VGA q10（畫質較好）
    else if (band == 1) { s->set_quality(s, 14); g_streamActive = true; }   // 弱：更壓縮省頻寬，仍不暫停
    else                { g_streamActive = false; }                          // 極弱(< -86dBm)：才暫停
}

// 即時拍照：直接把「目前串流的最新影格」寫入 SD（不切解析度、不暫停串流）→ 零延遲、不中斷畫面。
// 解析度＝目前串流尺寸（預設 VGA 640×480）。LB 在 GS 端已做邊緣觸發（按一下一張）。
void takePhotoInstant() {
    g_photoReq = false;
    if (!recorderIsCardReady()) return;

    static uint8_t* photoBuf = nullptr;
    if (!photoBuf) photoBuf = (uint8_t*)ps_malloc(MAX_JPEG);
    if (!photoBuf) return;

    size_t len = 0;
    xSemaphoreTake(g_frameMutex, portMAX_DELAY);
    len = g_frameLen;
    if (len > 0 && len <= MAX_JPEG) memcpy(photoBuf, g_frameBuf, len);
    else len = 0;
    xSemaphoreGive(g_frameMutex);
    if (len == 0) return;                  // 還沒有可存的影格

    char fn[40];
    snprintf(fn, sizeof(fn), "/photo_%lu.jpg", (unsigned long)millis());
    File f = SD_MMC.open(fn, FILE_WRITE);
    if (f) { f.write(photoBuf, len); f.close(); log_i("即時拍照 %s（%u bytes）", fn, (unsigned)len); }
    g_photoAck = true;
}

void streamTask(void*) {
    // 開機自癒：OV5640 偶發開機進壞狀態 → 零影格。先試抓一幀，抓不到就 reinit 重試 3 次。
    // （壞狀態下 fb_get 會 block 數秒才回 null；正常開機則立即拿到一幀直接跳出。）
    bool gotFrame = false;
    for (int attempt = 0; attempt < 3 && !gotFrame; ++attempt) {
        camera_fb_t* fb = esp_camera_fb_get();
        if (fb) { esp_camera_fb_return(fb); gotFrame = true; break; }
        log_w("[CAM] 開機取不到影格，重新初始化相機（第 %d 次）", attempt + 1);
        reinitCamera();
    }
    if (!gotFrame) {
        // 軟體 deinit+init 救不回：OV5640 無 RESET/PWDN 腳，SoC 重置（含燒錄/esp_restart）不會把
        // sensor 斷電，這顆已卡在壞狀態。判死 → 閒置不再狂刷；控制/遙測不受影響。
        g_camDead = true;
        log_e("[CAM] 開機相機壞死、軟體救不回 → 放棄相機、保留控制/遙測。"
              "請『實體斷電完整重開潛水艇一次』清除（治本：把 PWDN/RESET 接到空閒 GPIO）。");
    }

    // 執行期掉幀偵測（水下不重開機）：active 中 seq 連續 ~4s 沒推進 → reinit；連 3 次無效則判死閒置。
    uint32_t lastSeqMs = millis();
    uint32_t lastSeqVal = g_frameSeq;
    int      reinitStreak = 0;

    for (;;) {
        if (g_camDead) { vTaskDelay(pdMS_TO_TICKS(500)); continue; }  // 已判死：閒置，不狂刷 fb_get/reinit
        applyQualityIfChanged();

        if (g_frameSeq != lastSeqVal) { lastSeqVal = g_frameSeq; lastSeqMs = millis(); reinitStreak = 0; }
        else if (g_streamActive && !g_photoReq && millis() - lastSeqMs > 4000) {
            if (reinitStreak < 3) {
                log_w("[CAM] 執行期掉幀逾時，重新初始化相機（第 %d 次）", reinitStreak + 1);
                reinitCamera();
                reinitStreak++;
                lastSeqMs = millis();
            } else {
                g_camDead = true;
                log_e("[CAM] 執行期相機重初始化多次無效 → 放棄相機（需實體斷電重開）");
            }
        }

        // 拍照請求（僅模式 0）：即時存最新影格，不暫停串流、不切解析度
        if (g_photoReq && g_streamMode == 0) takePhotoInstant();

        // 模式切換 → 開/關錄影（所有 SD/recorder 操作集中在本任務避免競爭）
        if (g_streamMode == 1 && !isRecording())      startRecording(640, 480, 15);
        else if (g_streamMode == 0 && isRecording())  stopRecording();

        camera_fb_t* fb = esp_camera_fb_get();
#if defined(WIFI_DIAG) && WIFI_DIAG
        // [診斷] 統計影格取得情況：ok / null（沒擷取到）/ big（超過 MAX_JPEG 被丟）
        static uint32_t camLogMs = 0; static int nOk = 0, nNull = 0, nBig = 0;
        if (!fb) nNull++; else if (fb->len > MAX_JPEG) nBig++; else nOk++;
        if (millis() - camLogMs > 2000) {
            camLogMs = millis();
            log_i("[CAM] ok=%d null=%d big=%d seq=%lu active=%d lastLen=%u",
                  nOk, nNull, nBig, (unsigned long)g_frameSeq, (int)g_streamActive,
                  (unsigned)(fb ? fb->len : 0));
            nOk = nNull = nBig = 0;
        }
#endif
        if (!fb) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }

        // 發佈最新影格供串流（永遠擷取，串流暫停時僅不更新緩衝）
        if (g_streamActive && fb->len <= MAX_JPEG) {
            xSemaphoreTake(g_frameMutex, portMAX_DELAY);
            memcpy(g_frameBuf, fb->buf, fb->len);
            g_frameLen = fb->len;
            g_frameSeq++;
            xSemaphoreGive(g_frameMutex);
        }

        if (isRecording()) writeFrame(fb);

        esp_camera_fb_return(fb);
        vTaskDelay(1);
    }
}
}  // namespace

bool setupCamera() {
    g_frameMutex = xSemaphoreCreateMutex();
    g_frameBuf = (uint8_t*)ps_malloc(MAX_JPEG);
    if (!g_frameBuf) { log_e("影格緩衝 ps_malloc 失敗"); return false; }

    g_camConfig = {};
    g_camConfig.pin_xclk = CAM_PIN_XCLK; g_camConfig.pin_sccb_sda = CAM_PIN_SIOD;
    g_camConfig.pin_sccb_scl = CAM_PIN_SIOC;
    g_camConfig.pin_d0 = CAM_PIN_D0; g_camConfig.pin_d1 = CAM_PIN_D1; g_camConfig.pin_d2 = CAM_PIN_D2;
    g_camConfig.pin_d3 = CAM_PIN_D3; g_camConfig.pin_d4 = CAM_PIN_D4; g_camConfig.pin_d5 = CAM_PIN_D5;
    g_camConfig.pin_d6 = CAM_PIN_D6; g_camConfig.pin_d7 = CAM_PIN_D7;
    g_camConfig.pin_vsync = CAM_PIN_VSYNC; g_camConfig.pin_href = CAM_PIN_HREF; g_camConfig.pin_pclk = CAM_PIN_PCLK;
    g_camConfig.pin_pwdn = CAM_PIN_PWDN; g_camConfig.pin_reset = CAM_PIN_RESET;
    g_camConfig.xclk_freq_hz = CAM_XCLK_FREQ;
    g_camConfig.ledc_timer   = LEDC_TIMER_3;    // 避開馬達 LEDC（ledcAttach 佔用 ch0–5/timer0..）
    g_camConfig.ledc_channel = LEDC_CHANNEL_7;  // 相機 XCLK 用獨立 channel/timer，免與左馬達(ch0)衝突
    g_camConfig.pixel_format = PIXFORMAT_JPEG;
    g_camConfig.frame_size   = FRAMESIZE_VGA;   // 流暢優先：640×480（小幀、低延遲、弱訊號容錯）
    g_camConfig.jpeg_quality = 10;              // 畫質優先一點（數字小=畫質好；VGA 幀仍不大）
    g_camConfig.fb_count     = 3;               // 多一個緩衝平滑抖動（配 GRAB_LATEST 仍低延遲）
    g_camConfig.fb_location  = CAMERA_FB_IN_PSRAM;
    g_camConfig.grab_mode    = CAMERA_GRAB_LATEST;

    esp_err_t err = esp_camera_init(&g_camConfig);
    if (err != ESP_OK) { log_e("esp_camera_init 失敗：0x%x", err); return false; }

    sensor_t* s = esp_camera_sensor_get();
    if (s) { log_i("[CAM] 偵測到 sensor PID=0x%04x（OV5640=0x5640）", s->id.PID); applyCamOrientation(s); }
    else   log_e("[CAM] esp_camera_sensor_get 回 null");

    startHttpServer();
    return true;
}

void startCameraTask() {
    // streamTask 釘在 Core 0（Wi-Fi/網路核），8KB 堆疊
    xTaskCreatePinnedToCore(streamTask, "stream", 8192, nullptr, 1, nullptr, 0);
}

void cameraSetStreamMode(uint8_t mode) { g_streamMode = (mode == 1) ? 1 : 0; }
uint8_t cameraGetStreamMode()          { return g_streamMode; }
void cameraRequestPhoto()              { g_photoReq = true; }
bool cameraConsumePhotoAck()           { bool a = g_photoAck; g_photoAck = false; return a; }
void cameraSetRssi(int8_t rssi)        { g_rssi = rssi; }
uint32_t cameraFrameSeq()              { return g_frameSeq; }
bool     cameraStreamActive()          { return g_streamActive; }
