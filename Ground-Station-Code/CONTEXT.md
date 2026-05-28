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

## 程式結構（`main/`，ESP-IDF component）
- `main.c` — Bluepad32/BTstack 進入點（`initBluepad32()`）；autostart 後轉入 Arduino `setup()/loop()`。
- `sketch.cpp` — Arduino `setup()/loop()`：初始化順序（依 `../doc/04` §七）+ loop 節流（控制 ~100Hz、遙測推送 ~6.7Hz）。
  **不呼叫 `Serial.begin()`**（會搶走 IDF console）；全程用 `printf` 走 IDF console（UART0 → COM4）。
- `net_wifi.*` — `setupWifi_GS()` 純 AP + 關省電。
- `espnow_link.*` — ESP-NOW 收發、`sendControl()`、`sendWaypointsToROV()`、`takeLatestTelemetry()`。
- `web_server.*` — ESPAsyncWebServer + WebSocket `/ws`、`POST /api/waypoints`、靜態檔（LittleFS `/www`）。
- `gamepad.*` — Bluepad32（BTstack）藍牙手把；對外只給軸值/按鍵（軸值已轉文件慣例：前推為負、右為正）。
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
`index.html` / `app.js` / `style.css` + 本地 Leaflet（`leaflet.js` / `leaflet.css`，未 gzip）。
地圖點擊新增航點（間距 ≥5m 防呆）、即時遙測、串流徽章/RSSI 警告/拍照 toast/導航狀態、
WebSocket 指數退避重連、tile 載入失敗降級灰網格。

## 與文件的差異（刻意）
> 設計規格在 `../doc/`（共用文件，描述的是初版設計）；以下為實作刻意偏離規格之處。
1. **手把走藍牙（Bluepad32/BTstack），非 `../doc/06` §一的有線 USB HID**。
   doc 初版為避 2.4GHz 競爭選有線 USB；改版實測 ESP-IDF Wi-Fi/BLE coexistence 可與
   ESP-NOW/Wi-Fi 共存，改用原廠 Xbox Series 藍牙手把，省去 OTG/XInput 解析。
2. **建置框架由 Arduino core 2.0.x 改為 ESP-IDF（Arduino as component, core 3.x）**，
   board 由 `esp32s3usbotg` 改為 `esp32-s3-devkitc-1`，分割表改 `partitions.csv`。
3. 入口為 Bluepad32 autostart（`main.c`）銜接 Arduino `setup()/loop()`（`sketch.cpp`）。

## ⚠ 待辦 / 實機注意
1. **ESP-NOW MAC 配對**：
   - `config.h` `ROV_STA_MAC` 已填 `14:C1:9F:29:E0:B8`（ROV 的 STA MAC）。
   - 地面站 AP MAC 為 `14:C1:9F:29:EA:AD`（改 ESP-IDF 後變動），已填入 ROV 端 `config.h` `GS_AP_MAC`。
   - 開機序列埠（COM4@115200）仍會印出本機 AP MAC，可再次核對。
2. **藍牙手把配對（待實機驗證）**：
   - 首次配對時，附近其他曾配對過此手把的裝置（尤其開發 PC）藍牙須關閉，否則手把被搶連回去；
     配對成功後綁定、之後自動重連（見 `gamepad.h` 註解）。
   - 供電：地面站端建議仍以 5V/2A 升壓模組穩定供電（`../doc/03` §一、`../doc/06` §一）。
3. **軸向/方向**：`gpAxisLY/RY` 已轉成文件慣例（前推為負）。若實機前後/轉向相反，
   調 `gamepad.cpp` 的負號或 `control.cpp` 映射。

## 驗證紀錄
- 韌體 `pio run -e groundstation`、`buildfs` 通過（ESP-IDF）。
- 手機網頁以 headless Chrome 灌假遙測驗證：遙測數值/徽章/RSSI 警告/導航狀態/拍照 toast 正確、
  Leaflet 初始化成功、零 JS 錯誤；航點 5m 間距判定正確。
- 與 ROV 整合（ESP-NOW 遙控/遙測/航點）、藍牙手把實機，尚待硬體齊備測試。
