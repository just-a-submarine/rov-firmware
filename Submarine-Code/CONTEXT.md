# CONTEXT — 潛水艇 (ROV) ESP32-S3 韌體

開發交接文件。本專案只負責**潛水艇 (ROV)** 端；地面站為另一獨立專案（`../Ground-Station-Code/`）。

## 硬體
- 主板：GOOUUU ESP32-S3-CAM N16R8（16MB Flash / 8MB OPI PSRAM，OV5640，Freenove 接腳相容）。
- 規格來源（共用設計規格，位於專案根目錄）：`../doc/03`（電力接線）、`../doc/04`（通訊）、`../doc/05`（影像錄影）、`../doc/06`（控制導航）。

## 建置
- `platformio.ini` 使用 **pioarduino 平台**（Arduino-ESP32 **core 3.x**）。
  官方 espressif32 仍是 core 2.0.x，缺 `ledcAttach(pin)` / `esp_now_recv_info_t` 等本專案 API。
- 設定：16MB flash、`huge_app.csv`、`qio_opi`(OPI PSRAM)、`USB CDC On Boot`。
- 兩個環境：
  - `goouuu_esp32s3cam`：正式版（純 STA 連地面站）。
  - `standalone`：單機測試版（ROV 自開 AP `ROV_TEST`，無地面站可測相機/感測器）。
- 編譯：`pio run -e goouuu_esp32s3cam`（已驗證 SUCCESS）。
- 燒錄：`pio run -e <env> -t upload --upload-port <port>`。
  **COM 埠（2026-05-29 重新確認）**：CH340 偵錯線現為 **COM4**（舊紀錄 COM3），
  S3 原生 USB 為 **COM6**。`ENABLE_GPS=1` 下 43/44 由 GPS 佔用，CH340 偵錯失效，
  **正式版偵錯/燒錄一律用原生 USB COM6**。
- **本機 ROV STA MAC：`14:C1:9F:29:E0:B8`**（地面站 peer_addr 要填這個）。
- **地面站 AP MAC（已填入 `config.h` `GS_AP_MAC`）：`14:C1:9F:29:EA:AD`**
  （地面站改 ESP-IDF 後 AP MAC 變動，非舊 `16:..:AC`）。

## 程式結構（src/、include/）
- `config.h`：所有接腳、I²C 位址、`GS_AP_MAC`、常數、`STANDALONE_TEST` 開關。
- `packets.h`：ESP-NOW 封包格式（**須與地面站逐位元組一致**）。
- `shared_state.*`：跨任務共享狀態（mutex 保護，複製語意）。
- `motors.*`：LEDC PWM × 6 + MCP23017 EN/繼電器；開機 EN 全關，`enableMotors()` 最後呼叫。
- `sensors.*`：QMC5883P 羅盤、MS5837 深度、INA260 功耗、GPS(TinyGPS)、電量估算。
- `camera_stream.*`：OV5640 init + MJPEG 串流 + RSSI 降級 + 720p 拍照 + 串流/錄影同步。
- `recorder.*`：AVI/MJPEG SD 錄影（完整 RIFF 容器 + idx1，索引存 PSRAM）。
- `navigation.*`：Potential Field 水平導航 + QuickPID 深度控制。
- `control.*`：套用控制封包→馬達/LED/相機；`controlTask`(Core1)、`networkTask`(Core0)。
- `main.cpp`：初始化順序（`../doc/06` §十一）。

## FreeRTOS 任務
- `streamTask`(Core0)：唯一影格擷取者，發佈最新影格 + 錄影 + 拍照。
- `controlTask`(Core1, 100Hz)：讀感測器、套用控制、導航、PID。
- `networkTask`(Core0, 5Hz)：打包遙測經 ESP-NOW 送出、餵 RSSI 給串流降級。

