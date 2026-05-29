# CONTEXT — 地面站 (Ground Station) ESP32-S3 韌體

開發交接文件。ROV 潛水艇專案的**地面站**韌體 + 手機操作網頁；潛水艇端為另一獨立專案（`../Submarine-Code/`）。
共用設計規格見專案根目錄 `../doc/03~06`。

## 職責
- Wi-Fi 純 AP（`192.168.4.1`，SSID `ROV_GS` / pass `rov12345`）
- WebSocket 推遙測給手機（150ms ≈ 6.7Hz）
- HTTP POST 收手機上傳的航點，分塊經 ESP-NOW 轉給 ROV（每塊 14 點）
- ESP-NOW 與 ROV 雙向（送控制/航點、收遙測）
- 讀原廠 **Xbox Series 手把（藍牙 Bluepad32/BTstack）**，差速混控成馬達指令
- **不碰影像**：手機 `<img>` 直連 ROV `192.168.4.100`

## 建置框架（重要：已遷移 ESP-IDF）
- **PlatformIO + ESP-IDF**（`framework = espidf`），非舊版 Arduino/`.ino`。
- 平台釘選 **pioarduino `54.03.21-2`**（Arduino 作為 IDF component，core 3.x）。
  54.03.21 的工具鏈對 `esp_lcd` 有編譯器 ICE，`-2` 已修，**不要降版**。
- board `esp32-s3-devkitc-1`；env 名稱 `groundstation`；`src_dir = main`。
- ⚠ **必須用原生 PowerShell 建置**（非 MSYS/Git-Bash），否則 `idf_tools.py` 會拒跑。
- Arduino core 3.x 依賴 esp_insights/esp_rainmaker，`platformio.ini` 已 `embed_txtfiles` 嵌入其憑證。

## 建置環境踩坑（Windows，務必先讀）
> 2026-05-29 乾淨建置（刪 `.pio` 後）會踩到兩個 Windows 問題，**現以「修補 PlatformIO builder」一次解決**。
> 平台已釘 `54.03.21-2`，所以修補穩定；但 builder 檔在 `~/.platformio`（**不在本 repo**），
> 重裝平台/換機後需重新套用，否則乾淨建置會壞。

- **症狀 1：`Two environments ... same target ... utils.c.o`**。
  `managed_components/` 內有兩個同名 `utils.c`（`espressif__libsodium` 與 `espressif__rmaker_common`，
  由 RainMaker/Insights 透過 arduino core 拉入），builder 對「非框架的絕對路徑來源」只取 basename →
  撞同一個 `$BUILD_DIR/utils.c.o`。**有現成 `.pio` 快取時不會發生**（故初版能建），刪掉就重現。
- **症狀 2：`ar.exe ... No such file or directory`（libsodium 封存）**。
  libsodium 深層檔（`...crypto_box/curve25519xchacha20poly1305/box_seal_...c.o`）路徑剛好 260 字元，
  撞 Windows `MAX_PATH`（`LongPathsEnabled=0`），`ar.exe` 開不了。
- **解法（已套用）**：修補
  `~/.platformio/platforms/espressif32/builder/frameworks/espidf.py`
  的 `compile_source_files()`，把「非框架來源」的 obj 改成 **來源目錄短雜湊子目錄 + basename**
  （既不撞名、路徑又短，同時解掉症狀 1、2）：
  ```python
  # 檔頭 import 區加：
  import hashlib
  # compile_source_files() 內，原本的：
  #   else:
  #       if not os.path.isabs(source["path"]):
  #           obj_path = os.path.join(obj_path, source["path"])
  #       else:
  #           obj_path = os.path.join(obj_path, os.path.basename(src_path))
  # 改成：
  else:
      dir_hash = hashlib.md5(
          fs.to_unix_path(os.path.dirname(src_path)).lower().encode()
      ).hexdigest()[:8]
      obj_path = os.path.join(obj_path, dir_hash, os.path.basename(src_path))
  ```
