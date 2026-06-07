# Lessons（踩坑與教訓）

## 2026-06-07（日）— 相機「調更清晰」反而越調越暗：別覆蓋 OV5640 原廠預設、看不到結果別盲調

### 🚨 OV5640 原廠自動值已夠好，手動覆蓋一堆 sensor setter 來「提清晰/提亮」是反模式
- **症狀**：為了讓影像更清晰，加 `applyCamTuning()` 一次寫了 ~18 個 setter（AEC2/ae_level/gainceiling/lenc/raw_gma/
  對比/denoise…）。使用者回報**比原廠預設更暗**；接連調 `gainceiling` 4X→16X→32X、`ae_level`+2、`brightness`+2 都拉不回。
- **真因**：OV5640 在 esp32-camera 的多個 setter 互相干擾（DSP AEC2／gamma／對比等彼此牽動），淨效果變暗——
  **不是某個數值調錯，是「整段覆蓋原廠自動值」這條路本身錯了**。原廠預設的 AWB/AEC/AGC 本就調得好。
- **正解**：整段移除 `applyCamTuning`，還原成 git HEAD 原始狀態（只 `set_framesize`＋翻轉＋RSSI 調壓縮率，
  亮度/曝光/增益/白平衡全交原廠）。使用者確認原始版本很好。

### 看不到實際畫面就別做多旋鈕盲調——這是在擲骰子
- 我**從頭到尾沒看過水下實際影像**（無法目視串流），卻一次改十幾個參數又連改三輪，等於沒有回授地亂試。
- **教訓**：① 沒有視覺回授時，最安全是「不動原廠預設」；真要動，**一次只改一個參數＋請使用者目視**，別整包套。
  ② 連改 3 次（4X→16X→32X）還回不到原始好狀態，早該停手質疑「方法錯了」（systematic-debugging Phase 4.5），
  而不是繼續加碼同一條路。③ 使用者說「最原始就很好」＝ground truth，**直接還原到 known-good**，勝過繼續盲調。
- **簡單至上**：CLAUDE.md「拒絕過度設計」。原廠預設能用就別自作聰明覆蓋；改動越小越好。

## 2026-06-07（日）— GPS「搜不到衛星」其實是模組 TX 死：量輸出腳 + 夾帶遙測到 COM9

### 「搜不到衛星」先分「沒資料」還是「有資料無定位」——用 charsProcessed
- 遙測只有 `gpsValid/lat/lng`，看 false 無法分辨。關鍵指標是 **`gps.charsProcessed()`**：
  =0 → UART 完全沒資料（線/電/baud）；>0 但 `passedChecksum`=0 → baud 不對；句子過但 `sats`=0 → 天線/冷啟動/sky。
- 本案 charsProcessed 全程=0 → 根本不是天線/天空，是**沒有任何 NMEA 進來**。別被「廣闊天空」帶風向。

### ROV 端診斷走 GS COM9，不要硬抓 ROV USB COM8
- ROV 原生 USB-CDC app 接手後**不吐 steady-state**（reopen-loop 實測也抓 0，只有 boot log）。
- 解法＝把原始統計**暫時夾帶進遙測既有欄位**（`depth←chars`、`cur←passedChecksum`、`bat←sats`），讀超穩的 GS COM9 DIAG（同相機 cam 借 navDistanceM）。判完**立刻全部移除、還原真值**。
- 教訓：夾帶會污染手機上的真實電量/深度——使用者會察覺。能盡早還原就還原，長期要做就**開獨立欄位**別佔用真欄位。

### 韌體被質疑「TX/RX 反了」用受控測試證明，別嘴硬
- 使用者質疑韌體極性。**直接燒一版把 UART RX 改到另一支腳**實測：RX=43→chars=1、RX=44→chars=0，兩支都收不到 → 客觀排除極性，比口頭爭論有用。
- `GPS_RX_PIN` 是**從 ESP 視角命名**（ESP 接收腳，接對方 TX）；板上絲印「TX/RX」是預設 UART0 角色，軟體可改派，別被絲印誤導。

### 判模組死活：量它自己的輸出腳，UART idle 該是高電位
- 量 **GPS TX 對地**：健康 idle ~3.3V；本案 0.1V(接ESP)/~1V(拔離)＝模組沒在驅動 → 模組死。
- **紅 PWR LED 亮只代表 VCC 有到，不代表晶片在跑**；VCC 4V+/共地正常(量 GPS GND↔RX≈3V)仍可能模組壞。
- 量電壓先確認**參考點/接法**再判讀（使用者澄清「GND 接好、是拿 GPS GND 當基準量 TX/RX」後我才更正誤判）。

## 2026-06-06（六）— 影像「持續中斷」不是 RF：非侵入定位 + 把「判死」改「自癒」

### 訊號修好 ≠ 影像修好：先分清是哪條傳輸路徑
- **症狀**：換新 GS 板把 RF 修好（rssi −36/−27、stations/haveTelem 穩）後，影像仍「持續中斷」，使用者自然歸咎訊號。
- **關鍵切分**：遙測走 ESP-NOW（connectionless、低速率，−36/−27 量的是這條）；影像走 MJPEG/TCP 且經 GS-AP 轉發＝**雙跳 airtime**，是另一條路。RF 強只提升鏈路餘裕，不等於影像會好。
- **教訓**：報「影像斷」先問「斷的是哪條傳輸路徑」，別把遙測鏈路的好壞套到影像。