## 與文件的差異（刻意）
> 設計規格在 `../doc/`（共用文件）；以下為實作刻意偏離規格之處。
1. **MJPEG 改用 ESP-IDF 內建 `esp_http_server`**（CameraWebServer 同款，最穩定），
   非 `../doc/05` 的 ESPAsyncWebServer（那是地面站 WebSocket 用）。ROV 不需 Async 庫。
2. **垂直馬達接腳用 41/42**：`../doc/06` §六深度 PID 原寫 `setMotor(44,46,...)` 是筆誤
   （44 是 GPS 腳），與 §三、§八矛盾；doc v2.7 已更正為 41/42。
3. QMC5883P 用 `getGaussField()`（Adafruit 庫實際 API），非 `../doc/06` 的 `getEvent()`。

## 實機踩坑（已解，務必知道）
1. **GPS 不可用 UART0（會與 CH340 偵錯主控台衝突 → 開機 abort）**。
   解法：GPS 改用 **UART1 @ 43/44**（`sensors.cpp` 的 `HardwareSerial gpsSerial(1)`），
   不碰 UART0 主控台，不再崩潰。`config.h` `ENABLE_GPS` 目前 **開啟**。
   代價：啟用後 43/44 由 GPS 佔用，**CH340(COM3) 偵錯失效**（log 只到 GPS 接手前），
   要完整偵錯需改接 **S3 原生 USB**。`main.cpp` 在 ENABLE_GPS 下呼叫 `Serial.setDebugOutput(true)`。
2. **網路必須先於相機 HTTP server**：`httpd_start` 需要 lwIP 已初始化，否則
   `socket()` 對未建立的 tcpip mutex 取鎖 assert。`main.cpp` 已把 `setupComms()` 排在
   `setupCamera()` 之前。
3. **燒錄常卡在連線**：VSCode PlatformIO 擴充套件會在 .ini/檔案變動時自動重跑 `pio run`，
   與 CLI 上傳搶 build 鎖/COM3。卡住時先清乾淨 pio 程序再上傳：
   PowerShell `Get-CimInstance Win32_Process -Filter "Name='python.exe'" | ? {$_.CommandLine -match 'pio.exe'} | % {Stop-Process $_.ProcessId -Force}`。
   （或直接關閉 VSCode 再用 CLI 燒錄。）
4. 用 pyserial 開 COM3 會觸發板子重置（DTR/RTS）；要不重置就建立 Serial() 後先設
   `dtr=False; rts=False` 再 open。

## 待實機填入 / 調整（程式內以 `// TODO(實機)`）
- `config.h` `GS_AP_MAC`：地面站 AP MAC。**未填前 ESP-NOW 控制/遙測不通**。
  （ROV 開機序列會印出自己的 STA MAC，給地面站 peer 用。）
- `sensors.cpp` 羅盤校正：`offsetX/Y`、`scaleX/Y`、馬達干擾補償表（需轉 360° 校正）。
- `navigation.cpp` PID 參數（起始 2.0/0.5/0.1）；Geofence 邊界座標（目前只用吸引力）。
- `config.h` SD 接腳 `SD_CLK/CMD/D0`(39/38/40)：若掛載失敗，對調 CLK/CMD 再試。

## 單機測試（手邊無地面站）
`platformio.ini` build_flags 取消 `-DSTANDALONE_TEST=1` 註解 → ROV 自開 SoftAP
（SSID `ROV_TEST`）。手機連上後瀏覽器開 `http://<rov-ip>/stream` 看即時影像、
序列觀察感測器數值。正式運行請關閉此旗標（純 STA 連地面站）。

## 操控 / 感測修正（2026-05-29，依手機實測回饋）
- **左馬達反槳**：`config.h LEFT_MOTOR_INVERT=1`，於 `motors.cpp setLeftMotor()` 反向電氣命令，
  使「正命令＝與右馬達同向推力」。同時涵蓋手動差速與自動導航（都走 setLeftMotor）。改正槳設 0。