- **不要用 subst/junction 繞 MAX_PATH**：junction 會被解析回真實路徑 → 反而觸發 utils.c.o 撞名；
  subst（如 `S:`）會讓 CMake 記真實 `D:\` 路徑、`PROJECT_DIR` 卻是 `S:\` → 跨碟 relpath 出錯
  （`main/` 變 `ain/`）。修補 builder 後**直接從原始 `D:\...` 路徑建置即可**。
- **不要用 `platformio.ini` 的 `build_flags`**：espidf 專案加 `build_flags` 也會誘發 utils.c.o 撞名；
  需要編譯期宏就寫進原始碼/`main/diag.h` 或 `CMakeLists` 的 `target_compile_definitions`。
- 建置/燒錄：`pio run -e groundstation [-t upload]`，仍須**原生 PowerShell**。
- 原生 USB-CDC 板（如本專案潛水艇 COM6）用 pyserial DTR/RTS 重置會 re-enumerate、
  且常被誤帶進下載模式 → 序列看似「無輸出」≠ 當機。要乾淨重啟用
  `python <esptoolpy>/esptool.py --port COMx --after hard_reset run`。

## 程式結構（`main/`，ESP-IDF component）
- `main.c` — Bluepad32/BTstack 進入點（`initBluepad32()`）；autostart 後轉入 Arduino `setup()/loop()`。
- `sketch.cpp` — Arduino `setup()/loop()`：初始化順序（依 `../doc/04` §七）+ loop 節流（控制 ~100Hz、遙測推送 ~6.7Hz）。
  **不呼叫 `Serial.begin()`**（會搶走 IDF console）；全程用 `printf` 走 IDF console（UART0 → COM4）。
- `net_wifi.*` — `setupWifi_GS()` 純 AP + 關省電 + **coex 偏好 WIFI + 最大 TX 功率**（見「Wi-Fi/BLE 共存」）。
- `espnow_link.*` — ESP-NOW 收發、`sendControl()`、`sendWaypointsToROV()`、`takeLatestTelemetry()`。
- `web_server.*` — ESPAsyncWebServer + WebSocket `/ws`、`POST /api/waypoints`、靜態檔（LittleFS `/www`）。
- `gamepad.*` — Bluepad32（BTstack）藍牙手把；對外只給軸值/按鍵（軸值已轉文件慣例：前推為負、右為正）。
  **含「配對視窗」：開機開 BLE 掃描 `GS_PAIR_WINDOW_MS`(45s)，連上或逾時即關掃描**（見「Wi-Fi/BLE 共存」）。
  `gamepadOpenPairing()` 可重開視窗（保留給 web 觸發）。
- `diag.h` — 診斷開關（`GS_DIAG_STA`：每 3s 印 AP station 數/MAC/RSSI + haveTelem，正式可設 0）。
- `control.*` — `computeDifferential()` 差速混控 + 按鍵邊緣狀態機。
- `config.h` — SSID/pass/channel、`ROV_STA_MAC`（已填 `14:C1:9F:29:E0:B8`）、節流/死區/航點常數。
- `packets.h` — ESP-NOW 封包結構（**ROV 端 `../Submarine-Code/include/packets.h` 須逐位元組一致**）。
- `CMakeLists.txt`（`main/`）— component 來源檔與 REQUIRES（bluepad32、bluepad32_arduino、arduino、btstack、AsyncTCP、ESPAsyncWebServer、ArduinoJson、esp_wifi）。

## 燒錄與檔案系統
```
pio run -e groundstation              # 編譯韌體
pio run -e groundstation -t upload    # 燒韌體（COM4）
pio run -e groundstation -t buildfs   # 打包 LittleFS（data/www → 網頁）
pio run -e groundstation -t uploadfs  # 上傳網頁到 LittleFS（韌體與網頁兩者都要做）
pio device monitor -e groundstation   # 序列監看（COM4 @115200）
```
- 分割表 `partitions.csv`（16MB flash）：factory app 3MB + spiffs(LittleFS) 2MB + coredump 64KB。
- `board_build.filesystem = littlefs`。

## 手機網頁（`data/www/`）
`index.html` / `app.js` / `style.css` + 本地 Leaflet（`leaflet.js`/`leaflet.css`，**已 gzip**：`*.gz` 同名檔）。
地圖點擊新增航點（間距 ≥5m 防呆）、即時遙測、串流徽章/RSSI 警告/拍照 toast/導航狀態、
WebSocket 指數退避重連、tile 載入失敗降級灰網格。

**載入慢修正（2026-05-30）**：手機連上後載入很慢，主因＝`leaflet.js` 147KB 未壓縮 + `<head>` 同步
`<script>` 擋住 app.js（WS/手把/影像初始化全等它經 SoftAP/LittleFS 傳完）。地圖在第二分頁、首屏用不到。
- **Leaflet 延遲載入**：`index.html` 移除 leaflet 的 `<link>`/`<script>`；`app.js` 首次切到「航點」分頁時才
  `ensureMap()` 動態注入 `/leaflet.css` + `/leaflet.js` 再 `initMap()`（遙測/影像/手把不再被 147KB 擋住）。
- **gzip**：`leaflet.js` 147→42KB、`leaflet.css` 14.8→3.5KB。本 ESPAsyncWebServer fork `serveStatic` 的
  `_tryGzipFirst` **預設 true**，放 `xxx.gz` 同名檔即自動帶 `Content-Encoding: gzip`（原檔留著供本機 dev）。
  ⚠ 改 leaflet 後要重 gzip；app.js/style.css/index.html 我會手改、**不 gzip**（避免 stale .gz）。

**🚨 分頁沒反應＋沒畫面同時壞（2026-05-30）**：真因＝`<img id=stream>` 的 src 寫在初始 HTML + `app.js`
用 `window.load` 啟動。MJPEG 是無限連線、永不 load 完成 → **`window.load` 不觸發** → `initTabs/initStream/connectWS`
全沒跑（分頁沒綁監聽＝點了沒反應、無遙測、無影像）。修法：app.js 改 **`DOMContentLoaded`** 啟動、
`<img>.src` 由 `initStream()` 用 JS 設定（不寫在 HTML）。並把 serveStatic `setCacheControl` 改 **`no-cache`**
（ETag 重新驗證；原 `max-age=600` 會卡手機載舊快取最長 10 分鐘）。已用 headless Chrome `--dump-dom` 驗證
（boot 有跑、點航點→pane active、`#map`→`leaflet-container`）。