### 非侵入定位相機健康：從 GS 遙測讀 frame-seq + 探 /stream，別動會 reset 的 ROV
- GS COM9 的 DIAG 已含 `cam=cameraFrameSeq`（手動模式借 navDistanceM 回傳）＋對 `192.168.4.100/stream` 探測狀態。
- 實測 `cam` 硬凍住不動 + `/stream` 回 **HTTP 500**＝`g_camDead`（相機壞死）→ **不必開 ROV COM8**（開埠會 reset S3、還可能讓 OV5640 更卡）就鎖定是相機、非 RF、非手機。
- ROV 原生 USB-CDC 在 app 接手時會**重列舉**，single-open 抓 boot/[CAM] log 在 ROM banner（~0.45s）後就斷——這片抓相機健康，走 GS 遙測比硬抓 ROV serial 可靠。

### 同硬體時好時壞＝非決定性冷啟動；對邊際 sensor「試 N 次就永久放棄」是反模式
- `CAM_PIN_PWDN/RESET=-1`（沒拉到 GPIO）→ OV5640 冷啟動非決定性，需幾次 `reinit` 才起得來是隨機的：0–1 次→秒開、2–3 次→「等很久才有畫面」、舊韌體 >3 次→判死全黑。三種都是同一根因的擲骰子。
- **修法**：把「開機/執行期試 3 次就永久 `g_camDead` 閒置」改成**持續背景重試自癒**（退避 1→5s，相機一吐幀就清旗標續傳）。實證：冷啟動 wedge（`cam` 凍 84）自動恢復、爬到 800+。只要 reinit 可能救回，就持續重試（streamTask 最低優先級，不卡控制/遙測）。**這也推翻舊記憶「reinit 救不回、只能斷電」——是試太少次，不是救不回。**
- 先別猜大架構：LEDC timer 衝突用讀碼就排除（相機 ch7/timer3 vs 馬達 ch0–5/timer0–2），省掉一條岔路。

## 2026-06-05（四）— 「Wi-Fi 時好時壞」：先用現場 log 證明 AP 健康，再鎖 HT20 補餘裕

### 重新蒐證勝過沿用舊結論：抓現場 log 才知道「壞掉 vs 餘裕不足」
- **症狀**：手機有時掃不到 `ROV_GS`、有時掃到連不上、有時正常（典型 RF 邊界症）。上一輪已歸因天線失諧，但使用者反映仍在。
- **做法**：不沿用舊結論，pyserial 重置擷取 GS COM3 開機+執行 log。實證 `rst:0x1(POWERON)` 乾淨、**無 brownout/reset loop/panic**、
  heap 276KB、AP `OK`、抓 log 當下 ROV+手機都關聯、`haveTelem=1`、相機影格遞增 → **韌體/AP 健康，問題在 RF 餘裕不在程式**。
- **教訓**：「時好時壞」先抓 log 分層（開機原因 / 有無重啟 / AP 起沒起 / 即時 stations+RSSI），別憑症狀重猜硬體。

### 2.4GHz 弱訊號要鎖 HT20：HT40 攤兩倍頻寬 = 靈敏度 -3dB + 更怕鄰台
- **關鍵線索**：log 顯示 STA 以 `40U`(HT40/40MHz) 關聯。HT40 在已壅塞 2.4GHz 把發射功率攤到兩倍頻寬
  → 接收靈敏度約 -3dB、又更易被鄰台干擾，弱訊號(~-65dBm)下 beacon/WPA2 握手最先掉包 → 掃不到/連不上。
- **修法**：`net_wifi.cpp` 於 `WiFi.softAP()` 之後加 `esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20)`。
  驗證：STA 改以 `bgn, 20`(HT20) 關聯、RSSI 穩 -63/-65。**ESP-NOW 固定低速率不受影響；STA 頻寬跟隨 AP，ROV 端免改**。
- **判讀小抄**：DIAG/驅動 log 的關聯後綴 `20`=HT20、`40U/40D`=HT40 上/下副通道；2.4GHz 量產一律建議 HT20（只有 3 個非重疊通道，HT40 佔兩個很脆）。
- **直接改檔（非 git checkout）時增量建置可靠**：本次只改 `net_wifi.cpp`，mtime 是新的 → 增量重編該檔即可，不必 `-t clean`；
  仍以「新 printf 行出現 + STA 從 `40U`→`20`」雙重核對跑的是新韌體（非 stale build）。

## 2026-06-05（四）— 電流計(INA260) 沒響應其實多半是「沒接電池」；GPS remap 會切斷 CH340 開機 log

### 感測器 init 失敗別急著判晶片壞：先想它的供電在哪條 rail
- **症狀**：遙測 `cur=0.00A bat=0%`，從遙測分不出「量到 0」還是「根本沒量」（`readAllSensors` 只在 `g_inaOk` 才寫值）。
- **真相在開機 log**：`[E] INA260 初始化失敗（0x40，I2C 無回應）` + `感測器狀態：compass=1 depth=1 ina260=0`。
- **判讀**：同 I2C 匯流排的羅盤(0x2C)/深度(0x76)都 ACK → 匯流排健康；單一裝置不回應 + 桌面 USB 無電池 + 該裝置供電取自**電池匯流排**
  → 沒電當然不 ACK。佐證：先前接電池場次讀得到 `bat=88~99%`（晶片/位址/碼都對、之前會動）。**接電池再開機**即可區分「沒電」vs「真壞」。
- **教訓**：初始化失敗先問「它的 VS 供電在哪條 rail、現在那條有沒有電」，再談接線/晶片。