- **急停可恢復**：`emergencyStop()` 會拉低 MCP 的 EN 腳；舊版解除後不再 `enableMotors()`，
  導致按一次急停後馬達永久不動。`control.cpp applyControl()` 改為偵測 estop→正常的轉換時
  重新 `enableMotors()`。地面站側 Start 改 toggle（再按一次解鎖）。
- **電量估算（3S 18650）**：改掉線性內插（會隨油門忽上忽下）。`sensors.cpp`：
  ① 內阻補償 `OCV≈V_bus+|I|×R_int`（`BATTERY_IR_OHM`）抵銷負載壓降；
  ② 由「單顆 OCV」查 18650 放電曲線 `kCellCurve` 換算 SoC（非線性，比線性準）；
  ③ 顯示值「立即下降、僅 `BATTERY_RISE_PCT_S`/s 緩升」防彈跳。
  電壓來源：INA260 `readBusVoltage()`（電流計即可量匯流排電壓）。
  功率：INA260 `readPower()` 暫存器（實測 V×I），非假設 12V。
- **STA 發射功率**：`comms.cpp` 加 `esp_wifi_set_max_tx_power(84)`（~20dBm）改善弱訊號。
- **WiFi 斷線自癒 watchdog**：`comms.cpp wifiReconnectWatchdog()`（networkTask 每圈呼叫）——
  STA 被 deauth/干擾後會卡死不自動重連（實測：地面站手把那波 deauth 後 ROV 一直 -127、需重置才好）。
  未關聯時每 5s `WiFi.disconnect()+begin()` 自動恢復。水下不能重置，此為必要。
- **✅ 相機「沒畫面」真因＝偶發開機壞狀態（已修，2026-05-30）**：先前誤判為硬體故障，實為韌體可修。
  - 相機**完全正常**：乾淨開機可連續穩定輸出 ~11fps 有效 JPEG（`[CAM] ok=23 seq 持續推進 lastLen≈21KB`，45s 不掉）。
  - 真因：**OV5640 偶發開機進壞狀態 → 整次開機零影格**（同一韌體，`pio upload` 那次重置死、`esptool hard_reset` 那次活）。
    根因是本板 **CAM_PIN_PWDN/RESET 都 = -1（未接）**，驅動無法硬體重置感測器，偶發卡死回不來。
    先前每次剛好都讀到「死掉的那次開機」（uptime 35–43s 全 `null`），才誤判成硬體/資料線無訊號。
  - **腳位確認 100% 正確**（對照 GOOUUU 實板 pinout 圖逐一比對）：XCLK15/SIOD4/SIOC5/VSYNC6/HREF7/PCLK13，
    D0..D7 = Y2..Y9 = GPIO 11,9,8,10,12,18,17,16。**非腳位問題**。
  - **修法（camera_stream.cpp）**：`streamTask` 開頭**開機自癒**——抓不到第一幀就 `reinitCamera()`（deinit+init）重試 3 次；
    執行期 `active` 中 seq 連續 ~4s 沒推進也 `reinitCamera()`（連 3 次無效則判死）。設定存於 `g_camConfig` 供重初始化。
  - XCLK 降至 **10MHz**（`config.h`）：~11fps 穩定、訊號餘裕較大；20/24MHz 之前的「失敗」其實是讀到死開機，非頻率問題。
  - **端到端已驗證**：GS→ROV `:80/stream` 探測 `HTTP/1.1 200 OK` + **收到 195397 bytes** 真實影像資料。
