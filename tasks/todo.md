# rov-firmware todo
最後更新：2026-11-19
（原 `Submarine-Code/CONTEXT.md`、`Ground-Station-Code/CONTEXT.md` 已併入本檔。總覽見 `CLAUDE.md`／README；踩雷細節與方法論見 `tasks/lessons.md`。）

## 兩端共通（交接）
- MAC：ROV STA `14:C1:9F:29:E0:B8`；GS AP `14:C1:9F:29:EA:AD`（GS 開機以 `esp_iface_mac_addr_set` 在 `esp_wifi_init()` 前釘死 → 換 GS 板不必改 ROV；別用 `esp_wifi_set_mac()`，會打亂 Arduino WiFi 狀態機）。退役舊 GS 板勿同時上電（撞 MAC）。
- **封包兩端 `packets.h` 必須逐位元組一致，改了就兩端一起重燒**：ControlPacket 16B（含 `epochS` 手機授時、`uint8_t photoSeq` 取代舊 `bool takePhoto`；新舊混燒會 100Hz 狂拍）；TelemetryPacket 43B（含 `ledOn`、`headingDeg/magX/magY`，`msgType` 維持最後一位元組）。
- 頻道 ch1；要換 ch6/11 須兩端 `config.h` `WIFI_CHANNEL` 同改。ESP-NOW `peer.channel=0` 跟隨本機頻道 → ROV 沒關聯 AP 時 `haveTelem=0` 與 `stations=0` 是同一件事。
- RF：2026-06-06 換新 GS 板後近距 RSSI −36/−27（舊板 −71/−69），「時好時壞」根因＝舊 GS 天線，已結案。GS AP 鎖 HT20、兩端 max TX power、關省電已用盡韌體槓桿。外接天線須把 0Ω 電阻移到 IPEX 側才會通，且要伸出水面直立；沒切之前外接比板載更差。
- COM 埠會變（近期 ROV 原生 USB=COM8、GS=COM9）；不符就 `--upload-port COMx`。S3 原生 USB 開序列埠會 SoC reset；要乾淨重啟用 `esptool --port COMx --after hard_reset run`。
- 🚨 git 改動後燒錄前先 `-t clean` 全重編（mtime 會騙增量建置），燒完核對開機 log 的 `Compile time`＋`App version`。

## 潛水艇 `Submarine-Code/`
- 主板 GOOUUU ESP32-S3-CAM N16R8；pioarduino（Arduino core 3.x，官方 espressif32 缺 `ledcAttach`／`esp_now_recv_info_t`）；16MB、`huge_app.csv`、`qio_opi`、USB CDC On Boot。
- env：`goouuu_esp32s3cam`（正式，純 STA）、`standalone`（`-DSTANDALONE_TEST=1`，自開 AP `ROV_TEST`，`http://<rov-ip>/stream`）。
- `ENABLE_GPS=1` 時 GPS 用 UART1@43/44（用 UART0 會與 console 衝突 abort）→ CH340 偵錯失效，正式版偵錯/燒錄走原生 USB。感測器狀態行要在 `gpsSerial.begin` 前印。
- 任務優先級：`controlTask`(Core1, 100Hz, pri5) ≥ `networkTask`(Core0, 5Hz, pri4) > 相機 HTTP（`core_id=0, pri2`）> `streamTask`(擷取, pri1，唯一影格擷取者)。
- 初始化：`setupComms()` 必須在 `setupCamera()` 前（httpd 需要 lwIP）。
- 與 doc 刻意差異：MJPEG 用 ESP-IDF `esp_http_server`（非 ESPAsyncWebServer）；垂直馬達 41/42（doc 舊版 44/46 為筆誤，v2.7 已更正）；QMC5883P 用 `getGaussField()`。
- 相機：`CAM_PIN_PWDN/RESET=-1` → 冷啟動非決定性 wedge；現行＝`g_camDead` 期間背景持續 `reinitCamera()`（退避 1→5s）自癒，不用 `esp_restart`。某些壞狀態只有實體斷電能清；`gdma_disconnect`＋`fb_get` null 多半是 FPC 接觸 → 先重插壓緊排線。治本＝PWDN 或 RESET 焊線到空閒 GPIO。XCLK 10MHz、LEDC ch7/timer3（避開馬達 ch0–5）、`CAM_VFLIP=1`（若左右也反改 `CAM_HFLIP=1`）。相機死時 `/stream` 回 500，手機每 3s 重連。
- 相機調參現值：只動 `brightness +2`（已上限）／`contrast −1`，其餘全交原廠自動；setup 與 reinit 都要套。別再整包覆蓋 setter（會更暗）；要更亮只能單獨 `set_ae_level(+1~+2)`。
- 錄影：AVI idx1 偏移 `position-(g_moviOffset+8)`；停止時以實測 fps 回補 header（防快轉）。錄影模式下 LB 不拍照。拍照 `takePhotoInstant` 直接存目前串流影格，只有真寫檔成功才 ack。檔案時間：手機 `ts`→`epochS`→`settimeofday`，`TZ=CST-8`。
- 羅盤已校正（2026-05-31）：`g_offsetX/Y=-0.237/-0.135`、`g_scaleX/Y=1.0484/0.9559`（在 `sensors.cpp`）。
- 其他已上線：左馬達反槳 `LEFT_MOTOR_INVERT=1`（在 `setLeftMotor`）；急停解除時重新 `enableMotors()`；WiFi 斷線 watchdog 每 5s 重連；電量＝內阻補償＋18650 曲線查表＋緩升防跳。
- INA260 取電自電池匯流排：桌面純 USB 無電池時 init 失敗、cur/power/bat 顯示 0 是「沒量」不是故障。