### GPS UART1 接管 43/44 會「半行截斷」CH340 開機 log → 診斷行必須印在 GPS begin 之前
- **症狀**：COM8(CH340) 開機 log 每次都精準停在 `[442][W][Wire.cpp:296] b`（"begin" 的 b），之後全靜默；兩次重抓同一點＝非隨機截斷。
- **真因**：`gpsSerial.begin(1, SERIAL_8N1, 43, 44)` 一把 GPIO43/44 從 UART0 console 切到 UART1 → UART0 TX FIFO 未排空的內容被沖掉
  → 半行截斷；之後 console 改走原生 USB（桌面未接）→ CH340 全黑。`感測器狀態`/INA260 結果原本印在 GPS 之後 → CH340 永遠看不到。
- **修法**：要在 CH340 看得到的開機診斷，一律印在 `gpsSerial.begin()` **之前**，並 `delay(120)` 讓 FIFO 排空再切。

## 2026-06-05（四）— 「垂直馬達一通電就轉」最終證明是硬體，非韌體

### 用遙測 `cur` 欄回傳除錯值，繞過 ROV USB-CDC 讀不到的死路
- **問題**：ROV 一通電垂直馬達就轉（沒接控制、手機沒連），疑韌體。靜態分析：失控保護把垂直馬達交深度 PID，
  但開機設定點＝當下深度→誤差≈0、增益又小（KP=2 PWM/公尺、PWM_MAX=1023）→ 應輸出 0。但需實測證實。
- **ROV 原生 USB-CDC（COM8）讀不到 runtime**：`pio upload` 後開埠 `len=0`（DTR 開關都試過）；app 的 TinyUSB CDC
  與 ROM 階段行為不一致、開埠不一定 reset、輸出不流到 reader。**別跟 ROV USB serial 纏鬥**。
- **解法＝把除錯值塞進遙測欄位，從 GS COM3 可靠讀**：`networkTask` 暫時 `pkt.currentA=(float)g_vertCmdDbg`
  （在 `applyControl`/`applyFailsafe` 抓 `setVertMotor` 的實際值），GS DIAG 的 `cur=` 就顯示垂直 PWM 命令。
  **不改封包大小（只換欄位內容）→ 不破壞兩端一致性**。實測 `cur=0.00` 全程、`depth=-0.10` 穩 → 韌體命令確為 0。
- **定論**：韌體命令垂直馬達=0，馬達卻轉 → **電路/硬體**（BTS7960 垂直驅動：RPWM=GPIO41/LPWM=GPIO42/EN=MCP23017 GPA4/5）。
  最可能＝41/42 訊號線鬆脫→輸入浮空（無下拉浮高）→送 0 也壓不住；或 EN 線異常/驅動損壞。**之前沒有＝最近大量插拔/搬動弄鬆線**。
- **方法論**：① 「之前沒有」先比對「改了什麼」——我們上次只改優先級、沒碰馬達/深度/開機。② 軟體嫌疑要**實測命令值**才能定罪/脫罪，
  別停在「應該是 0」。③ 讀不到某端 serial 時，借既有可靠回傳鏈（遙測）夾帶診斷值是萬用招。④ 用完即還原（已移除暫時改動）。

## 2026-06-04（三）— 連線不穩排查：先分清「AP 韌體 vs 天線(實體 RF)」＋ 增量建置會燒到舊 binary

### 🚨 git 來源的改動 + 增量建置 = 可能燒到「舊 binary」（差點誤判）
- **症狀**：GS 反映遙測/連線異常。重燒 GS（`-t upload` Hash verified、SUCCESS）後，開機 log 的
  `App version`/`Compile time` **仍是舊的（May 29）**，且該次 `pio run` 只花 **9s**（純 relink、沒重編）。
- **真因**：05-31 的 source 變更（`c605689` packets 31B→43B 等）是經 **git commit/checkout** 落地，
  **git 不保留 mtime** → 工作目錄檔案 mtime 可能「早於」上次 build 產物 → ninja 判定 obj 較新、**跳過重編**
  → `firmware.bin` 還是舊的 → esptool「Hash verified」驗的是**舊 bin 的 hash**，照樣燒進去也沒用。
- **正解**：凡 source 來自 git 操作（pull/checkout/merge/換機 clone）而非當下編輯，**先 `pio run -t clean` 全重編**再燒。
  本次 clean build 花 **202s**（真重編），重燒後 log 變 `Compile time: Jun 4 ... / App version: d8bb635`（=HEAD）才算對。
- **驗證鐵則**：**燒完一定讀開機 log 的 `Compile time`＋`App version` 核對是不是「剛建的那版」**；
  「Hash of data verified」只證明「寫進去的＝build 產物」，不證明「build 產物＝最新原始碼」。
- **封包一致性**：ROV 已燒新版（43B），GS 若還是舊版（31B），`onDataRecv_GS` 的 `len==sizeof(TelemetryPacket)`
  永遠不符 → **靜默丟掉所有遙測**（`haveTelem=0`）。CLAUDE.md 的「兩端 packets 逐位元組一致」要連**實際燒進去的版本**都算。

### GS（ESP-IDF）建置在 zh-TW Windows 會 cp950 解碼炸掉
- **症狀**：`pio run -e groundstation` 報 `UnicodeDecodeError: 'cp950' codec can't decode byte 0xe5`
  （`espidf.py:2321` 的 `fp.readlines()` 用系統 Big5 codec 讀到 UTF-8/CJK 位元組）。