- **🚨 相機壞狀態的真正極限＝需「實體斷電」才清得掉（2026-05-30 補充）**：
  - 軟體 `reinitCamera()`（deinit+init）**救不回某些壞狀態**：log 反覆 `[CAM] gdma_disconnect: no peripheral connected` + `fb_get` block ~9s 回 null。
  - 根因＝**PWDN/RESET 未接（-1）→ 任何 SoC 重置（含 `pio upload`、`esptool` reset、`esp_restart`）都不會把 sensor 斷電**，
    所以 sensor 一旦卡死，軟重置/重開機都沒用，**只有拔插電源完整斷電**才會回復（先前能用 81 分鐘就是某次 power-on 抽到好狀態）。
  - ⚠ **開序列埠監看會 SoC-reset 本板**（ESP32-S3 native USB CDC 開埠觸發 core auto-reset）→ 每次監看都重置、且不斷電，
    所以「用序列埠盯相機」永遠看到壞的；先前誤以為「板子自己重開」其實是開埠造成。
  - **決策：不要用 `esp_restart` 自癒**（對 sensor-wedge 徒勞，且反覆中斷控制）。改為**判死 → `g_camDead` 閒置**：
    開機重試 3 次救不回（或執行期 reinit 3 次無效）就放棄相機、streamTask 閒置不再狂刷，**控制/遙測完全不受影響**。
    已驗證：判死後無自我重開、WifiDiag 每 2s 照常 CONNECTED。**使用者修法：實體斷電重開潛水艇一次**。
    **治本（硬體）**：把相機 FPC 的 PWDN 或 RESET 焊一條線到空閒 GPIO，填入 `CAM_PIN_PWDN/RESET`，驅動才能硬體重置 sensor。
- **🔧 精確診斷：症狀指向 FPC 排線/模組接觸（2026-05-30 再補）**：COM6 即時 log＝`偵測到 sensor PID=0x5640`（SCCB/電源 OK）
  但 `fb_get` block 8s 回 null + `gdma_disconnect`（D0–D7/PCLK/VSYNC/HREF 無資料、DMA 收不到像素）。`config.h:88-91` XCLK
  已從 20/24MHz **降到 10MHz**，10MHz 下仍零影格 → 依該註解判斷邏輯＝**排線接觸不良／模組實體斷路**（非韌體）。
  **第一優先：重插相機 FPC、壓緊連接器卡扣、確認金手指方向**；其次換排線/模組；單純斷電重開不一定穩定（間歇接觸）。
- **串流判死立即收連線（2026-05-30）**：`streamHandler` 開頭 `if (g_camDead) httpd_resp_send_err(500)`、迴圈內 `if (g_camDead) break`
  → 相機死時手機 `<img>` 觸發 error 顯示「影像中斷，重連中」並每 3s 重連，**不再永遠卡「影像連線中…」**；相機復原後自動接上。
- **燈狀態進遙測（2026-05-30）**：`motors.cpp` 記錄 `setLed` 命令值、`ledIsOn()` 取出；`networkTask` 填 `TelemetryPacket.ledOn`
  （兩端 packets.h 同步加），GS `web_server.cpp` 轉 JSON `led` → 手機狀態列「燈 開/關」。
- **LB 即時拍照（2026-05-30）**：原 `takePhotoAndSave` 會切 720p+暫停串流+等 150ms（慢又頓）→ 改 **`takePhotoInstant`**：
  直接把目前串流最新影格（SVGA）寫 SD，**零延遲、不中斷畫面**。GS 端 LB 已邊緣觸發（按一下一張）。
  - 診斷工具（已留）：`WIFI_DIAG` 下 streamTask 每 2s 印 `[CAM] ok/null/big/seq`；
    遙測借 `navDistanceM` 回傳影格數（手動模式），GS COM4 DIAG 印 `cam(影格/-1停用)`。
- **天線診斷**：`WIFI_DIAG`（config.h，預設 1）每 2s 印 STA 狀態/RSSI/通道/IP/TX。
- **相機 XCLK LEDC**：改用 `LEDC_CHANNEL_7/TIMER_3`，避開馬達 `ledcAttach` 佔用的 ch0–5（原本相機用 ch0 與左馬達衝突）。
  ⚠ 原生 USB(COM6) `pio upload` 後常卡下載模式靜默 → 改從 GS COM4 看 ROV 自量 RSSI 較可靠
  （或 `python -m esptool --port COM6 --after hard_reset run` 讓它跑）。
  「訊號」遙測＝潛水艇 STA 看 GS AP 的 `WiFi.RSSI()`；`-127`＝當下未關聯（ESP-NOW 免關聯仍通）。
  弱訊號根因疑為板上「板載/IPEX 天線二選一」0Ω 電阻未切對 → 待實機檢查。

