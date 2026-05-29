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

// 重新初始化相機（deinit→init）。回傳 true 表示 init 成功。
bool reinitCamera() {
    esp_camera_deinit();
    vTaskDelay(pdMS_TO_TICKS(150));
    esp_err_t e = esp_camera_init(&g_camConfig);
    vTaskDelay(pdMS_TO_TICKS(150));
    if (e != ESP_OK) { log_e("[CAM] reinit 失敗：0x%x", e); return false; }
    sensor_t* s = esp_camera_sensor_get();
    if (s) s->set_framesize(s, FRAMESIZE_SVGA);
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
    if (httpd_resp_set_type(req, STREAM_CT) != ESP_OK) return ESP_FAIL;
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    uint8_t* sendBuf = (uint8_t*)ps_malloc(MAX_JPEG);
    if (!sendBuf) return ESP_FAIL;

    uint32_t lastSeq = 0;
    char part[80];
    esp_err_t res = ESP_OK;

    while (res == ESP_OK) {
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
    if (httpd_start(&g_httpd, &cfg) != ESP_OK) { log_e("httpd_start 失敗"); return; }

    httpd_uri_t stream = {"/stream", HTTP_GET, streamHandler, nullptr};
    httpd_uri_t root   = {"/",       HTTP_GET, rootHandler,   nullptr};
    httpd_register_uri_handler(g_httpd, &stream);
    httpd_register_uri_handler(g_httpd, &root);
    log_i("MJPEG HTTP server 已啟動（:80/stream）");
}

// RSSI 降級（doc/05 §四）：僅在等級切換時套用，避免每影格重設
void applyQualityIfChanged() {
    static int lastBand = -2;
    int8_t rssi = g_rssi;
    int band = (rssi > -60) ? 0 : (rssi > -75 ? 1 : 2);
    if (band == lastBand) return;
    lastBand = band;

    sensor_t* s = esp_camera_sensor_get();
    if (!s) return;
    if (band == 0)      { s->set_framesize(s, FRAMESIZE_SVGA); s->set_quality(s, 10); g_streamActive = true; }
    else if (band == 1) { s->set_framesize(s, FRAMESIZE_VGA);  s->set_quality(s, 12); g_streamActive = true; }
    else                { g_streamActive = false; }   // 訊號極弱暫停串流
}

// 模式 0 拍照：暫停串流 → 切 720p → 存 SD → 還原（doc/05 §六）
void takePhotoAndSave() {
    if (!recorderIsCardReady()) { g_photoReq = false; return; }

    bool prevActive = g_streamActive;
    g_streamActive = false;
    vTaskDelay(pdMS_TO_TICKS(50));

    sensor_t* s = esp_camera_sensor_get();
    framesize_t prevSize = s ? s->status.framesize : FRAMESIZE_SVGA;
    if (s) s->set_framesize(s, FRAMESIZE_HD);     // 1280×720
    vTaskDelay(pdMS_TO_TICKS(100));               // 等感測器穩定

    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) {
        char fn[40];
        snprintf(fn, sizeof(fn), "/photo_%lu.jpg", (unsigned long)millis());
        File f = SD_MMC.open(fn, FILE_WRITE);
        if (f) { f.write(fb->buf, fb->len); f.close(); log_i("拍照存檔 %s", fn); }
        esp_camera_fb_return(fb);
    }

    if (s) s->set_framesize(s, prevSize);
    g_streamActive = prevActive;
    g_photoReq = false;
    g_photoAck = true;
}

void streamTask(void*) {
    // 開機自癒：OV5640 偶發開機進壞狀態 → 零影格。先試抓一幀，抓不到就 reinit 重試。
    // （fb_get 在壞狀態下會逾時回 null，每輪較久；正常開機則立即拿到一幀直接跳出。）
    for (int attempt = 0; attempt < 4; ++attempt) {
        camera_fb_t* fb = esp_camera_fb_get();
        if (fb) { esp_camera_fb_return(fb); break; }
        log_w("[CAM] 開機取不到影格，重新初始化相機（第 %d 次）", attempt + 1);
        reinitCamera();
    }

    // 執行期掉幀偵測（水下不能重開機）：active 中 seq 連續 ~4s 沒推進 → reinit
    uint32_t lastSeqMs = millis();
    uint32_t lastSeqVal = g_frameSeq;

    for (;;) {
        applyQualityIfChanged();

        if (g_frameSeq != lastSeqVal) { lastSeqVal = g_frameSeq; lastSeqMs = millis(); }
        else if (g_streamActive && !g_photoReq && millis() - lastSeqMs > 4000) {
            log_w("[CAM] 執行期掉幀逾時，重新初始化相機");
            reinitCamera();
            lastSeqMs = millis();
        }

        // 拍照請求（僅模式 0）優先處理
        if (g_photoReq && g_streamMode == 0) { takePhotoAndSave(); continue; }

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
    g_camConfig.frame_size   = FRAMESIZE_SVGA;  // 開機預設 800×600
    g_camConfig.jpeg_quality = 10;
    g_camConfig.fb_count     = 2;
    g_camConfig.fb_location  = CAMERA_FB_IN_PSRAM;
    g_camConfig.grab_mode    = CAMERA_GRAB_LATEST;

    esp_err_t err = esp_camera_init(&g_camConfig);
    if (err != ESP_OK) { log_e("esp_camera_init 失敗：0x%x", err); return false; }

    sensor_t* s = esp_camera_sensor_get();
    if (s) log_i("[CAM] 偵測到 sensor PID=0x%04x（OV5640=0x5640）", s->id.PID);
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