- **正解**：跑 pio 前設 **`$env:PYTHONUTF8="1"`**（順手 `PYTHONIOENCODING="utf-8"`）強制 Python UTF-8 模式即過。
  環境變數在每個 PowerShell 工具呼叫**不持久**，故 build/upload/uploadfs 每條都要重設。

### 「連線不穩」要先分層：AP 韌體健康 ≠ 手機連得上；天線參數不進韌體
- **方法**：開 GS 序列埠看開機。`softAP(ROV_GS,ch1)->OK`＋`POWERON` 乾淨開機、跑 11s **無 brownout/無 reset loop**
  → **AP 韌體層健康、有在發**。此時「有時掃不到 SSID、有時掃到連不上」＝**實體 RF/天線**（beacon 弱到時有時無、
  WPA2 握手在 marginal 鏈路失敗），不是程式 bug。
- **天線參數不影響韌體送訊號**：程式只 `softAP()`＋`esp_wifi_set_max_tx_power()`，**不讀天線規格**；天線是被動輻射體。
  唯一 RF 軟體旋鈕＝TX power（本專案請求 84、driver 夾到 **80＝20dBm 已最大**）。故 5dBi/VSWR/極化/「50W」等規格
  **不會改變代碼行為**——「50W」是天線耐受功率，ESP32 只輸出 ~0.1W，無關。
- **實體可疑點**（firmware 未變、變的是天線時優先查）：① 模組版本——DevKitC-1 是 **WROOM-1（PCB 天線、無外接座）**
  還是 **WROOM-1U（U.FL 外接、無 PCB 天線）**；只有 -1U 該接外接天線，-1 硬接/未移 0Ω 匹配＝失配變更差。
  ② U.FL→SMA pigtail 是否插到底、SMA 是否鎖緊（U.FL 極易脫）。③ 3m 細同軸在 2.4G 損耗 3–6dB，淨增益可能反而 < PCB 天線。
  ④ A/B 測：手機貼著 GS 若就穩＝鏈路/天線問題、非程式。實機關聯後看 `[DIAG] sta0 ... rssi=` 量化。
- **天線 A/B 實測定錘（本次）**：用 GS DIAG 雙向 RSSI，逐一拔/換外接天線量四組。結果**兩端都板載 -68/-67（最好且零跳動）**，
  掛任一支外接都差 7~12dB（兩端外接 -79、單邊 -75~-81）。→ **外接天線根本沒接進 RF（0Ω 天線選擇電阻沒切到 IPEX）**，
  只是 IPEX 上的失諧短截線拖累板載 ~10dB、把訊號抖到 -80 關聯門檻＝「有時掃不到/連不上」。**「兩邊都有天線」≠ 訊號有走到外接**。
  要用外接（下水必須）得把 0Ω 移到 IPEX 焊接、天線伸出水面直立；沒切之前外接比不接更糟。**判天線好壞別看規格表，量 RSSI 做 A/B 最快。**

## 2026-05-30（五，第三批）— 停用藍牙要攔的是「範本的 app_main」，不是 setupGamepad

### 🔵 Bluepad32 app 範本在 Arduino setup() 之前就把 BT 開機
- **症狀/誤區**：要關地面站藍牙，第一直覺是在 `setupGamepad()` 不呼叫 `BP32.setup()` + `esp_bt_controller_mem_release()`。
  **重燒後 log 打臉**：`Bluepad32 ... / BTstack up and running / BLE_INIT / Bluetooth MAC` 全印在 `[GS] setup() start`
  **之前**，且 `mem_release` 回非 ESP_OK（BT 已被初始化，釋放無效）。還伴隨 `HCI not ready`、Wi-Fi
  `AP mode...opcode=0x0c05 status=1` 互卡。
- **真因**：本專案進入點是 Bluepad32 範本的 `main/main.c`（`app_main`）：`btstack_init()`+`uni_init()` 先把 BT 控制器
  開機，**Arduino 的 setup()/loop() 是被 `uni_init` 的 `on_init_complete`→`arduino_bootstrap()` 啟動的**。
  所以「在 setup() 裡關 BT」永遠太晚——BT 早就開了，且 setup() 本身是 BT 初始化鏈的產物。
- **正解**：改寫 `main/main.c` 的 `app_main()`：**完全不跑 btstack/uni_init**，先 `esp_bt_controller_mem_release(ESP_BT_MODE_BTDM)`
  回收 BT RAM，再**直接 `arduino_bootstrap()`** 啟動 setup()/loop()（與 BT 無關，loop 照跑）。
  `arduino_bootstrap()` 在 `bluepad32_arduino/include/arduino_bootstrap.h`（C linkage，可從 main.c 呼叫）。
- **驗證收穫**：log 不再有任何 BTstack/BLE/HCI 訊息、AP 乾淨起；**heap +65KB**（BT RAM 釋放）、
  **flash −360KB**（linker 把不再被引用的 BTstack/Bluepad32 GC 掉）。→ 印證「BT 真的沒被連進來」。
- **方法論**：① 改「停用某子系統」前，先確認**它是被誰、在哪一層初始化的**（看開機 log 的相對順序最快）；
  別假設「不呼叫某個 high-level setup 就等於關掉」。② 框架/範本常在 `app_main` 偷塞 init——進入點檔（`main.c`）
  才是真戰場。③ CONTEXT.md 舊敘述（「不可跳過 BT init 否則 loop 不跑」）是錯的假設，已順手更正——
  **舊文件的推測也要被實測打臉時更新**。

## 2026-05-30（五，第二批）— 相機壞狀態的真正極限 + 序列埠開埠會重置 S3

