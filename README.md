# ROV 潛水艇專案（rov-firmware）

電子學（二）期末 ROV 水下載具專案。整套系統由三部分組成：

- **潛水艇 (ROV)** — 水下載具，負責推進、感測、影像串流與錄影、自動導航。
- **地面站 (Ground Station)** — 岸上控制端，Wi-Fi AP + 遙測轉發 + 藍牙手把。
- **手機操作網頁** — 由地面站提供，顯示遙測/地圖/影像、設定航點。

> 同一個倉庫包含**兩個獨立的 ESP32-S3 韌體子專案**（潛水艇、地面站），
> 建置框架不同，請勿混淆。動手前先讀 [`CLAUDE.md`](CLAUDE.md) 與對應子專案的 `CONTEXT.md`。

---

## 系統架構

```
   Xbox 手把 ──藍牙──▶ 手機瀏覽器（Gamepad API 當控制器）
                          │
                          ├─ WebSocket ──▶ 地面站 ESP32-S3（AP 192.168.4.1）
                          │     • 控制上行（搖桿/按鍵）
                          │     • 遙測下行（~5–10Hz）
                          ├─ HTTP POST（航點）──▶ 地面站
                          └─ MJPEG <img>（影像直連）─────────────┐
                                                                  ▼
   地面站 ──ESP-NOW（控制/航點 ↕ 遙測）──▶ 潛水艇 ESP32-S3（STA 192.168.4.100）
                                            └─ OV5640 MJPEG Server（esp_http_server :80/stream）
```

- **控制來源＝手機**：Xbox 手把藍牙配對到**手機**，手機網頁用 Gamepad API 讀取，經 WebSocket 上行到地面站，
  地面站再以 ESP-NOW 轉發給潛水艇。地面站 Bluepad32 僅保留 BTstack 入口、**不接受手把藍牙連線**
  （手把直連地面站會餓死 Wi-Fi AP）。
- 地面站**不碰影像**：手機 `<img>` 直連潛水艇 `192.168.4.100`，地面站零負擔。
- 兩端 ESP-NOW 封包格式**必須逐位元組一致**
  （`Submarine-Code/include/packets.h` ⟷ `Ground-Station-Code/main/packets.h`）。

---

## 倉庫結構

```
.
├── doc/                     共用設計規格（參考資料，潛水艇與地面站皆引用）
│   ├── 03-電力與硬體接線.md
│   ├── 04-通訊架構.md
│   ├── 05-影像與錄影系統.md
│   └── 06-控制與導航.md
├── CLAUDE.md                專案總覽 + 文件維護規範（AI/開發者進入點）
├── README.md                本檔
├── Submarine-Code/          🟦 潛水艇 (ROV) 韌體 — PlatformIO + pioarduino (Arduino core 3.x)
│   └── CONTEXT.md           開發交接文件（潛水艇）
└── Ground-Station-Code/     🟩 地面站韌體 + 手機網頁 — PlatformIO + ESP-IDF + Bluepad32
    └── CONTEXT.md           開發交接文件（地面站）
```

---

## 建置與燒錄

### 🟦 潛水艇 — `Submarine-Code/`
- 主板：GOOUUU ESP32-S3-CAM N16R8（OV5640、16MB Flash / 8MB OPI PSRAM）。
- 框架：**PlatformIO + pioarduino**（Arduino-ESP32 core 3.x）。

```bash
cd Submarine-Code
pio run -e goouuu_esp32s3cam              # 正式版（純 STA 連地面站）
pio run -e standalone                     # 單機測試版（自開 AP ROV_TEST）
pio run -e goouuu_esp32s3cam -t upload --upload-port COM6   # 燒錄（原生 USB）
```
> `ENABLE_GPS=1` 時 GPIO43/44 由 GPS 佔用、CH340(COM4) 偵錯失效，正式版偵錯/燒錄一律用**原生 USB COM6**。

### 🟩 地面站 — `Ground-Station-Code/`
- 板：`esp32-s3-devkitc-1`。
- 框架：**PlatformIO + ESP-IDF**（平台釘 pioarduino `54.03.21-2`，Arduino 作為 component）。
- ⚠ **必須用原生 PowerShell 建置**（非 MSYS/Git-Bash），否則 `idf_tools.py` 會拒跑。

```powershell
cd Ground-Station-Code
pio run -e groundstation                  # 編譯韌體
pio run -e groundstation -t upload        # 燒韌體（COM4）
pio run -e groundstation -t buildfs       # 打包手機網頁（data/www → LittleFS）
pio run -e groundstation -t uploadfs      # 上傳網頁（韌體與網頁兩者都要做）
```

---

## 依賴還原（本倉庫已排除以保持輕量）

`.gitignore` 排除了建置產物與第三方依賴（`.pio/`、`managed_components/`、`Ground-Station-Code/components/`）。
首次 clone 後需還原：

- **潛水艇**：`pio run` 會依 `platformio.ini` 的 `lib_deps` 自動安裝函式庫
  （清單見 `doc/06` §十二）。
- **地面站**：
  - `managed_components/` — IDF component manager 依 `dependencies.lock` 於建置時自動下載。
  - `Ground-Station-Code/components/` — 手動 vendored 的依賴框架
    （arduino-esp32、Bluepad32、BTstack、ESPAsyncWebServer、AsyncTCP、ArduinoJson 等），
    需自行放入該目錄。**Bluepad32 僅作 BTstack 入口維持 setup/loop**；手把實際配對到**手機**，
    控制走手機網頁 Gamepad API → WebSocket（地面站不接受手把藍牙連線，避免餓死 Wi-Fi AP）。

---

## 目前狀態

- 🟦 潛水艇：編譯/燒錄/開機驗證通過；感測器自檢、SD、單機 AP 均 OK。相機 MJPEG 串流已實測出畫面
  （VGA@10MHz、~6–7fps；20MHz 因相機 FPC 排線訊號完整性會出垂直條紋故降頻）；含開機/執行期自癒。
  待實機：GPS 定位、馬達轉向、錄影、與地面站整合。
- 🟩 地面站：韌體 + `buildfs` 編譯通過；手機網頁以假遙測驗證 UI/Leaflet/航點正確；
  控制鏈改手機 Gamepad → WebSocket，潛水艇關聯 + ESP-NOW 遙測已實測通。
  待實機：手把配對到手機後整體操控、與潛水艇馬達整合。

詳細進度見各子專案 `CONTEXT.md`。
