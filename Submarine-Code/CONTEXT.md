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
- ~~`sensors.cpp` 羅盤校正~~ **✅ 已校正（2026-05-31）**：板攤平、手機「🧭 校準」轉滿一圈算得
  `g_offsetX/Y=-0.237/-0.135`、`g_scaleX/Y=1.0484/0.9559`，已貼回 `sensors.cpp`、編譯 SUCCESS、已燒 COM6。
- 羅盤兩個未實作項（見 `doc/06 §五`）：
  - `HEADING_OFFSET_DEG` 單一常數＝**裝設水平旋轉角＋磁偏角**（台北西偏 ~4.5°）；在 `getCorrectedHeading()` 末端加上、對 360° 取模。
  - `motorComp`（馬達動態干擾補償）：**先測再做**——艇固定比較「馬達關 vs 滿油門」航向漂多少，漂小不必補、能拉開距離優先；
    要做就艇固定、左右馬達各檔正反推、記 `magX/Y` 變化得 `kX/kY` 小表，runtime 先扣再算。
  - 擺設：**水平旋轉角免對準艇首**（固定偏移、常數補）；**傾斜必須裝平**（無 IMU 補不了）；**離馬達/電流線越遠越好**。
- `navigation.cpp` PID 參數（起始 2.0/0.5/0.1）；Geofence 邊界座標（目前只用吸引力）。
- `config.h` SD 接腳 `SD_CLK/CMD/D0`(39/38/40)：若掛載失敗，對調 CLK/CMD 再試。

## 單機測試（手邊無地面站）
`platformio.ini` build_flags 取消 `-DSTANDALONE_TEST=1` 註解 → ROV 自開 SoftAP
（SSID `ROV_TEST`）。手機連上後瀏覽器開 `http://<rov-ip>/stream` 看即時影像、
序列觀察感測器數值。正式運行請關閉此旗標（純 STA 連地面站）。

## 操控 / 感測修正（2026-05-29，依手機實測回饋）
- **✅ 影像「持續中斷」真因＝OV5640 非決定性冷啟動 wedge（與訊號無關，已加自癒，2026-06-06）**：
  換新 GS 板把 RF 修好後（rssi −36/−27、stations/haveTelem 穩），影像仍持續中斷。**非 RF**：遙測（ESP-NOW、connectionless）
  與影像（MJPEG/TCP，經 GS-AP 轉發＝雙跳 airtime）是兩條不同路徑。
  - **非侵入定位**：GS COM9 的 DIAG 已含 `cam(影格)=cameraFrameSeq` ＋對 `192.168.4.100/stream` 探測。實測 `cam` 硬凍住
    （0/51/72/0 後不動）、`/stream` 回 **HTTP 500**（＝`g_camDead`）→ 相機開機後跑幾秒就 wedge，**不必動到開埠會 reset 的 ROV COM8** 即可判定是相機。
  - **根因**：`CAM_PIN_PWDN/RESET=-1`（Freenove 腳位未拉到 GPIO）→ sensor 冷啟動非決定性；疊加 FPC 接觸邊際。同硬體每次結果不同＝
    需要的 reinit 次數在擲骰子。LEDC timer 已讀碼排除（相機 ch7/timer3 vs 馬達 ch0–5/timer0–2 不撞）。
  - **修法（camera_stream.cpp）**：把「開機/執行期試 3 次就**永久判死閒置**」改成**持續背景重試自癒**——`g_camDead` 期間反覆
    `reinitCamera()`（退避 1→5s），相機一吐幀就清旗標續傳，**不必拔電**。
  - **已驗證（燒 COM8）**：冷啟動 wedge（`cam` 凍 84）**自動恢復**、`cam` 爬到 800+ 不再凍；`haveTelem=1`、控制/遙測全程不受影響。
    治本仍是硬體（FPC 接觸／RESET 接 GPIO）。