### 🚨 sensor-wedge 需「實體斷電」；SoC reset（含開序列埠）救不回
- **接續上面「相機開機自癒」**：實測發現軟體 `reinitCamera()`（deinit+init）對某些壞狀態**完全救不回**——
  log 反覆 `gdma_disconnect: no peripheral connected` + `fb_get` block ~9s 回 null，連續多 boot 都壞。
- **真因深一層**：PWDN/RESET 未接 → **任何 SoC 重置都不會把 OV5640 斷電**（`pio upload`、`esptool` reset、`esp_restart` 皆然）。
  sensor 一旦卡死，軟重置/整機重開都沒用，**只有拔插電源完整斷電**才回復。
- **🚨 開序列埠會 SoC-reset ESP32-S3 native USB CDC**：每次用 pyserial 開 COM6（DTR）都觸發 core 的 auto-reset，
  uptime 從頭算。先前誤以為「板子自己重開/crash」其實是**我開埠造成的**。→ 用序列埠盯相機＝每次都重置又不斷電＝永遠看到壞的。
  **教訓**：診斷 native-USB S3 的「是否自我重開」時，開埠本身就是干擾源；要看單一視窗內 uptime 是否連續，別跨多次開埠比較。
- **設計決策**：**不要用 `esp_restart` 做相機自癒**——對 sensor-wedge 徒勞，還反覆中斷控制/遙測（水下尤其糟）。
  改為**判死閒置**（`g_camDead`）：開機重試 3 次 / 執行期 reinit 3 次無效就放棄相機、streamTask 閒置，**控制與遙測續行**。
  使用者修法＝實體斷電重開；治本＝把 PWDN/RESET 焊到空閒 GPIO。**「能自動修」不等於「該自動修」——權衡中斷成本。**

### 兩個 pio 不要同時跑
- 同時跑潛水艇（pioarduino）與地面站（ESP-IDF）的 `pio run` 會撞工具鏈環境（`operable program or batch file` / Error 1）。
  → 序列化執行；不同 COM 埠也別圖快並行 build。

## 2026-05-30（五）— 相機「沒畫面」誤判成硬體（重大教訓）＋ 手機載入慢

### 相機：detected + fb_get timeout ≠ 硬體故障（我先前判斷錯）
- **最痛的教訓：只讀「一次開機」就斷言硬體故障是錯的。** 先前 `[CAM] ok=0 null=1`、`fb_get` 逾時回 null、
  GS→ROV 探測 0 bytes，我據此判定「相機/FPC 硬體故障」。**但那只是讀到「偶發死掉的那次開機」**。
  使用者堅持鏡頭正常、排線插緊後，我換成 `esptool hard_reset` 重開一次 → 立刻 `ok=23 seq 持續推進 lastLen≈21KB`，
  **45s 穩定 ~11fps**，GS 探測 `HTTP/1.1 200 OK` + 收到 **195397 bytes**。同一份韌體，開機有時死有時活。
- **真因**：OV5640 偶發開機進壞狀態，且本板 **PWDN/RESET 腳未接（config -1）**，驅動無法硬體重置 → 卡死回不來。
- **修法**：streamTask **開機自癒**（抓不到第一幀就 `deinit+init` 重試）＋ 執行期掉幀逾時 reinit。設定存 `g_camConfig`。
- **方法論修正**：宣告「硬體故障」前，**務必跨多次開機/冷熱重置重測**（尤其是無 RESET 腳的相機）；
  間歇性故障最會騙人。寧可多花一次 reset 重讀，也別讓使用者去重插/換模組。降 XCLK（10MHz）對「死開機」無效，
  反而證明不是訊號完整性/頻率問題——**降頻沒救＝不是 marginal 接觸**，該往「狀態/初始化」想。

### 🚨 航點分頁沒反應 + 沒畫面（同時壞）＝ window.load 被無限 MJPEG `<img>` 卡住
- **症狀**：手機上點「航點」分頁完全沒反應、影像也沒出來、遙測也沒更新——**全部一起壞**。
- **真因**：`index.html` 把 `<img id="stream" src="http://192.168.4.100:80/stream">` 直接寫在初始 HTML，
  而 `app.js` 用 **`window.addEventListener('load')`** 啟動。MJPEG multipart 是**無限連線、永不「載入完成」**
  （相機死開機時連得上但 0 bytes、活著時持續串流），所以 **`window.load` 永遠不觸發** →
  `initTabs/initStream/connectWS` 全沒跑 → 分頁沒綁監聽（點了沒反應）、無遙測、無影像。「多個東西一起壞」是強烈訊號。
- **修法**：① 改用 **`DOMContentLoaded`**（DOM 解析完即觸發，不等子資源）啟動；
  ② `<img>` 的 `src` **不要寫在 HTML**，改由 `initStream()` 用 JS 設定（`img.src = STREAM_SRC`）。
- **驗證法（無瀏覽器擴充時）**：headless Chrome `--headless=new --dump-dom --virtual-time-budget=4000`（會跑 JS）
  經 `Start-Process -RedirectStandardOutput` 存檔（`*>`/`2>$null` 在本機抓不到 native exe stdout）。
  檢查 DOM：`#stream` 的 src 是否被 JS 設上、autoclick 後 `#pane-map` 是否 `active`、`#map` 是否變 `leaflet-container`。
- **附帶**：serveStatic `setCacheControl` 從 `max-age=600` 改 **`no-cache`**（ETag 重新驗證、未變動回 304），
  否則改版後手機最長 10 分鐘載到舊快取，會讓「我已修好但手機還是壞」的除錯鬼打牆。

