# TODO — 第三輪：手把改接手機 + 前端重構（2026-05-29）

## 根因確診（已驗證）
手把(classic BT)連線中 → GS AP 餓死（gamepad=1 → stations=0、ROV -127、影像連不上）。
閒置 BT 不影響 Wi-Fi（-58 正常）。PREFER_WIFI 壓不住。**決策：手把改接手機**。

## E. 後端（GS 韌體）— 控制改走 WebSocket
- [x] gamepad.cpp：拒絕並斷開手把 BT 連線（forgetKeys+disableConn+onConnected disconnect）；
      控制狀態由 WS 餵入（gamepadSetRemote），gpAxis/gpButton/gamepadConnected 讀 WS 值 + 500ms 逾時失效
- [x] web_server.cpp：WS_EVT_DATA 收手機控制 JSON → gamepadSetRemote
- [x] config.h：GP_WS_TIMEOUT_MS=500（失聯中立）
- [x] 潛水艇 WiFi 重連 watchdog（被 deauth 後自癒）
- [x] 驗證：btGamepad=0 → 潛水艇關聯恢復（stations=1、ROV自量 -60、haveTelem=1）

## F. 前端重構（手機優先 + 科技感）
- [x] 手把：Gamepad API 輪詢(~25Hz) → WS 上行（配對到手機）
- [x] 分頁佈局：常駐 HUD（狀態監控）+〔影像〕〔航點〕分頁
- [x] 手機地圖修復：切頁 map.invalidateSize()（+ resize）
- [x] 手機適配：dvh/safe-area、大分頁鈕、直/橫向
- [x] 科技感：青藍霓虹、等寬發光數字、HUD 角框、微網格、狀態燈
- [x] headless Chrome 直/橫/地圖頁截圖驗證 + uploadfs

## G. 影像鏈路除錯（2026-05-29 第五輪）
- [x] 分層定位：關聯 OK(-58) → GS→ROV :80 探測「連得上但 0 bytes」→ 相機影格數=0
- [x] 確認 client↔client 非問題（GS 連得到 ROV :80）
- [x] 相機：PID=0x5640（OV5640 偵測 OK）但 fb_get 逾時回 null＝零影格；20/24MHz 皆然
- [x] 修 LEDC 衝突（相機 XCLK 改 ch7/timer3，原 ch0 與左馬達衝突）
- [x] 補呼叫 wifiReconnectWatchdog（先前漏呼叫）

## ✅ 影像黑真因＝相機偶發開機壞狀態（已修，非硬體，2026-05-30）
- [x] 推翻先前「硬體故障」誤判：乾淨重開後相機 ~11fps 穩定 45s、GS 探測 HTTP 200 + 收 195397 bytes
- [x] 腳位對照 GOOUUU 實板 pinout 圖逐一確認 100% 正確（D0-D7=Y2-Y9=11,9,8,10,12,18,17,16）
- [x] camera_stream.cpp 加**開機自癒**（抓不到首幀→deinit+init 重試 4 次）＋ 執行期掉幀逾時 reinit（`g_camConfig`）
- [x] XCLK→10MHz（穩定 ~11fps；20/24MHz 之前「失敗」其實是讀到死開機，非頻率）
- [x] build + flash COM6 驗證：本次開機健康、seq 持續推進
- [ ] 手機實際目視確認畫面（韌體鏈路已通）

## ✅ 航點分頁沒反應 + 沒畫面（已修，2026-05-30）
- [x] 真因：`<img src=stream>` 寫在 HTML + `window.load` 啟動；MJPEG 無限連線 → load 永不觸發 → app 完全不初始化
- [x] index.html 移除 `<img>` 的 src（改 initStream 設）；app.js 改 `DOMContentLoaded` 啟動
- [x] web_server.cpp `setCacheControl("no-cache")`（避免改版後手機載舊快取）
- [x] headless Chrome --dump-dom 驗證：boot 有跑(stream src 被 JS 設)、點航點→pane active、#map→leaflet-container
- [x] GS rebuild firmware + uploadfs（COM4）

## ✅ 手機載入慢（已修，2026-05-30）
- [x] 主因：leaflet.js 147KB 未壓縮 + `<head>` 同步載入擋住 app.js（WS/手把/影像初始化全等它）
- [x] Leaflet 改延遲載入（首次開航點分頁才注入 css/js）→ 移出首屏關鍵路徑
- [x] gzip leaflet.js(147→42KB)/leaflet.css(14.8→3.5KB)；serveStatic `_tryGzipFirst` 預設 true 自動送 .gz
- [x] uploadfs 已含新前端（gzip 在 uploadfs 之前產生）；app.js node --check 通過

## 仍待實機（硬體限定）
- [ ] 手把配對到手機（藍牙 + 按鍵啟用 Gamepad API）→ 連 ROV_GS 開網頁；確認手把控制可用
- [ ] 馬達轉向（左反槳/左推左轉）、急停 toggle、電量 R_int 校正、天線 0Ω 檢查

---

# （封存）TODO — 前端 App 化 + 操控/感測修正（2026-05-29 手機實測後）

> 手機已能連上地面站（目標 #1 達成）。依實測畫面與回饋修整。

