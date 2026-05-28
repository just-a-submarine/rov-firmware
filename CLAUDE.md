# CLAUDE.md — ROV 潛水艇專案（根目錄總覽）

電子學（二）期末 ROV 水下載具專案。系統分三部分：**潛水艇 (ROV)**、**地面站 (Ground Station)**、
**手機操作網頁**（網頁由地面站 LittleFS 提供）。本檔是給 AI Agent / 開發者的進入點。

> 🚨 **潛水艇與地面站是兩個獨立子專案，建置框架不同，切勿混淆。**
> 動手前先確認你在哪一邊（看路徑、看 `platformio.ini`），再讀對應的 `CONTEXT.md`。

---

## 目錄結構與路徑（重要）

| 路徑 | 角色 | 維護檔 |
|------|------|--------|
| `doc/` | **共用設計規格（參考資料）**，描述整套系統（潛水艇 + 地面站 + 網頁） | `03`~`06`（見下） |
| `Submarine-Code/` | **潛水艇 (ROV)** 韌體 | `Submarine-Code/CONTEXT.md` |
| `Ground-Station-Code/` | **地面站** 韌體 + 手機網頁（`data/www/`） | `Ground-Station-Code/CONTEXT.md` |
| `CLAUDE.md` | 本檔，專案總覽與維護規範 | — |

根目錄**只保留 `doc/` 與本 `CLAUDE.md`**；其餘皆在兩個子專案內。

### `doc/`（共用參考資料，潛水艇與地面站皆引用）
- `03-電力與硬體接線.md` — 電池、GPIO 分配、各元件接線、續航。
- `04-通訊架構.md` — ESP-NOW、Wi-Fi、WebSocket、航點上傳、地圖。
- `05-影像與錄影系統.md` — OV5640、串流模式、RSSI 降級、AVI 錄影。
- `06-控制與導航.md` — 手把、差速混控、PWM、PID 深度、Potential Field 導航。
- 編號從 03 起（無 01/02），刻意保留。

---

## 兩個子專案速覽（別搞混）

### 🟦 潛水艇 ROV — `Submarine-Code/`
- 主板 GOOUUU **ESP32-S3-CAM N16R8**（OV5640、16MB Flash / 8MB OPI PSRAM）。
- 框架：**PlatformIO + pioarduino**（Arduino-ESP32 **core 3.x**）；佈局 `src/` + `include/`。
- env：`goouuu_esp32s3cam`（正式，純 STA 連地面站）、`standalone`（單機測試，自開 AP `ROV_TEST`）。
- 偵錯/燒錄：正式版用 **原生 USB COM6**（`ENABLE_GPS=1` 時 CH340 COM4 失效）。
- 職責：馬達控制、感測器（羅盤/深度/電流/GPS）、MJPEG 串流 + SD 錄影、ESP-NOW 收發、導航/PID。
- 細節見 `Submarine-Code/CONTEXT.md`。

### 🟩 地面站 — `Ground-Station-Code/`
- 板 `esp32-s3-devkitc-1`。
- 框架：**PlatformIO + ESP-IDF**（平台釘 pioarduino `54.03.21-2`，Arduino 作為 component / core 3.x）；
  佈局 `main/`（**非** `src/`）。⚠ **必須用原生 PowerShell 建置**（非 MSYS/Git-Bash）。
- env：`groundstation`，COM4 @115200。
- 職責：Wi-Fi 純 AP、WebSocket 推遙測、HTTP 收航點並經 ESP-NOW 轉發、**Bluepad32 藍牙 Xbox 手把**、
  差速混控。**不碰影像**（手機 `<img>` 直連 ROV `192.168.4.100`）。
- 細節見 `Ground-Station-Code/CONTEXT.md`。

### 兩端協同
- ESP-NOW 雙向：地面站送控制/航點、收遙測。MAC 已互填
  （ROV STA `14:C1:9F:29:E0:B8`、GS AP `14:C1:9F:29:EA:AD`）。
- **封包格式必須逐位元組一致**：`Submarine-Code/include/packets.h` ⟷ `Ground-Station-Code/main/packets.h`。

---

## 文件維護規範（每次有更動務必同步）

1. **`doc/` 是參考資料（設計規格），要及時更新對齊最新狀況。**
   修改後更新該檔頁尾「文件版本 / 最後更新」並註明改動。`doc` 為潛水艇與地面站共用，
   改動前確認影響的是哪一端或兩端。
2. **實作刻意偏離 `doc/` 時，不一定改 doc，而是記到該子專案 `CONTEXT.md` 的「與文件的差異」**
   （例：地面站手把已由有線 USB 改藍牙 Bluepad32、改 ESP-IDF；潛水艇 MJPEG 改 `esp_http_server`）。
   doc 內明確錯誤（如腳位筆誤）則直接修 doc 並升版。
3. **`CONTEXT.md` 兩份各自獨立維護**：潛水艇放潛水艇的、地面站放地面站的，不要互相污染。
   每次任務完成後更新對應 `CONTEXT.md`（硬體/MAC/COM 埠/待辦/驗證進度/踩坑）。
4. **本 `CLAUDE.md` 也要維護**：路徑、框架、env、子專案職責有變時同步更新。

## 動手前檢查清單
- [ ] 我在潛水艇還是地面站？（看路徑與 `platformio.ini`）
- [ ] 已讀對應 `CONTEXT.md` 與相關 `doc/`？
- [ ] 改到通訊封包了嗎？兩端 `packets.h` 是否仍逐位元組一致？
- [ ] 地面站建置：是否用原生 PowerShell？env 是否帶 `-e groundstation`？
- [ ] 完成後是否更新了 `CONTEXT.md` / 視情況更新 `doc/` 與本檔？