### 手機連上後載入很慢
- **元兇：`leaflet.js` 147KB 未壓縮、且在 `<head>` 同步 `<script>` 載入會擋住整支 app.js**
  （WS 遙測、手把、影像初始化全得等它從 LittleFS 經 ESP32 SoftAP 慢慢傳完）。地圖在第二分頁、首屏根本用不到。
- **解法**：① Leaflet 改**延遲載入**（首次點「航點」分頁才動態注入 css/js）→ 移出關鍵路徑；
  ② **gzip**（147KB→42KB、css 14.8→3.5KB）。本 ESPAsyncWebServer fork `serveStatic` 的 `_tryGzipFirst` **預設 true**，
  只要放 `xxx.gz` 同名檔就自動帶 `Content-Encoding: gzip` 回傳（原檔可留著供本機 dev，裝置端優先送 .gz）。
- **教訓**：ESP32 SoftAP + LittleFS 供檔頻寬有限，**大型第三方資產一律 gzip + 延遲/非同步載入**，別放進首屏同步路徑。

## 2026-05-29（三）— 手把↔Wi-Fi 共存硬限 → 手把改接手機；前端重構

### 共存（重大，已實測定論）
- **連線中的 classic-BT 手把會「完全餓死」GS 的 Wi-Fi AP**：實測 `gamepad 連線 → AP stations=0`、
  潛水艇/手機一個都關聯不上、ROV rssi=-127、影像連不到。ESP-NOW（免關聯）不受影響仍通。
- **`esp_coex_preference_set(PREFER_WIFI)`（含 BT init 後重設）壓不住**；ESP-IDF 對「Wi-Fi **AP** + classic BT」
  共存支援差（官方良好的是 Wi-Fi STA + BLE）。**閒置 BT 不影響 Wi-Fi（-58 正常），只有「連線中」會餓死。**
- **解法（採用）：手把改配對到「手機」**，瀏覽器 Gamepad API 讀軸值 → 經既有 WebSocket 上行 → GS 轉 ESP-NOW。
  GS 端：`enableNewBluetoothConnections(false)` + `forgetBluetoothKeys()` + **onConnected 立即 `c->disconnect()`**
  （已綁定手把仍會回連，必須主動踢掉）。GS 藍牙保持閒置（不重構入口、不關 BT controller，風險低）。
- **`gamepadConnected()` 重新定義為「WS 控制新鮮度」**；BT 連線狀態要另開 `btControllerConnected()` 觀察，
  別把兩者混為一談（否則看 gamepad=0 會誤判 BT 已斷）。

### 影像鏈路除錯（端到端）
- **分層定位法**（手機看不到影像時）：① GS COM4 看 `stations`/`ROV自量 rssi`（關聯？）→
  ② GS 端開 WiFiClient 探測 `192.168.4.100:80/stream`（GS→ROV 可達？有資料？）→
  ③ 遙測借欄位回傳相機影格數 + streamTask `[CAM] ok/null/big` 統計（相機有產影格？）。
  本次靠這套把「影像黑」從「以為是訊號/共存」精準收斂到「**相機零影格**」。
- **client↔client 不是問題**：手機能連 .1(GS) 卻看不到 .100(ROV) 一度懷疑 AP 不轉發；但 GS→ROV 探測
  「連得上、0 bytes」證明是 ROV 串流端沒資料，非轉發。**先用 GS→ROV 探測排除網路，再查相機**。
- **相機 init 成功 ≠ 會出影格**：`esp_camera_init` 只驗 SCCB+XCLK+sensor ID（PID 0x5640）；
  `esp_camera_fb_get` 卡逾時回 null＝高速資料線(D0-D7/PCLK/VSYNC)無訊號 → 多為 FPC 排線/模組接觸，韌體改不動。
- **forgetwatchdog 教訓**：加了 `wifiReconnectWatchdog()` 函式卻忘了在 networkTask **呼叫**，
  導致潛水艇 deauth 後仍不自癒（debug 半天）。加自癒/週期函式後，務必確認有被呼叫。

### 韌體自癒 / 前端
- **潛水艇 STA 被 deauth 後會卡死、不自動重連**（手把那波 deauth 後 ROV 一直 -127，重置才好）。
  必加 **重連 watchdog**（未關聯時每 5s `WiFi.disconnect()+begin()`）——水下不能重置，這是必要。
- **Leaflet 在隱藏分頁(display:none)初始化 → 手機地圖空白**；切到該分頁時要 `map.invalidateSize()`
  （桌面剛好版面夠大看似正常，手機才暴露）。resize/orientation 也要 invalidateSize。


## 2026-05-29（二）— 前端 PWA 化 + 操控/感測修正

### 前端 / PWA
- **ESP32 AP 走純 http（192.168.4.1）是「非安全來源」**：`navigator.serviceWorker.register` 會失敗，
  Chrome 完整 PWA 安裝條件（https+SW）無法滿足。要「App 化/隱藏網址列」靠：① manifest `display:standalone`
  （手動「加到主畫面」仍生效）② **Fullscreen API**（`requestFullscreen`，Android 走 http 也行）。SW 仍留著
  best-effort（localhost/https 才會註冊；GS 本機供檔，離線本就 OK）。