## A. 前端（地面站 data/www）
- [x] PWA：manifest.json + icon.svg + apple/mobile meta → 可加到桌面、standalone
- [x] 全螢幕：requestFullscreen（Android 走 http 也有效）隱藏瀏覽器網址列
- [x] service worker（best-effort；http 非安全來源不註冊，無妨）
- [x] 遙測列改 grid：修「-127 dBm」溢出/裁切；座標/警告整列
- [x] 影像中斷顯示斜紋佔位圖 + 自動重連（補上 onStreamError）；video-wrap 加 min-height

## B. 潛水艇韌體（Submarine-Code）
- [x] 馬達轉向：左馬達反槳 → `LEFT_MOTOR_INVERT`，於 setLeftMotor 反向（涵蓋手動+導航）
- [x] 急停恢復：applyControl 偵測 estop→正常的轉換，重新 enableMotors()
- [x] 電量：內阻補償回推 OCV + 18650 放電曲線查表 + 只降緩升
- [x] 訊號：STA 加 esp_wifi_set_max_tx_power(84)

## C. 地面站韌體（Ground-Station-Code）
- [x] 急停改 toggle：Start 邊緣觸發切換（再按一次解鎖），送 latched 值
- [x] 遙測 JSON 加 estop（手機顯示急停橫幅）

## D. 建置/燒錄/驗證
- [x] 潛水艇 build + flash COM6；地面站 build + flash COM4 + uploadfs（×2，含 min-height 修正）
- [x] 開機 log 驗證：重置後潛水艇重新關聯 stations≥1 rssi≈-54 haveTelem=1
- [x] 前端 headless Chrome 直/橫向截圖驗證版面

## 第二輪（2026-05-29 二次回饋）
- [x] 差速轉向方向相反 → GS computeDifferential 對調 x 項（y-x, y+x）
- [x] 潛水艇加 WIFI_DIAG；GS DIAG 印 ROV 自量 RSSI + 電量（天線軟體驗證）
- [x] 天線量測：GS聽ROV -62 / ROV聽GS -58，對稱、關聯正常、bat 99% 穩定
  → 影像黑主因是先前 -127(未關聯)，非衰減；現 -58 串流 band 0 應可出畫面

## 仍待實機（硬體限定，無法自動驗）
- [ ] 影像：手機重連確認出畫面；比較「手把連線/未連線」是否影響關聯
- [ ] 馬達實際轉向（離水低工率）：左反槳前進一致 + 左推=左轉
- [ ] 急停 toggle：Start 按一次停、再按一次恢復
- [ ] 天線 0Ω：兩板靠 ~20cm 重讀 RSSI（理想 ~-40；若 -65↓＝天線偏弱）；檢查板載/IPEX 切換
- [ ] 電量 R_int 校正（BATTERY_IR_OHM 0.15Ω 粗估）；手把連線時與 Wi-Fi 並存

## 診斷答覆（已於對話回覆，非程式碼）
- 4a 天線 0Ω：板載/IPEX 二選一，實機檢查；韌體先開最大 TX
- 4c 訊號：潛水艇 STA 看 GS AP 的 RSSI；-127=未關聯
- 5 功耗：INA260 power 暫存器（實測 V×I），非假設 12V

---

# （封存）TODO — ROV 地面站↔潛水艇整合

## 已完成（2026-05-29，本機實測）
- [x] 修好地面站 ESP-IDF 乾淨建置（patch builder：utils.c.o 撞名 + MAX_PATH）
- [x] GS AP `ROV_GS` ch1 啟動、ESP-NOW init、HTTP :80
- [x] 潛水艇關聯 GS AP 並穩定（stations=1, rssi≈-70）
- [x] ESP-NOW 遙測潛水艇→GS 雙向通（haveTelem=1）
- [x] 診斷 Wi-Fi/BLE 共存衝突，實作手把「配對視窗」（開機 45s，連上/逾時關掃描）
- [x] coex 偏好 WIFI + 最大 AP TX 功率

## 待實機（需手把 / 手機，本機無法代理）
- [ ] 手把：開機 45s 內配對/重連（先關開發 PC 藍牙）；連上後關掃描
- [ ] **手把已連線時與 Wi-Fi 是否穩定並存**（唯一未測交互；理論 BLE 連線遠輕於掃描，應可）
- [ ] 手機：連 `ROV_GS`/`rov12345` → `http://192.168.4.1` 看遙測網頁；`<img>` 連 `192.168.4.100` 看串流
- [ ] 開發 PC 掃不到 `ROV_GS` 是訊號/位置問題（USB 網卡太弱），改用手機就近測

## 可選後續
- [ ] web 加「配對手把」鈕 → 呼叫 `gamepadOpenPairing()`（手把中途斷線可重連，免重開機）
- [ ] 正式出貨把 `main/diag.h` 的 `GS_DIAG_STA` 設 0（移除每 3s station 印出）
- [ ] 把 builder patch 做成可重套的腳本/extra_script（免換機重裝平台後手動修）

## 回顧
本次核心阻塞是地面站「乾淨建置壞」+「Wi-Fi/BLE 共存」。前者靠修 PlatformIO builder（短雜湊 obj 路徑）
一次解掉撞名與 MAX_PATH；後者證實 BLE 主動掃描破壞 Wi-Fi 關聯，以「配對視窗」錯開。
潛水艇↔地面站整合鏈路（關聯 + ESP-NOW 遙測）已實測通。手把與手機需實機收尾。
