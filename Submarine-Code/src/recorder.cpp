#include "recorder.h"
#include "config.h"
#include <SD_MMC.h>

namespace {
File     g_avi;
bool     g_recording = false;
bool     g_cardReady = false;
uint32_t g_frameCount = 0;
uint32_t g_moviOffset = 0;        // 指向 movi 的 "LIST" tag（header 結尾）
uint32_t g_recStartMs = 0;        // 錄影起始 millis（停止時算實測 fps，回補進 header 防快轉）

constexpr uint32_t MAX_IDX_FRAMES = 30000;   // 15fps×30min≈27000（doc/05）
struct AviIdx { uint32_t offset; uint32_t size; };
AviIdx*  g_idx = nullptr;

// header 內待回補欄位的絕對位元組偏移（layout 見下方 writeAviHeader 註解）
constexpr uint32_t OFF_RIFF_SIZE   = 4;
constexpr uint32_t OFF_USPERFRAME  = 32;     // avih dwMicroSecPerFrame
constexpr uint32_t OFF_TOTALFRAMES = 48;
constexpr uint32_t OFF_STRH_SCALE  = 128;    // strh dwScale（dwRate 緊接 @132）
constexpr uint32_t OFF_STRH_LENGTH = 140;

void wr32(uint32_t v) { g_avi.write((uint8_t*)&v, 4); }
void wr16(uint16_t v) { g_avi.write((uint8_t*)&v, 2); }
void tag(const char* t) { g_avi.write((const uint8_t*)t, 4); }

// 寫入固定 212 bytes 的 RIFF/AVI 標頭（avih + strl/strh/strf）
void writeAviHeader(int w, int h, int fps) {
    uint32_t usPerFrame = 1000000UL / (fps > 0 ? fps : 15);

    tag("RIFF"); wr32(0); tag("AVI ");          // 0  RIFF / size(回補) / AVI
    tag("LIST"); wr32(192); tag("hdrl");        // 12 LIST hdrl（內容 192 bytes）

    tag("avih"); wr32(56);                      // 24 avih
    wr32(usPerFrame);   // dwMicroSecPerFrame      32
    wr32(0);            // dwMaxBytesPerSec         36
    wr32(0);            // dwPaddingGranularity     40
    wr32(0x10);         // dwFlags = AVIF_HASINDEX  44
    wr32(0);            // dwTotalFrames (回補)     48
    wr32(0);            // dwInitialFrames          52
    wr32(1);            // dwStreams                56
    wr32(0);            // dwSuggestedBufferSize    60
    wr32(w);            // dwWidth                  64
    wr32(h);            // dwHeight                 68
    wr32(0); wr32(0); wr32(0); wr32(0);  // dwReserved[4]   72

    tag("LIST"); wr32(116); tag("strl");        // 88 LIST strl（內容 116 bytes）
    tag("strh"); wr32(56);                      // 100 strh
    tag("vids");        // fccType               108
    tag("MJPG");        // fccHandler            112
    wr32(0);            // dwFlags               116
    wr16(0); wr16(0);   // wPriority/wLanguage   120
    wr32(0);            // dwInitialFrames       124
    wr32(1);            // dwScale               128
    wr32(fps);          // dwRate                132
    wr32(0);            // dwStart               136
    wr32(0);            // dwLength (回補)        140
    wr32(0);            // dwSuggestedBufferSize 144
    wr32(0xFFFFFFFF);   // dwQuality             148
    wr32(0);            // dwSampleSize          152
    wr16(0); wr16(0); wr16(w); wr16(h);  // rcFrame  156

    tag("strf"); wr32(40);                      // 164 strf (BITMAPINFOHEADER)
    wr32(40);           // biSize                172
    wr32(w);            // biWidth               176
    wr32(h);            // biHeight              180
    wr16(1); wr16(24);  // biPlanes/biBitCount   184
    tag("MJPG");        // biCompression         188
    wr32(w * h * 3);    // biSizeImage           192
    wr32(0); wr32(0);   // biXPelsPerMeter/Y     196
    wr32(0); wr32(0);   // biClrUsed/Important   204
    // 結束於 offset 212
}

void writeIdx1() {
    uint32_t n = (g_frameCount > MAX_IDX_FRAMES) ? MAX_IDX_FRAMES : g_frameCount;
    tag("idx1");
    wr32(n * 16);
    for (uint32_t i = 0; i < n; i++) {
        tag("00dc");
        wr32(0x10);              // AVIIF_KEYFRAME
        wr32(g_idx[i].offset);
        wr32(g_idx[i].size);
    }
}
}  // namespace