- **遙測數值溢出**：固定 flex %寬的 cell 容不下「-127 dBm」→ 改 CSS grid（窄 3 欄/寬 6 欄）+ `min-width:0`。
- **影像 `<img>` 載入失敗無高度** → 容器會塌陷、佔位圖看不到；`video-wrap` 要給 `min-height`。
- **headless Chrome 截圖驗證前端**（無實機/擴充未連時）穩定配方（Windows）：
  `--headless=new` + 先 `Stop-Process chrome` 清殘留 + **每次給唯一 `--user-data-dir`**（否則 profile 鎖→寫檔失敗）;
  本機先 `python -m http.server` 供檔（避免 file:// 下 `/abs/path` 解析錯）。

### 訊號 / 序列埠診斷
- **潛水艇原生 USB 是 USB-Serial/JTAG（COM6）**：`pio upload` 後常**靜默無 log**（app 切 TinyUSB CDC 或卡下載模式），
  `esptool --after hard_reset run` 後仍可能讀不到。**改從可靠的 GS COM4 印出潛水艇自量 RSSI**
  （遙測封包本就帶 `pkt.rssi`，於 `sketch.cpp` GS DIAG 印出）——免跟 COM6 USB 重列舉纏鬥。
- **天線/弱訊號軟體驗證**：同時看「GS 聽 ROV」(`esp_wifi_ap_get_sta_list` rssi) 與「ROV 聽 GS」(`pkt.rssi`)。
  兩者對稱（實測 -62/-58）＝無收發不對稱；近距離(<0.5m)若仍只有 -55～-65（理想 ~-40）＝天線(0Ω 切換)偏弱，
  影響「range/margin」而非「完全連不上」。**影像全黑的主因是 RSSI=-127＝未關聯**（連不到 192.168.4.100、
  且 <-75 觸發串流暫停），非單純衰減。

### 韌體（操控/感測）
- **馬達驅動 EN 腳式急停**：拉低 EN 後，單純再寫 PWM 不會動；解除急停要顯式 `enableMotors()` 復歸 EN。
  且急停按鈕送「瞬時值」會一放就恢復、送鎖存又永遠停 → 正解是**邊緣觸發 toggle**（latch，再按解鎖）。
- **差速轉向方向以實機為準**：數學上 (y+x, y-x) 沒錯，但實機可能因軸號/左右馬達位置對調而相反；
  使用者實測「搖桿左推卻右轉」→ 對調 x 項 (y-x, y+x) 即修正。注意此修正在 GS computeDifferential（只影響手動）；
  若自動導航轉向也反，根因是左右馬達位置對調，應改在潛水艇馬達輸出層對調（手動/自動一起修）。
- **電量別用瞬時匯流排電壓線性內插**：馬達負載→內阻壓降→%忽上忽下（放開彈回 100）。
  正解：內阻補償回推 OCV（`V_bus+|I|×R`）+ 非線性 OCV→SoC 查表 + 顯示「只降緩升」。
  電壓可直接由電流計（INA260 `readBusVoltage`）讀；功率用其 `readPower` 暫存器（實測 V×I）非假設電壓。
- **分清「訊號」語意**：遙測 RSSI＝ROV STA 看 AP 的 `WiFi.RSSI()`；`-127`＝未關聯。ESP-NOW 免關聯，
  故「有遙測但訊號 -127、影像黑」會並存（影像需 STA 關聯到 192.168.4.100）。


## 2026-05-29 — 地面站 ESP-IDF 建置 + Wi-Fi/BLE 共存整合

### 建置（Windows + pioarduino espidf）
- **別亂刪正在運作的 `.pio`**：刪掉後乾淨建置會暴露 builder 的 `utils.c.o` 撞名 bug
  （兩個 component 同名 `utils.c` 都被丟到 build root）。先確認快取為何「能用」再動它。