**PWA / 介面（2026-05-29 依手機實測整修）**：
- `manifest.json` + `icon.svg` + `sw.js`：可「加到主畫面」以 standalone 啟動（隱藏網址列）。
  ⚠ `http://192.168.4.1` **非安全來源 → service worker 不會註冊**（離線本由 GS 本機供檔，無妨）；
  「隱藏網址列」主要靠 **manifest standalone + 全螢幕鈕**（`app.js initFullscreen` 呼叫 requestFullscreen，
  Android 走 http 亦有效）。
- 遙測列改 **CSS grid**（窄屏 3 欄／寬屏 6 欄）修「-127 dBm」溢出裁切。
- 影像中斷顯示**斜紋佔位圖**並每 3s 自動重連（`initStream`，補上原本未定義的 onStreamError）；
  `video-wrap` 加 `min-height` 避免串流斷時區塊塌陷。
- **急停橫幅**：遙測 JSON 新增 `estop`（`web_server.cpp` 取 `controlEstopLatched()`），
  地面站急停 latch 時手機顯示紅色閃爍橫幅。
- **分頁佈局（科技風）**：常駐頂列 HUD（GS/PAD/CAM 狀態燈 + 模式 + 全螢幕）+ 常駐遙測 HUD（狀態監控）
  + 分頁切換〔影像〕〔航點/地圖〕，解決三區擁擠。青藍霓虹、等寬發光數字、影像 HUD 角框。
- **手把（控制上行）**：`app.js` 用 Gamepad API 輪詢（~25Hz），送 `{"t":"c",lx,ly,ry,b}` 經 ws 上行。
  手把配對到「手機」（非 GS）。位元：bit0=A..3=Y,4=LB,5=RB,6=Start,7=Back。
- **手機地圖修復**：Leaflet 在隱藏分頁初始化會空白；切到航點頁時 `map.invalidateSize()`（resize 亦然）。

## Wi-Fi/BLE 共存（2026-05-29 實測，關鍵）
> 「手把走藍牙」與「Wi-Fi AP/ESP-NOW」同晶片共存其實**有衝突**，doc 初版「實測可共存」過於樂觀。
- **ESP-NOW（連線無關、固定 ch1）完全不受影響**：全程 `haveTelem=1`，遙測穩定。
- **BLE 主動掃描會搶 2.4GHz 射頻 → Wi-Fi WPA2 關聯握手封包遺失 → STA/手機關聯不上**。
  受控實測：BLE 掃描**開** → 潛水艇 24s 都 `NO_AP_FOUND`／`stations=0`；BLE 掃描**關** →
  潛水艇 3–6s 內關聯、`stations=1`、`rssi≈-70` 穩定維持。
- **解法（已實作）= 配對視窗**：開機開掃描 `GS_PAIR_WINDOW_MS`(45s) 供手把配對/已綁定自動重連；
  **連上手把或逾時即 `enableNewBluetoothConnections(false)` 關掃描**，把射頻讓給 Wi-Fi。
  之後 Wi-Fi 關聯恢復正常（已驗證：視窗一關，潛水艇立刻關聯成功）。
