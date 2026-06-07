# 任務：修復拍照／錄影問題（2026-06-07）

使用者回報三點：① 錄影 .avi 全黑 ② 照片/影片時間固定 1980-01-01 ③ 拍照延遲、要連按好幾下。

## 根因（已查證，非臆測）

- **A 錄影全黑**：`recorder.cpp` 的 idx1 偏移每筆 +4，全部指進 JPEG 長度欄而非 `00dc` 標記
  （host 重現 0/10 命中；修正後 10/10 命中）。header 設了 `AVIF_HASINDEX`，信任索引的播放器
  seek 到垃圾 → 全黑；ffmpeg/VLC 靠重掃才倖存。**照片正常＝同一份 fb JPEG bytes 有效 → 純容器問題，
  不需動畫格/DHT。**
- **B 1980**：ROV 系統時間從未設定（無 RTC/NTP，GPS 模組壞）→ FAT 起始紀元。
- **C 連按/延遲**：手機 25Hz（`controlTick` 40ms）取樣送出當下 bit，<40ms 的點擊在兩次 tick 間
  set→clear 不被取樣 → GS 收不到上升邊緣 → 不拍照。另 `takePhotoInstant` 即使 `SD_MMC.open` 失敗
  也設 `photoAck=true`（假「已存檔」提示）。

## 待辦

- [x] A1 修 `recorder.cpp` writeFrame：`frameOffset = position - (g_moviOffset + 8)`（對齊 'movi' FourCC）
- [x] A2 host 驗證：修正公式 → idx 10/10 命中 + ffmpeg 解出全部影格（buggy 0/10）
- [x] C1 手機 `app.js`：動作鍵（拍照/錄影/燈）加最短脈衝寬（PULSE_MS=120ms），保證 25Hz 上行必取樣到
- [x] C2 ROV `camera_stream.cpp` `takePhotoInstant`：只有實際寫檔成功才 `g_photoAck=true`（提示誠實）
- [x] B1 `packets.h`（兩端逐位元組一致）：ControlPacket 加 `uint32_t epochS`（12→16B）
- [x] B2 手機送 `Math.floor(Date.now()/1000)`；GS WS 解析 + 轉發；ROV 收到即設時鐘一次 + TZ=CST-8
- [x] 建置：ROV + GS 皆 SUCCESS；燒錄 ROV(COM8) + GS fw/fs(COM9) 皆 hash verified；GS 開機正常、ESP-NOW 遙測正常
- [x] 待使用者實測（第一輪）：✅ 換播放器後 .avi 可播、✅ 檔案時間正確；拍照仍要連按＋影片快轉 → 進第二輪

## 第二輪（實機回測：影片快轉 + 拍照要即時利索）

- [x] D 影片快轉＝宣告 15fps≠實測 fps → `recorder.cpp` 停止時用實測 fps 回補 avih/strh（host 驗證 0.67s→2.0s）
- [x] E 拍照改單調序號 `photoSeq`（取代 `bool takePhoto`，同 1 byte）：手機 +1 夾帶 `ph`→GS→ROV 序號變即拍；漏不掉/不合併
- [x] E2 手機 `flashShutter()` 即時快門閃（按下當下回饋、不等 ack）→「一按就拍」利索
- [x] 兩端建置 SUCCESS（ROV + GS）
- [x] 燒錄：ROV(COM8) + GS fw/fs(COM9) 三個一起燒、皆 hash verified；開機 `stations=1 haveTelem=1`、`wsCtrl=0` 不誤拍
- [⚠] 待使用者實測（第二輪）：錄片不再快轉（時長正常）；拍照按一下＝快門即閃一張、不必連按
- [x] 文件：CONTEXT.md（ROV/GS）、doc/05、tasks/lessons.md 同步

## Review
- 2026-06-07：順利更新並同步 Submarine-Code/doc/ 及 Electronics-2-Note/文件/ 內部的 05-影像與錄影系統.md 與 06-控制與導航.md。
- 2026-06-07：更新雙方的 CLAUDE.md、README.md 以對齊目前最新的相機自癒、虛擬搖桿與藍牙關閉等架構現狀。
- 2026-06-07：在專案根目錄新增 CONTEXT.md 交接文件，並更新 Note/CONTEXT.md 變更紀錄。
- 2026-06-07（拍照/錄影三修）：A 錄影全黑＝idx1 偏移 +4（host port 重現驗證 0/10→10/10）；
  C 連按＝手機 25Hz 漏短按（latch 120ms）＋ack 誠實化；B 1980＝手機 ts→epochS→ROV 設時鐘+TZ。
  ControlPacket 12→16B 兩端同步、ROV+GS 一起重燒（fw+fs hash verified）。關鍵領悟：照片正常＝畫格有效→
  黑純在容器、不需 DHT；ffmpeg 太寬容不能當嚴格播放器代理，改直接驗 idx 命中率。待使用者實機錄新 clip 確認。