- **路徑繞法會引入新問題，要修根因**：
  - `PLATFORMIO_BUILD_DIR=C:\b` → map 檔路徑 `\b \f` 被當跳脫吃掉 → link 失敗。
  - junction `C:\g` → 被解析回真實路徑 → 觸發 utils.c.o 撞名。
  - subst `S:` → CMake 記真實 `D:\`、PROJECT_DIR 是 `S:\` → 跨碟 relpath 出錯（`main/`→`ain/`）。
  - 正解：修 builder（obj 用「來源目錄短雜湊 + basename」），再從原始 `D:\` 路徑建。
- **espidf 專案別加 `platformio.ini` build_flags**：會誘發同一個 utils.c.o 撞名；
  要編譯期宏就寫進原始碼/`diag.h` 或 CMake `target_compile_definitions`。
- **MAX_PATH(260)**：libsodium 深層檔名超界，`ar.exe` 報 `No such file`。短雜湊 obj 路徑可一併解掉。
- **builder 修補在 `~/.platformio`（不在 repo）**：重裝平台/換機要重套，否則乾淨建置壞。詳見 GS `CONTEXT.md`。

### 序列埠 / 偵錯
- **原生 USB-CDC（潛水艇 COM6）**：pyserial 開埠時 DTR/RTS 重置會 re-enumerate，常誤入下載模式，
  「序列無輸出」≠ 當機。乾淨重啟用 `esptool.py --port COMx --after hard_reset run` 再**另開埠**讀。
- `netsh wlan show networks` 回的是**快取**，不是即時掃描；要新鮮結果先 `disconnect` 再查。

### Wi-Fi / BLE 共存（重大）
- **ESP-NOW（連線無關、固定頻道）不受 BLE 影響**；受影響的是 **Wi-Fi WPA2 關聯握手**。
- **BLE 主動掃描會害 STA/手機關聯失敗**（受控實測：掃描開→`NO_AP_FOUND`；關→3–6s 關聯穩定）。
  解法＝「配對視窗」：開機開掃描供配對/重連，連上或逾時即關掃描。
- **顯式 `coex_init()/coex_enable()`+BALANCE 反而讓 STA 掉線**；用 `esp_coex_preference_set(PREFER_WIFI)` 即可。
- **分清 `haveTelem`(ESP-NOW 收到) ≠ 已關聯(AP station)**：兩者機制不同，別用一個推論另一個。
- **驗證客戶端本身要可靠**：開發 PC 的 TP-Link USB 網卡訊號太弱、掃不到近處 ESP32 AP，
  不能當手機代理；用「緊鄰 GS 的 ESP32（潛水艇）」當權威關聯測試端更準。


## 2026-06-06 — 「掃描/連線時好時壞」整起間歇案：找共同原因、用近距 RSSI 釘根因

- **兩個看似獨立的症狀，先找共同原因再分頭修**：本案「手機掃不到 AP」＋「GS 收不到 ROV 遙測」
  其實同源。讀 ROV `comms.cpp` 才看清：ROV 純 STA、ESP-NOW `peer.channel=0`（跟隨本機頻道），
  **ROV 只有關聯上 AP 才落到 ch1、ESP-NOW 才送得到**。故 `haveTelem=0` 與 `stations=0` 不是巧合，
  是**同一個門檻（STA 關聯）**。先前 lesson「haveTelem ≠ 已關聯」在機制上對，但此設計下兩者被關聯閘**綁在一起**，別當成兩條獨立線索各查。
- **使用者觀察「ESP-NOW 通了手機才掃得到 AP」＝共同原因，非因果**：AP 開機就無條件 beacon（log `softAP OK` 早於 ESP-NOW init）。
  真相是天線餘裕太差、有效範圍貼著關聯懸崖；條件好時 ROV 關聯**且**手機掃得到（一起成立），差時一起失敗。
  **遇到「A 發生時 B 才出現」先別套因果**，查兩者是否被同一個底層條件（這裡＝RF 餘裕）一起掐住。
- **近距 RSSI 是釘死天線根因的決定性量測**：把權威客戶端（ROV）貼到 GS ~20cm 量關聯 RSSI。
  量到 -71/-69（正常該 -30~-40，缺 30~40dB）→ 直接證實天線/0Ω 問題，不必再盲抓 log。
  韌體側（`max_tx_power` 拉滿、HT20、關省電）能調的都調了仍這麼弱 → 缺口只能是硬體天線。
- **firmware「有電在跑」≠「有上線」**：ROV COM8 開機 log 乾淨（POWERON、sensors 全 OK）但 GS 端 `stations=0`；
  關聯結果印在 GPS 接管 console 之後（跑去沒插的 COM6），COM8 看不到。判「在不在線」要看**對端（GS）**的 station/telem，
  別只憑本端開機 log 就斷定。


## 2026-06-06 — 換板結案：A/B 釘根因、ESP-NOW 對端 MAC 改韌體釘死、cp950 建置雷

- **換掉可疑硬體、韌體保持不變＝最乾淨的根因確認**：使用者把舊 GS 天線搞爆換新板，同一份韌體只換 GS 板，
  近距 RSSI 由 -71/-69 跳到 -36/-27（改善 ~35dB）、`stations=1 haveTelem=1` 穩定零掉線。整起間歇案就此釘死＝**GS 天線**。
  先前各種 0Ω/IPEX 假設不必再逐一試——一次受控 A/B（換可疑件、固定其他變數）就收斂。
- **ESP-NOW/AP 對端 MAC 寫死時，換板要在韌體釘 MAC，不要每次重填對端 config**：ROV 把 `GS_AP_MAC` 寫死。
  與其每換一片 GS 就回頭改 ROV `config.h`＋重燒 ROV（ROV 還不一定接著電腦、native-USB/GPS console 又難燒），
  不如讓 GS 開機強制把自己的 AP MAC 釘成那組固定值 → **換板 drop-in、ROV 零改動**。這才是「換板」的根因級解法。
- **釘 SoftAP MAC 要用 `esp_iface_mac_addr_set(..., ESP_MAC_WIFI_SOFTAP)` 且在 `esp_wifi_init()` 之前**，
  別用 `esp_wifi_set_mac(WIFI_IF_AP, ...)`：後者要求介面 disabled，在 Arduino `WiFi.mode()` 後插裸 `esp_wifi_stop()`
  會打亂 Arduino WiFi 狀態機 → `softAP()` 回 FAIL、`max_tx_power` 讀回 0（SSID/auth/功率都沒套用，MAC 卻變對了，最易被騙）。
  **驗證要看「全套都綠」**（pin OK＋softAP OK＋tx=80＋末行 MAC 正確），別只看 MAC 變對就收工。
- **zh-TW(cp950) Windows 跑 PlatformIO/ESP-IDF 先 `$env:PYTHONUTF8="1"`**：builder（espidf.py）用 locale codec
  `open().readlines()` 讀 `partitions.csv`，檔內有中文註解（UTF-8）就 `UnicodeDecodeError: 'cp950' ... 0xe5`。
  PYTHONUTF8 一次解掉整類「locale 編碼讀 UTF-8 檔」，比逐檔砍中文穩。環境變數不持久，每條 pio 指令都要帶。
- **換板 COM 埠會變且驅動可能不同**：新 GS 板認成 FTDI「USB Serial Port (COM9)」，非舊板的 CH340；
  `platformio.ini` 釘死的 `upload_port` 要用 `--upload-port COMx` 覆寫。