## 驗證進度
- [x] 編譯通過（core 3.x / pioarduino），正式版 + 單機版
- [x] 實機燒錄、開機完整跑完不崩潰（COM3）
- [x] 感測器自檢：羅盤/深度/電流 全部 OK（compass=1 depth=1 ina260=1）
- [x] SD 掛載成功、相機 init + MJPEG server 啟動成功
- [x] 單機模式 SoftAP `ROV_TEST` @192.168.4.1 啟動
- [x] 單機模式 HTTP server 實測：手機連 AP 後 `/` 與 `/stream` 請求均到達並回應（韌體層 OK）
- [x] 相機影像鏈路端到端驗證（2026-05-30）：相機 ~11fps 穩定、GS→ROV `:80/stream` 收到 195397 bytes；加開機/執行期自癒
- [x] **相機實機目視 OK（2026-05-30）**：使用者重插 FPC 排線後手機有畫面（真因＝排線接觸，非韌體）。
  畫面上下顛倒 → 加 `CAM_VFLIP=1`（config.h）+ `applyCamOrientation()`（setup/reinit 都套用），燒錄後 `ok=12 seq` 穩定推進。
  ⚠ 若左右也鏡像（文字反）→ 改 `CAM_HFLIP=1`（＝整體 180°）。
- [x] **LB 拍照 / RB 錄影鏈路查證（2026-05-30）**：兩端按鍵 bit 一致（LB=bit4/RB=bit5/Start=bit6）。
  LB：邊緣觸發**僅 streamMode 0** → `takePhoto` → ROV `cameraRequestPhoto`→`takePhotoInstant` 寫 `/photo_*.jpg` → `photoAck`
  回遙測 → 手機閃「已拍照」toast。RB：邊緣 toggle streamMode → ROV `startRecording`/`stopRecording` 寫 `/rec_*.avi`(MJPG AVI+idx1)；
  badge 變紅本身即證明往返成功。SD 開機掛載成功（無「未就緒」警告）→ 兩者都實際寫檔。
  ⚠ 注意：**錄影模式(RB 紅)下 LB 不會拍照**（拍照僅純串流模式）。
- [x] GPS 版（UART1@43/44）燒錄開機正常、無崩潰循環
- [ ] GPS 實測：接模組 + 天空視野取得定位，遙測帶出 lat/lng（需原生 USB 偵錯）
- [ ] 馬達（離水、低工率）與緊急停車
- [ ] 錄影 .avi 可播放性
- [ ] 與地面站整合（ESP-NOW 遙控、遙測、航點導航）

## 正式版（STA）連線測試結果（2026-05-29，COM6）
韌體側全數正常，唯一阻塞點是**地面站未開機**：
- 正式版（`goouuu_esp32s3cam`，含 `GS_AP_MAC=14:C1:9F:29:EA:AD`）編譯/燒錄/開機皆 SUCCESS，
  跑到 `=== 初始化完成 ===`、MJPEG server `:80/stream` 啟動，**無崩潰**。
- WiFi STA 啟動正常但 `Reason: 201 NO_AP_FOUND`（找不到 `ROV_GS`）→ 背景持續重連。
- 連帶 `onDataSent` ESP-NOW 送出失敗（peer 不可達，已 2s 節流告警）。
- **判讀**：ROV 端無問題；待整合只需把地面站開機廣播 `ROV_GS`（ch1），
  ROV 會自動關聯取得 192.168.4.100、ESP-NOW 隨頻道接通。屬硬體待辦，非程式缺陷。