## 地面站 `Ground-Station-Code/`
- 職責：純 AP（`192.168.4.1`，SSID `ROV_GS`／pass `rov12345`）、WS 收手機控制＋推遙測（~6.7Hz）、`POST /api/waypoints` 分塊（每塊 14 點）經 ESP-NOW 轉 ROV、差速混控。不碰影像（手機 `<img>` 直連 ROV `192.168.4.100`）。
- 控制來源＝手機（手把 USB 接手機 → Gamepad API／螢幕虛擬搖桿 → WS `{t:'c',lx,ly,ry,b,ph,ts}`）。**GS 藍牙完全停用**：`main.c` 不跑 btstack/uni_init、`esp_bt_controller_mem_release` 後直接 `arduino_bootstrap()`（連線中的 BT 手把會餓死 AP）。sdkconfig 仍 `CONFIG_BT_ENABLED=y`（未回收那塊 flash）。
- 建置：PlatformIO + ESP-IDF（`framework=espidf`，`src_dir=main`，board `esp32-s3-devkitc-1`，env `groundstation`）；平台釘 pioarduino `54.03.21-2`（不要降版）。**必須原生 PowerShell**，且每條指令前 `$env:PYTHONUTF8="1"`（cp950 會讀爆）。不要在 `platformio.ini` 加 `build_flags`（改寫在原始碼／`diag.h`／CMake）。不要在 `sketch.cpp` 呼叫 `Serial.begin()`。
- 燒錄：`pio run -e groundstation -t upload` ＋ `-t uploadfs`（網頁在 LittleFS，兩者都要；分割表 app 3MB＋LittleFS 4MB＋coredump 64KB）。
- **Builder 修補（在 `~/.platformio`，不在 repo，重裝平台/換機要重套）**：`platforms/espressif32/builder/frameworks/espidf.py` 的 `compile_source_files()`，非框架來源 obj 改成「來源目錄短雜湊 + basename」，同時解 `utils.c.o` 撞名與 MAX_PATH：
  `dir_hash = hashlib.md5(fs.to_unix_path(os.path.dirname(src_path)).lower().encode()).hexdigest()[:8]`；`obj_path = os.path.join(obj_path, dir_hash, os.path.basename(src_path))`（檔頭 `import hashlib`）。不要用 subst/junction 繞路。
- 手機網頁 `data/www/`：`DOMContentLoaded` 啟動、`<img>.src` 由 JS 設（MJPEG 讓 `window.load` 永不觸發）；Leaflet 延遲載入且用 `.gz`（改 leaflet 要重 gzip；app.js/style.css/index.html 不 gzip）；`serveStatic` `no-cache`。
- 離線地圖：CARTO Voyager 圖磚 z15–17 共 166 張在 `data/www/tiles/`，`tools/fetch_tiles.py` 產生（OSM 官方禁批次下載）；四場域外雙溪／大湖／碧湖／基隆河；`maxBounds=tileCoverageBounds(...)`、viscosity 1。地圖艇位＝ROV 的 GPS（非手機位置），航向用真羅盤，`magX/Y`=0,0 退回 GPS 位移；🧭 校準浮層可算出 ROV 校正常數。
- 拍照走單調序號 `photoSeq`（連點不漏不合併）＋本地快門閃；燈／錄影走 `PULSE_MS=120ms` 脈衝 bit。差速 2026-06-07 改回教科書式 `rawLeft=y+x, rawRight=y-x`（與 ROV 導航一致）。急停 Start 為 toggle（latch）。
- 與 doc 刻意差異：控制走 WS 不用藍牙；改 ESP-IDF；地圖離線圖磚；磁偏角尚未補。

## 待辦
- [ ] ROV 馬達實測（離水、低工率）與急停；目視確認「上推＝前進、左推＝左轉」，若仍反＝馬達實體對調，改在 ROV 輸出層處理。
- [ ] 與地面站整機整合：手把接手機實操、航點上傳 → ESP-NOW 轉發 → 自動導航。
- [ ] GPS 模組 #1 確認壞（TX 不驅動、`charsProcessed=0`），換新模組再測。
- [ ] 待使用者回測：錄片時長正常不快轉、拍照按一下即拍。
- [ ] 羅盤：`HEADING_OFFSET_DEG`（裝設水平旋轉角＋磁偏角，台北西偏約 4.5°）加在 `getCorrectedHeading()` 末端；`motorComp` 先測「馬達關 vs 滿油門」漂移再決定要不要做。羅盤要裝平、遠離馬達與電流線。
- [ ] `navigation.cpp` PID 參數（起始 2.0/0.5/0.1）實機調整；Geofence 邊界（目前只用吸引力）。
- [ ] 虛擬面板缺急停解鎖鈕（先用手把 latch 再切純觸控會解不開）。
- [ ] 若影像仍因頻寬卡頓：`streamTask` 發佈幀率上限 ≤15fps（先測再加）。

## 已知問題／刻意不處理
- SD 掛載失敗時可對調 `SD_CLK/CMD`（39/38）再試。
- 地圖不自動依 GPS 切場域、不顯示操作者位置（未來增強）。
- 相機 PWDN/RESET 未接屬硬體限制，韌體只能自癒不能根治。