bool recorderBeginSD() {
    SD_MMC.setPins(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN);
    g_cardReady = SD_MMC.begin("/sdcard", true);   // true = 1-bit 模式（doc/05）
    if (!g_cardReady) log_e("SD_MMC 掛載失敗（檢查 CLK/CMD/D0 接腳）");
    return g_cardReady;
}

bool recorderIsCardReady() { return g_cardReady; }
bool isRecording()         { return g_recording; }

bool startRecording(int width, int height, int fps) {
    if (g_recording || !g_cardReady) return false;

    if (!g_idx) {
        g_idx = (AviIdx*)ps_malloc(sizeof(AviIdx) * MAX_IDX_FRAMES);
        if (!g_idx) { log_e("idx 緩衝 ps_malloc 失敗"); return false; }
    }

    char filename[40];
    snprintf(filename, sizeof(filename), "/rec_%lu.avi", (unsigned long)millis());
    g_avi = SD_MMC.open(filename, FILE_WRITE);
    if (!g_avi) { log_e("無法建立錄影檔 %s", filename); return false; }

    g_frameCount = 0;
    writeAviHeader(width, height, fps);

    g_moviOffset = g_avi.position();      // = 212
    tag("LIST"); wr32(0); tag("movi");    // movi 大小先填 0，結束回補

    g_recStartMs = millis();
    g_recording = true;
    log_i("開始錄影：%s（%dx%d @%dfps）", filename, width, height, fps);
    return true;
}

void writeFrame(camera_fb_t* fb) {
    if (!g_recording || !g_avi || !fb) return;

    // idx1 的 dwChunkOffset：播放器以 (movi FourCC 位置 + offset) 來 seek，必須正好落在本影格的
    // "00dc" 標記。movi FourCC 在 g_moviOffset+8（"LIST"4 + size4 之後），故 offset 相對於它。
    // 舊版寫成 position-g_moviOffset-4，每筆多 +4 → 全部指進 JPEG 長度欄；header 又設了
    // AVIF_HASINDEX → 信任索引的播放器 seek 到垃圾 → 整支影片全黑（ffmpeg/VLC 靠重掃才倖存）。
    uint32_t frameOffset = g_avi.position() - (g_moviOffset + 8);

    tag("00dc");
    wr32(fb->len);
    g_avi.write(fb->buf, fb->len);
    if (fb->len & 1) g_avi.write((uint8_t)0);   // RIFF 偶數對齊 padding

    if (g_frameCount < MAX_IDX_FRAMES) {
        g_idx[g_frameCount].offset = frameOffset;
        g_idx[g_frameCount].size   = fb->len;
    }
    g_frameCount++;
}

void stopRecording() {
    if (!g_recording || !g_avi) { g_recording = false; return; }

    // 回補 movi LIST 大小
    uint32_t moviSize = g_avi.position() - g_moviOffset - 8;
    g_avi.seek(g_moviOffset + 4);
    wr32(moviSize);
    g_avi.seek(0, SeekEnd);

    writeIdx1();

    // 回補 RIFF 總大小、總影格數、串流長度
    uint32_t fileSize = g_avi.position() - 8;
    g_avi.seek(OFF_RIFF_SIZE);   wr32(fileSize);
    g_avi.seek(OFF_TOTALFRAMES); wr32(g_frameCount);
    g_avi.seek(OFF_STRH_LENGTH); wr32(g_frameCount);

    // 回補「實測 fps」防快轉：header 原本寫死宣告 15fps，但實際擷取率受 SD 寫入 + streamTask 最低優先
    // 壓到比 15 低 → 同樣張數標成 15fps 會播太快（快轉）。用整段實測時間算真 fps 蓋回 avih/strh，
    // 播放器即以真實時間播放。dwScale=毫秒、dwRate=影格數×1000 → fps=frames/秒，精確不四捨。
    uint32_t elapsedMs = millis() - g_recStartMs;
    if (g_frameCount > 0 && elapsedMs > 100) {
        uint32_t usPerFrame = (uint32_t)((uint64_t)elapsedMs * 1000ULL / g_frameCount);
        g_avi.seek(OFF_USPERFRAME); wr32(usPerFrame);
        g_avi.seek(OFF_STRH_SCALE); wr32(elapsedMs); wr32(g_frameCount * 1000UL);  // dwScale, dwRate 連寫
        log_i("錄影結束：%lu 影格 / %lu ms ＝ 實測 %.1f fps（已回補防快轉）",
              (unsigned long)g_frameCount, (unsigned long)elapsedMs,
              g_frameCount * 1000.0 / elapsedMs);
    } else {
        log_i("錄影結束：%lu 影格（時間太短，fps 維持宣告值）", (unsigned long)g_frameCount);
    }

    g_avi.close();
    g_recording = false;
}