- 顯式 `coex_init()/coex_enable()` + `PREFER_BALANCE` **反而讓 STA 掉線**（BT 分到更多時間片），
  故僅用 `esp_coex_preference_set(ESP_COEX_PREFER_WIFI)`（給 Wi-Fi 較高優先），不強制 coex_enable。
- 另開 `esp_wifi_set_max_tx_power(84)` 最大化 AP 覆蓋，改善弱客戶端/手機連線。
- **尚待實機驗證的唯一交互**：手把**已連線**（非掃描）時與 Wi-Fi 是否能穩定並存。
  理論上 BLE 連線（稀疏排程）遠輕於主動掃描，應可共存；需接真手把確認。
- 操作流程建議：**開機 45s 內先讓手把配對/重連**（此時手機可能連不穩）→ 視窗關閉後再連手機操作。
  若手把中途斷線，目前需重開機（或日後加 web「配對」鈕呼叫 `gamepadOpenPairing()`）。

## 與文件的差異（刻意）
> 設計規格在 `../doc/`（共用文件，描述的是初版設計）；以下為實作刻意偏離規格之處。
1. **手把改配對到「手機」，控制走 WebSocket（非 GS 藍牙）**。
   ⚠ 實測定論：**連線中的 classic-BT 手把會完全餓死 GS Wi-Fi AP**（gamepad 連線→AP stations=0、
   ROV/手機 -127、影像連不上；PREFER_WIFI 壓不住）。故手把改配對到手機，瀏覽器 Gamepad API 讀軸值
   經既有 WebSocket 上行（`web_server.cpp` WS_EVT_DATA → `gamepadSetRemote`），GS 轉 ESP-NOW。
   GS 藍牙保持閒置：`enableNewBluetoothConnections(false)`+`forgetBluetoothKeys()`+onConnected 立即
   `disconnect()` 踢掉回連手把（閒置 BT 不影響 Wi-Fi）。`gamepad.cpp` 已不讀 BP32 軸值。
   （原 Bluepad32/BTstack 入口仍保留以驅動 setup/loop；只是不接受手把連線。）
2. **建置框架由 Arduino core 2.0.x 改為 ESP-IDF（Arduino as component, core 3.x）**，
   board 由 `esp32s3usbotg` 改為 `esp32-s3-devkitc-1`，分割表改 `partitions.csv`。
3. 入口為 Bluepad32 autostart（`main.c`，編譯為 `int app_main()`，**非** autostart-arduino；
   setup()/loop() 由 BTstack run loop 經 arduino_platform 驅動 → 不可單純跳過 BT 初始化，否則 loop 不跑）。

## ⚠ 待辦 / 實機注意
1. **ESP-NOW MAC 配對**：
   - `config.h` `ROV_STA_MAC` 已填 `14:C1:9F:29:E0:B8`（ROV 的 STA MAC）。
   - 地面站 AP MAC 為 `14:C1:9F:29:EA:AD`（改 ESP-IDF 後變動），已填入 ROV 端 `config.h` `GS_AP_MAC`。
   - 開機序列埠（COM4@115200）仍會印出本機 AP MAC，可再次核對。
2. **藍牙手把配對（待實機驗證）**：
   - **只在開機後 45s「配對視窗」內可配對/重連**（之後關掃描保 Wi-Fi）；逾時未連需重開機重開視窗。
   - 首次配對時，附近其他曾配對過此手把的裝置（尤其開發 PC）藍牙須關閉，否則手把被搶連回去；
     配對成功後綁定、之後每次開機在視窗內自動重連（見 `gamepad.h` 註解、「Wi-Fi/BLE 共存」節）。
   - 供電：地面站端建議仍以 5V/2A 升壓模組穩定供電（`../doc/03` §一、`../doc/06` §一）。
3. **軸向/方向**：`gpAxisLY/RY` 已轉成文件慣例（前推為負）。若實機前後/轉向相反，
   調 `gamepad.cpp` 的負號或 `control.cpp` 映射。
   （左馬達反槳的反向已在**潛水艇** `setLeftMotor` 處理，GS 差速混控不必改。）
4. **急停 Start 鍵改 toggle**：`control.cpp` 中 Start 邊緣觸發切換 `s_estop`（按一次鎖、再按一次解），
   送 latched 值（舊版送瞬時值，配合潛水艇 EN 拉低後不復歸 → 一按永遠停）。潛水艇側已配套復歸。

## 驗證紀錄
- 韌體 `pio run -e groundstation`、`buildfs` 通過（ESP-IDF）。修補 builder 後**乾淨建置亦通過**。
- 手機網頁以 headless Chrome 灌假遙測驗證：遙測數值/徽章/RSSI 警告/導航狀態/拍照 toast 正確、
  Leaflet 初始化成功、零 JS 錯誤；航點 5m 間距判定正確。