- **🎚 相機調參：整包覆蓋失敗→還原→極簡提亮（2026-06-07）**：
  - 曾加 `applyCamTuning()` 整包覆蓋（~18 個 setter）想更清晰，結果**比原廠預設更暗**；接連調
    `gainceiling` 4X→16X→32X、`ae_level`+2、`brightness`+2 都拉不回。**根因＝手動覆蓋 OV5640 原廠自動值這條路本身錯了**
    （esp32-camera 多個 setter 互相干擾、淨效果變暗），不是某個數值。先**整段移除還原為 git HEAD 原始狀態**，使用者確認「原始很好」。
  - 之後使用者要「再稍微亮、暗處還是有點暗、怕亮的太亮」→ **改成極簡版只動兩旋鈕**（其餘曝光/增益/白平衡仍全交原廠自動）：
    `set_brightness` + `set_contrast(s, -1)`（**降對比＝抬暗部、壓亮部**，正好解「暗處太暗」又避免「亮的太亮」）。
    setup 與自癒 `reinitCamera` 都套用（sensor reset 會回預設，須每次重設）。
  - **使用者再要更亮 → `brightness` +1→+2（已達上限）、`contrast` 維持 -1**。**現值＝brightness +2 / contrast -1，其餘原廠**。
    已編譯+燒 COM8 SUCCESS，待目視。若仍不夠亮（brightness 已到頂）→ 只能碰曝光：單獨 `set_ae_level(+1~+2)` 提高自動曝光目標，
    或最後手段關 AEC 改手動 `set_aec_value` 固定長曝光（動態會拖影）；切忌再整包覆蓋。⚠ 教訓見 lessons（看不到畫面別盲調、一次一旋鈕）。
- **🚦 控制/遙測優先於影像 ＋ 羅盤校正值落地（2026-05-31）**：使用者回報操作有延遲、疑影像卡到控制。
  - **根因＝兩處優先級反轉**：相機 MJPEG 的 `esp_http_server` 預設 `task_priority=5` 且 `core_id=tskNO_AFFINITY`
    → 會在 **Core1 搶佔 `controlTask`（100Hz 控制迴圈）**、在 **Core0 搶佔 `networkTask`（ESP-NOW 遙測）**。
  - **修法**：`startHttpServer` 加 `cfg.core_id=0; cfg.task_priority=2`（相機 HTTP 釘 Core0、降優先級）；
    `startControlTasks` 把 `controlTask` 升 **pri5**、`networkTask` 升 **pri4**。最終 控制(5)≥遙測(4)>相機HTTP(2)>擷取(1)。
  - **羅盤校正常數已套用**（見上「待實機」）。編譯 SUCCESS（RAM 15.9%/Flash 36.0%）、已燒 COM6（MAC `…e0:b8` 核對為 ROV、開機 log 正常）。
  - **airtime 備註**：相機與 ESP-NOW 共用同頻道（且經 GS-AP 轉發＝雙倍 airtime）。上述只解 CPU 搶佔；
    若實測仍因頻寬卡頓，下一槓桿＝`streamTask` 對發佈幀率設上限（≤15fps，不影響 SD 錄影）。**先測再加**（`doc/05 §九`）。
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
- **🔋 INA260 電流計：USB 純供電（無電池）時 init 失敗 → 遙測 cur/power/bat 全是假 0（2026-06-05 實測）**：
  開機 log `[E] INA260 初始化失敗（0x40，I2C 無回應）` + `感測器狀態：compass=1 depth=1 ina260=0`。
  `readAllSensors` 只在 `g_inaOk` 才寫 `currentA/powerW/batPct`，故 init 失敗時三者保持預設 0（**不是量到 0，是沒量**）。
  - **研判**：同 I2C 匯流排的羅盤(0x2C)/深度(0x76)都 ACK → 匯流排健康；單 INA260 0x40 不回應，最可能＝
    **INA260 供電取自電池/馬達匯流排**，桌面 USB 無電池時整顆沒電 → 不 ACK。佐證：先前**接電池**場次 `bat=88~99%` 讀得到真值
    （晶片/位址/程式碼都正確、之前會動）。
  - **✅ 已確認（2026-06-06）**：接電池後開機 `感測器狀態：compass=1 depth=1 ina260=1`、遙測 `bat=77% cur=0.01A`（真值）。
    印證研判正確＝INA260 取電自電池匯流排，桌面純 USB 無電池時才不 ACK。晶片/位址/接線都正常，非故障。
  - **診斷可見性修復（sensors.cpp）**：把「感測器狀態行＋INA260 V/I/P」移到 **GPS `gpsSerial.begin(43/44)` 接管 UART0 console 之前**印、
    並留 `delay(120)` 排空 FIFO；否則啟用 GPS 的板子在 CH340 永遠看不到該行（GPS 一 begin 就切斷 console、半行截在 `Wire.cpp:296 b`）。
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
- **羅盤航向＋原始磁場進遙測，支援手機端自動校準（2026-05-30）**：`TelemetryPacket` 加 `float headingDeg/magX/magY`
  （兩端 packets.h 逐位元組同步，插在 `navDistanceM` 後、`photoAck` 前，`msgType` 維持最後，GS 收包靠 `data[len-1]` 判型不受影響）。
  `sensors.cpp` 新增 `getMagRaw()` 取**未校正** Gauss；`readAllSensors` 填 `snap.magX/magY`；`networkTask` 一併送出。
  手機網頁「🧭 校準」轉一圈收 min/max 算 `offset/scale`（模型對齊 `getCorrectedHeading` 的 `x=(gx-offX)*scaleX`，
  數值驗證還原航向誤差≈0°）。marker 亦改用真 `heading`（`magX/Y`=0,0 視為羅盤未上線→退回 GPS 位移）。
  **已燒錄 COM6（hash verified）；GS 端 `haveTelem=1` 證實新封包（43B）逐位元組正確收發**。