- **2026-05-29 GS↔ROV 整合實測（COM4 GS / COM6 ROV，兩板 USB 供電）**：
  - ✅ GS AP `ROV_GS` ch1 啟動、ESP-NOW init、HTTP :80 起。
  - ✅ **潛水艇關聯成功並穩定**：`stations=1, sta0=14:C1:9F:29:E0:B8, rssi≈-70`，配對視窗關閉後 3–6s 內連上。
  - ✅ **ESP-NOW 遙測雙向通**：`haveTelem=1` 全程（潛水艇→GS）。整合鏈路 OK。
  - ⚠️ 開發 PC（TP-Link USB 無線網卡）**掃不到 ROV_GS**（鄰居 9 個 AP 看得到）→ 訊號太弱/位置遠，
    屬 PC 端物理問題，非韌體；真手機貼近 GS 應可連（潛水艇緊鄰即穩連可佐證 AP 正常）。
- **2026-05-29（二）介面/操控整修後重燒驗證**：
  - ✅ 兩端 build SUCCESS（builder patch 仍有效，GS 增量編譯）；潛水艇 COM6、GS 韌體+LittleFS COM4 燒錄成功。
  - ✅ 重置後配對視窗逾時→**潛水艇重新關聯**：`stations=2, sta0=14:C1:9F:29:E0:B8, rssi≈-54, haveTelem=1`
    （stations=2 表示同時有手機在線）。整合鏈路維持正常。
  - ✅ 前端以本機 http server + headless Chrome（`--headless=new`）直/橫向截圖驗證：
    遙測無溢出、急停橫幅、影像佔位圖、全螢幕鈕、grid 版面皆正確；`manifest.json`/JS 語法 OK。
  - **手機已能連上 `192.168.4.1`（目標達成）**；串流需潛水艇 STA 關聯穩定（弱訊號/天線待實機）。
- **2026-05-29 第三輪（轉向修正 + 天線軟體驗證）**：
  - 差速轉向實機相反 → `control.cpp computeDifferential` 對調 x 項（`rawLeft=y-x, rawRight=y+x`）。
    僅影響手動；若自動導航也反，根因為左右馬達位置對調，須在潛水艇馬達輸出層對調。
  - 天線軟體驗證（GS DIAG 同時印兩向 RSSI）：**GS聽ROV=-62、ROV聽GS=-58**，對稱、無收發不對稱；
    `bat=99%` 穩定（防彈跳生效）、`stations=1 haveTelem=1`。近距理想 ~-40，實測 ~-60＝天線偏弱（影響 range/margin）。
  - **影像全黑主因＝先前 RSSI -127（未關聯）**：未關聯連不到 192.168.4.100、且 <-75 觸發串流暫停。
    現穩定關聯 -58（串流降級 band 0：>-60 SVGA 全速），**影像應可出現**；待手機實測，並比較「手把連線時/未連線時」是否影響關聯。
- **2026-05-29 第四輪（手把改接手機 + 前端重構）**：
  - 確認「手把(BT)連線 → AP stations=0、ROV -127」；移到手機後 `btGamepad=0`、潛水艇關聯恢復
    （stations=1、ROV自量 -60、GS聽ROV -64、haveTelem=1）。
  - 控制改 WS：手機 Gamepad API → ws `{"t":"c",lx,ly,ry,b}` → `gamepadSetRemote` → ESP-NOW。失聯 500ms 中立。
  - 潛水艇加 WiFi 重連 watchdog（被 deauth 後自癒，免重置）。
  - 前端重構：常駐 HUD 狀態 + 分頁〔影像〕〔航點〕、青藍科技風、手機地圖切頁 `invalidateSize` 修復、
    狀態燈 GS/PAD/CAM。headless Chrome 直/橫向 + 地圖頁截圖驗證 OK。
  - **待手機實機**：手把配對到手機（按鍵啟用 Gamepad API）、連 ROV_GS 開網頁、確認影像+手把控制同時可用。
- **尚待實機（需手把/手機）**：
  - 藍牙手把：開機 45s 配對視窗內配對/重連，連上後關掃描；驗證連線後與 Wi-Fi 並存穩定。
  - 手機：連 `ROV_GS`（pass `rov12345`）開 `192.168.4.1` → 加到主畫面以 App 形式/全螢幕；`<img>` 連 `192.168.4.100` 看串流。
  - 航點上傳經 ESP-NOW 轉發、馬達轉向（左反槳）、急停 toggle 解鎖。