- **LB 即時拍照（2026-05-30）**：原 `takePhotoAndSave` 會切 720p+暫停串流+等 150ms（慢又頓）→ 改 **`takePhotoInstant`**：
  直接把目前串流最新影格（SVGA）寫 SD，**零延遲、不中斷畫面**。GS 端 LB 已邊緣觸發（按一下一張）。
  - 診斷工具（已留）：`WIFI_DIAG` 下 streamTask 每 2s 印 `[CAM] ok/null/big/seq`；
    遙測借 `navDistanceM` 回傳影格數（手動模式），GS COM4 DIAG 印 `cam(影格/-1停用)`。
- **🎬 錄影全黑 + 拍照連按 + 時間 1980 三修（2026-06-07，已燒錄 COM8/COM9 hash verified）**：
  - **錄影 .avi 全黑＝idx1 偏移每筆 +4（純容器 bug）**：`recorder.cpp:writeFrame` 舊式 `position-g_moviOffset-4`
    讓 idx1 的 dwChunkOffset 全部指進 JPEG 長度欄而非 `00dc` 標記；header 又設 `AVIF_HASINDEX`，**信任索引的播放器
    seek 到垃圾 → 整支黑**（ffmpeg/VLC 靠重掃才倖存）。改 `position-(g_moviOffset+8)`（對齊 'movi' FourCC）。
    **PC 端 host 重現驗證**（無需實機）：port `recorder.cpp` 邏輯成 Python，舊式 idx **0/10** 命中、新式 **10/10** 命中、
    ffmpeg 解出全部影格。**畫格本身沒問題**（拍照存的是同一份 `fb->buf`，照片正常＝JPEG 有效），故**不需 DHT 注入**。
  - **拍照要連按好幾下＝手機 25Hz 取樣漏掉短按**（根因在 GS/手機，非 ROV）：見 GS `CONTEXT.md`。ROV 端同步修
    `takePhotoInstant`：**只有真的寫檔成功才 `g_photoAck=true`**（舊版 `SD_MMC.open` 失敗也回 ack＝假「已存檔」提示）。
    → 答使用者問：toast 現在誠實可信，一下即可，不必盯著提示連按。
  - **照片/影片時間恆 1980＝系統時鐘從未設**（無 RTC/NTP、GPS 模組壞）→ FAT 起始紀元。新增鏈路：手機送
    `ts`(UTC 紀元秒)→WS→GS→ControlPacket 新欄 `epochS`→ROV `applyControl` 收到設一次 `settimeofday`；`main.cpp` 開機
    `setenv("TZ","CST-8")`→FatFs `get_fattime` 走 localtime → 新檔顯台灣本地時間。**ControlPacket 12→16B，兩端 `packets.h` 已同步**。
  - **✅ 實機回饋（2026-06-07）：換播放器後 .avi 可播、檔案時間正確**＝idx1 與時鐘修正成立。
- **🎬 第二輪（2026-06-07，已燒錄 COM8/COM9 hash verified、開機 ESP-NOW 遙測正常）：影片快轉 + 拍照即時利索**：
  - **影片快轉＝宣告 fps 假**：header 寫死 15fps，但實際擷取率（SD 寫入＋streamTask 最低優先）低於 15 → 同張數標 15fps
    播太快。修法：`recorder.cpp` 記 `g_recStartMs`，`stopRecording` 用**整段實測 fps**（影格數/秒）回補 avih
    `dwMicroSecPerFrame`＋strh `dwScale/dwRate`。**host 驗證**：宣告 15fps、真 5fps（10 影格/2s）→ ffprobe duration
    由 0.67s（3× 快轉）修正為 2.0s（real-time）。
  - **拍照要連按＝回饋慢＋連點被合併**（第一輪 latch 修了取樣漏，但回饋走 5Hz 遙測 ack ~400ms 才回 → 看不出拍到沒就猛按，
    且 latch 讓快速連點合併成一張）。**改單調序號**：手機每按一下 `photoSeq+1` 夾帶每筆上行，**ROV 序號一變就拍一張**
    （`applyControl` 比對，免邊緣偵測→任何取樣率/ESP-NOW 丟包都漏不掉、連點不合併）。**ControlPacket 把 `bool takePhoto`
    換成 `uint8_t photoSeq`（同 1 byte，仍 16B）**，兩端 `packets.h` 已同步。手機端加**即時快門閃光**（按下當下本地閃，不等 ack）。
    **⚠ 新舊不可混燒**：舊 ROV 把該 byte 讀成 `takePhoto` bool、序號恆非 0 → 會 100Hz 狂拍 → 兩端必須一起重燒。
- **天線診斷**：`WIFI_DIAG`（config.h，預設 1）每 2s 印 STA 狀態/RSSI/通道/IP/TX。
- **相機 XCLK LEDC**：改用 `LEDC_CHANNEL_7/TIMER_3`，避開馬達 `ledcAttach` 佔用的 ch0–5（原本相機用 ch0 與左馬達衝突）。
  ⚠ 原生 USB(COM6) `pio upload` 後常卡下載模式靜默 → 改從 GS COM4 看 ROV 自量 RSSI 較可靠
  （或 `python -m esptool --port COM6 --after hard_reset run` 讓它跑）。
  「訊號」遙測＝潛水艇 STA 看 GS AP 的 `WiFi.RSSI()`；`-127`＝當下未關聯（ESP-NOW 免關聯仍通）。
  弱訊號根因疑為板上「板載/IPEX 天線二選一」0Ω 電阻未切對 → 待實機檢查。
- **🛰️ 「地面站掃描/連線時好時壞」整起間歇案——根因＝天線餘裕不足（2026-06-06 證實）**：
  - **量到的證據**：ROV 貼在 GS 旁（~20cm）關聯，GS 端 `sta0(ROV) rssi=-71`、ROV 自量 `rssi=-69`。
    20cm 正常該 -30~-40，**整整少 30~40dB**，與 `comms.cpp:175` 註解「~20cm 仍 <-65 → 疑天線(0Ω)不良」吻合。
  - **ESP-NOW 連動性的真相**：ROV 純 STA（`comms.cpp`），ESP-NOW `peer.channel=0`＝跟隨本機頻道。
    ROV 只有**關聯上 AP 後**才落到 ch1，ESP-NOW 才送得到 GS。故 `haveTelem=0` 與 `stations=0` 是**同一件事**＝ROV 沒關聯。
  - **解釋使用者觀察「ESP-NOW 通＝手機才掃得到 AP」**：非因果，是**共同原因**。AP 開機就無條件 beacon（`softAP OK` 早於 ESP-NOW），
    但天線爛、有效範圍貼著關聯懸崖（~-80）；條件好時 ROV 關聯**且**手機掃得到（一起成立），條件差時兩者一起失敗。
  - **韌體槓桿已用盡**：ROV `max_tx_power(84)`、GS `max_tx_power(80)`＋HT20＋關省電。剩 30~40dB 缺口屬硬體天線。
  - ✅ **2026-06-06 結案＝壞的是 GS 那片天線**：使用者把舊 GS 天線搞爆、換新 GS 板（COM9）。同一份韌體、只換 GS 板，
    近距 RSSI 由 **-71/-69** 跳到 **GS聽ROV=-36 / ROV聽GS=-27**（改善 ~35dB），`stations=1 haveTelem=1` 每週期穩定、零掉線。
    先前「0Ω/IPEX」推論收斂於此。**ROV 端零改動就接上**——GS 韌體已把 AP MAC 釘死成 ROV 寫死的 `14:C1:9F:29:EA:AD`
    （見 GS `CONTEXT.md` 2026-06-06 紀錄），故 `include/config.h` 的 `GS_AP_MAC` 不必動。

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
- [⚠] **GPS 實測（2026-06-07）：模組 #1 確認壞、待換**。廣闊天空下 `charsProcessed=0`（零 NMEA）。
  逐項排除：接線 TX→43/RX→44 與韌體一致、swap 韌體 RX=44 仍 0（非極性）、共地正常（GPS GND↔RX≈3V）、
  VCC 4V+ 充足、紅 PWR LED 亮；**但 GPS TX 對地僅 0.1V(接ESP)/~1V(拔離)，到不了 idle-high ~3.3V＝模組沒在驅動 TX**。
  → 換新模組再測。診斷手法：**ROV 原生 USB COM8 app 接手後抓不到 steady-state**（只有 boot log），
  改把 `charsProcessed/衛星數/passedChecksum` 暫時夾帶進遙測 `depth/cur/bat` 欄位、讀超穩的 **GS COM9 DIAG**
  （同相機 cam 借 navDistanceM）；判完已全部移除、真值還原。見 memory `rov-gps-module-dead-tx-not-driving`。
- [ ] 馬達（離水、低工率）與緊急停車
- [⚠] **錄影 .avi 可播放性（2026-06-07）**：idx1 偏移 bug 已修並 **host 重現驗證**（buggy 0/10→fixed 10/10 命中、
  ffmpeg 解出全部影格）、已燒錄 COM8。**待使用者錄一段新 clip 拉出 SD 確認實機播放器能播**（舊檔仍是壞索引、會黑，要用新韌體重錄）。
- [ ] 與地面站整合（ESP-NOW 遙控、遙測、航點導航）

## 正式版（STA）連線測試結果（2026-05-29，COM6）
韌體側全數正常，唯一阻塞點是**地面站未開機**：
- 正式版（`goouuu_esp32s3cam`，含 `GS_AP_MAC=14:C1:9F:29:EA:AD`）編譯/燒錄/開機皆 SUCCESS，
  跑到 `=== 初始化完成 ===`、MJPEG server `:80/stream` 啟動，**無崩潰**。
- WiFi STA 啟動正常但 `Reason: 201 NO_AP_FOUND`（找不到 `ROV_GS`）→ 背景持續重連。
- 連帶 `onDataSent` ESP-NOW 送出失敗（peer 不可達，已 2s 節流告警）。
- **判讀**：ROV 端無問題；待整合只需把地面站開機廣播 `ROV_GS`（ch1），
  ROV 會自動關聯取得 192.168.4.100、ESP-NOW 隨頻道接通。屬硬體待辦，非程式缺陷。
