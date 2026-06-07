'use strict';

// =============================================================================
//  ROV 地面站手機端 App
//  - WebSocket：接收遙測（GS→手機）+ 上行手把控制（手機→GS，Gamepad API）
//  - 分頁：影像 / 地圖航點；常駐 HUD 狀態列
//  - 手把改配對到「手機」，瀏覽器讀軸值經 WS 送 GS（GS 不再用藍牙，避免餓死 Wi-Fi AP）
// =============================================================================

const GS_HOST = location.host || '192.168.4.1';
const MIN_WP_DIST_M = 5;
const STREAM_SRC = 'http://192.168.4.100:80/stream';

// 四個目標水域（離線圖磚已預打包，見 tools/fetch_tiles.py 的 SITES）。
// 現場無對外網路，底圖只涵蓋這四處。bounds = [[latMin,lonMin],[latMax,lonMax]]，
// 對齊 fetch_tiles 的下載 bbox，用來把地圖平移/縮放鎖在「有圖磚」的範圍內（避免黑屏）。
const SITES = {
  waishuangxi: { name: '外雙溪', center: [25.0985, 121.52025], zoom: 16,
                 bounds: [[25.0955, 121.5095], [25.1015, 121.5310]] },
  dahu:        { name: '大湖',   center: [25.08275, 121.6044], zoom: 16,
                 bounds: [[25.0775, 121.6000], [25.0880, 121.6088]] },
  bihu:        { name: '碧湖',   center: [25.08225, 121.58375], zoom: 17,
                 bounds: [[25.0795, 121.5805], [25.0850, 121.5870]] },
  keelung:     { name: '基隆河', center: [25.07525, 121.5615], zoom: 16,
                 bounds: [[25.0700, 121.5520], [25.0805, 121.5710]] },
};
const MAP_MIN_ZOOM = 15;   // 有圖磚的最低 zoom；不准再縮小（杜絕縮成世界圖→留白/黑屏）
const MAP_MAX_ZOOM = 19;   // maxNativeZoom 17，再放大由 Leaflet 升取樣
const MAP_MAX_NATIVE_ZOOM = 17;  // 實際下載到的最高 zoom（算「圖磚覆蓋範圍」用）
const DEFAULT_SITE = 'waishuangxi';
// 缺圖磚時回傳透明 1px，避免破圖；超出已下載範圍只會留白（底下有網格襯底）
const TRANSPARENT_TILE =
  'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+M8AAAMBAQDJ/pLvAAAAAElFTkSuQmCC';

// ---------- 狀態燈 ----------
function setDot(id, on, warn) {
  const el = document.getElementById(id);
  if (!el) return;
  el.classList.toggle('on', !!on && !warn);
  el.classList.toggle('warn', !!warn);
}

// ---------- 遙測 WebSocket ----------
let ws, reconnectDelay = 1000;
let photoToastTimer = null;

function connectWS() {
  ws = new WebSocket('ws://' + GS_HOST + '/ws');
  ws.onopen = () => setDot('dot-ws', true);
  ws.onmessage = (e) => {
    let d;
    try { d = JSON.parse(e.data); } catch (_) { return; }
    applyTelemetry(d);
    reconnectDelay = 1000;
  };
  ws.onclose = () => {
    setDot('dot-ws', false);
    setTimeout(connectWS, reconnectDelay);
    reconnectDelay = Math.min(reconnectDelay * 2, 30000);
  };
  ws.onerror = () => { try { ws.close(); } catch (_) {} };
}

function applyTelemetry(d) {
  updateTelemetry(d);
  updateStreamModeBadge(d.streamMode);
  updateRSSIWarning(d.rssi);
  updateNavStatus(d.navWpIdx, d.navDistM);
  updateEstopBanner(d.estop);
  // 羅盤活著（原始磁場非 0,0）才用真航向；否則退回 GPS 位移推算（沿用舊行為）
  updateRovMarker(d.lat, d.lng, magAlive(d) ? d.heading : undefined);
  updateCompassCal(d);                        // 校準浮層：收 magX/Y min/max
  if (d.photoAck) showPhotoToast();
}
// 原始磁場 (0,0) 視為羅盤未上線（getCorrectedHeading 失敗回 0、magX/Y 維持 0）
function magAlive(d) {
  return typeof d.magX === 'number' && typeof d.magY === 'number' && (d.magX !== 0 || d.magY !== 0);
}
window.__mockTelemetry = applyTelemetry;

// ---------- UI 更新 ----------
function setText(id, v) { const el = document.getElementById(id); if (el) el.textContent = v; }
function fmt(v, dp) { return (typeof v === 'number' && isFinite(v)) ? v.toFixed(dp) : '--'; }

function updateTelemetry(d) {
  setText('t-depth',   fmt(d.depth, 2));
  setText('t-bat',     (typeof d.bat === 'number') ? d.bat : '--');
  setText('t-power',   fmt(d.power, 1));
  setText('t-rssi',    (typeof d.rssi === 'number') ? d.rssi : '--');
  setText('t-heading', magAlive(d) ? fmt(d.heading, 0) : '--');   // 羅盤未上線顯示 --
  setText('t-ml', pctMotor(d.ml));   // 左/右/垂直馬達轉速 %（GS 回傳最近指令 -1023~1023）
  setText('t-mr', pctMotor(d.mr));
  setText('t-mv', pctMotor(d.mv));
  // 電流欄已移除（空間改放三馬達轉速）；座標欄亦已移除。地圖 ROV 標記仍用 d.lat/d.lng（updateRovMarker）
  updateLed(d.led);
}
// 馬達 PWM(-1023~1023) → 百分比整數（-100~100，負＝反向/下潛）；無資料回 '--'
function pctMotor(v) { return (typeof v === 'number' && isFinite(v)) ? Math.round(v / 1023 * 100) : '--'; }

// 燈狀態：開（琥珀亮）/ 關（灰）/ --（無遙測）
function updateLed(on) {
  const el = document.getElementById('t-led');
  if (!el) return;
  if (on === true)       { el.textContent = '開'; el.style.color = 'var(--amber)'; }
  else if (on === false) { el.textContent = '關'; el.style.color = 'var(--muted)'; }
  else                   { el.textContent = '--'; el.style.color = ''; }
  const lb = document.getElementById('tb-light');   // 虛擬燈鈕亮起＝燈實際為開
  if (lb) lb.classList.toggle('on', on === true);
}

function updateStreamModeBadge(mode) {
  const badge = document.getElementById('stream-mode-badge');   // 狀態列「串流」格
  if (badge) {
    badge.textContent = (mode === 1) ? '錄影中' : '純串流';
    badge.style.color = (mode === 1) ? 'var(--red)' : '';        // 錄影中＝紅，純串流＝預設青
  }
  const rb = document.getElementById('tb-rec');   // 虛擬錄影鈕亮起＝目前正在錄影
  if (rb) rb.classList.toggle('on', mode === 1);
}

function updateRSSIWarning(rssi) {
  const warn = document.getElementById('rssi-warning');
  if (!warn || typeof rssi !== 'number') return;
  if (rssi > -60)      { warn.textContent = ''; }
  else if (rssi > -75) { warn.textContent = '⚠ 訊號弱 已降質'; warn.style.color = '#fbbf24'; }
  else                 { warn.textContent = '✖ 訊號極弱 串流暫停'; warn.style.color = '#f43f5e'; }
}

function updateNavStatus(wpIdx, distM) {
  const txt = document.getElementById('nav-text');
  if (!txt) return;
  if (wpIdx === 0xFF || wpIdx === undefined || wpIdx === null) {
    txt.textContent = '手動模式';
  } else {
    txt.textContent = '自動 → 航點 ' + (wpIdx + 1) +
                      '　' + (typeof distM === 'number' ? distM.toFixed(1) : '?') + 'm';
  }
}

function updateEstopBanner(on) {
  const b = document.getElementById('estop-banner');
  if (b) b.style.display = on ? 'block' : 'none';
}

function showPhotoToast() {
  const toast = document.getElementById('photo-toast');
  if (!toast) return;
  toast.style.opacity = '1';
  clearTimeout(photoToastTimer);
  photoToastTimer = setTimeout(() => { toast.style.opacity = '0'; }, 1500);
}

// ---------- 地圖與航點（Leaflet 延遲載入，不擋首屏）----------
let map, rovMarker = null, polyline = null, mapReady = false, leafletLoading = null;
let currentSite = DEFAULT_SITE;
const waypoints = [];

// 首次開「航點」分頁時才動態載入 Leaflet（147KB），避免擋住遙測/手把/影像初始化
function loadLeaflet() {
  if (window.L) return Promise.resolve();
  if (leafletLoading) return leafletLoading;
  leafletLoading = new Promise((resolve, reject) => {
    const css = document.createElement('link');
    css.rel = 'stylesheet'; css.href = '/leaflet.css';
    document.head.appendChild(css);
    const js = document.createElement('script');
    js.src = '/leaflet.js';
    js.onload = resolve;
    js.onerror = () => reject(new Error('leaflet load failed'));
    document.head.appendChild(js);
  });
  return leafletLoading;
}

async function ensureMap() {
  if (mapReady) { if (map) map.invalidateSize(); return; }
  try { await loadLeaflet(); } catch (_) { setHint('✖ 地圖元件載入失敗'); return; }
  initMap();
  mapReady = true;
  // 容器尺寸就緒後再 invalidateSize + 重框一次（getBoundsZoom 需正確容器尺寸才算得準）
  setTimeout(() => { if (map) { map.invalidateSize(); frameSite(SITES[currentSite]); } }, 60);
}

function haversineM(a, b) {
  const R = 6371000, toRad = x => x * Math.PI / 180;
  const dLat = toRad(b.lat - a.lat), dLng = toRad(b.lng - a.lng);
  const s = Math.sin(dLat / 2) ** 2 +
            Math.cos(toRad(a.lat)) * Math.cos(toRad(b.lat)) * Math.sin(dLng / 2) ** 2;
  return 2 * R * Math.asin(Math.sqrt(s));
}

// 已下載圖磚在「原生 zoom nz」實際覆蓋的 lat/lng 矩形：把 bbox 外擴到 256px 磚界＝螢幕真正看到的圖範圍。
// （下載是「整塊磚」，覆蓋必然 ≥ bbox；用這個當 maxBounds，「能滑的範圍」才會等於「看得到的圖磚」。）
function tileCoverageBounds(b, nz) {
  const T = 256;
  const nw = map.project(b.getNorthWest(), nz);
  const se = map.project(b.getSouthEast(), nz);
  const p0 = L.point(Math.floor(nw.x / T) * T, Math.floor(nw.y / T) * T);
  const p1 = L.point(Math.ceil(se.x  / T) * T, Math.ceil(se.y  / T) * T);
  return L.latLngBounds(map.unproject(p0, nz), map.unproject(p1, nz));
}

// maxBounds 動態跟「目前原生 zoom 的圖磚覆蓋」走 → 可滑動範圍 ＝ 看得到的圖磚（消除「看得到卻滑不到」）。
function syncBoundsToTiles() {
  if (!map || map.getSize().y === 0) return;
  const b = L.latLngBounds(SITES[currentSite].bounds);
  const nz = Math.min(Math.round(map.getZoom()), MAP_MAX_NATIVE_ZOOM);
  map.setMaxBounds(tileCoverageBounds(b, nz));   // viscosity 1 → 硬停不回彈；覆蓋＝圖磚 → 不露黑邊
}

// 切場域/初始化：定 minZoom（視野塞進 bbox 的最小縮放，再小會露黑邊）→ setView → maxBounds 同步圖磚覆蓋。
function frameSite(s) {
  const b = L.latLngBounds(s.bounds);
  map.setMinZoom(MAP_MIN_ZOOM);              // 先放開下限，免被上一場域的動態下限擋住 setView
  map.setMaxBounds(null);
  if (map.getSize().y > 0) {                 // 需容器尺寸才算得準（初次無尺寸 → else，待 ensureMap 重框）
    const minZ = map.getBoundsZoom(b, true); // 視野塞進 bbox 的最小縮放（再縮出去就露黑邊）→ 當 minZoom
    map.setMinZoom(minZ);
    map.setView(b.getCenter(), minZ);
    syncBoundsToTiles();                     // maxBounds = 該 zoom 圖磚覆蓋（≥bbox，可滑到所有已載入磚、無黑邊）
  } else {
    map.setView(b.getCenter(), s.zoom);      // 容器尚無尺寸 → 暫置中，ensureMap 會以正確尺寸重框
    map.setMaxBounds(b);
  }
}

function initMap() {
  // 無 +/- 縮放鈕、無右下角 attribution（手機雙指縮放）；硬邊界 viscosity 1（滑到邊界硬停、不回彈）
  map = L.map('map', {
    zoomControl: false, attributionControl: false,
    minZoom: MAP_MIN_ZOOM, maxZoom: MAP_MAX_ZOOM, maxBoundsViscosity: 1.0,
  });
  // 本地離線圖磚（LittleFS /tiles）：maxNativeZoom 17，再放大由 Leaflet 升取樣，不需更多檔
  L.tileLayer('/tiles/{z}/{x}/{y}.png', {
    minZoom: MAP_MIN_ZOOM, maxZoom: MAP_MAX_ZOOM, maxNativeZoom: MAP_MAX_NATIVE_ZOOM,
    errorTileUrl: TRANSPARENT_TILE
  }).addTo(map);
  frameSite(SITES[currentSite]);           // 初次框住預設場域（容器就緒後 ensureMap 會再框一次）
  document.querySelectorAll('.site-chip').forEach(c =>
    c.classList.toggle('active', c.dataset.site === currentSite));
  map.on('zoomend', syncBoundsToTiles);    // 每次縮放後讓 maxBounds 對齊該 zoom 的圖磚覆蓋
  map.on('click', (e) => addWaypoint(e.latlng.lat, e.latlng.lng));
  document.getElementById('btn-upload').addEventListener('click', uploadWaypoints);
  document.getElementById('btn-clear').addEventListener('click', clearWaypoints);
  // 四場域快速跳轉
  document.querySelectorAll('.site-chip').forEach(chip => {
    chip.addEventListener('click', () => goToSite(chip.dataset.site));
  });
}

function goToSite(key) {
  const s = SITES[key];
  if (!s || !map) return;
  currentSite = key;
  frameSite(s);                            // 填滿水域、硬邊界（解鎖縮放下限→setView→鎖新場域 bbox）
  document.querySelectorAll('.site-chip').forEach(c =>
    c.classList.toggle('active', c.dataset.site === key));
}

function wpIcon(n) {
  return L.divIcon({ className: '', html: '<div class="wp-marker">' + n + '</div>',
                     iconSize: [24, 24], iconAnchor: [12, 12] });
}

function addWaypoint(lat, lng) {
  for (const w of waypoints) {
    if (haversineM({ lat, lng }, w) < MIN_WP_DIST_M) {
      setHint('⚠ 太靠近既有航點，未新增');   // 仍保留 5m 去重（防誤觸疊點），只是不顯示數字
      return;
    }
  }
  const marker = L.marker([lat, lng], { icon: wpIcon(waypoints.length + 1) }).addTo(map);
  waypoints.push({ lat, lng, marker });
  redraw();
}

function clearWaypoints() {
  waypoints.forEach(w => map.removeLayer(w.marker));
  waypoints.length = 0;
  redraw();
}

function redraw() {
  setText('wp-count', '航點 ' + waypoints.length);
  if (polyline) { map.removeLayer(polyline); polyline = null; }
  if (waypoints.length >= 2) {
    polyline = L.polyline(waypoints.map(w => [w.lat, w.lng]),
                          { color: '#22d3ee', weight: 3, opacity: .8 }).addTo(map);
  }
}

// 提示無常駐：僅上傳結果/警告等需要時短暫顯示，2.5s 後自動隱藏（平時完全不出現在地圖上）。
let hintTimer = null;
function setHint(t) {
  const el = document.getElementById('wp-hint');
  if (!el) return;
  el.textContent = t || '';
  el.hidden = !t;
  clearTimeout(hintTimer);
  if (t) hintTimer = setTimeout(() => { el.hidden = true; }, 2500);
}

// 初始航向（0=北，順時針為正）。優先用遙測 heading（羅盤，magX/Y 非 0 才採信）；
// 羅盤未上線時退回 GPS 位移推算 course-over-ground（移動 ≥ COURSE_MIN_M 才更新，靜止維持上一航向）。
let rovHeading = null, lastRovFix = null;
const COURSE_MIN_M = 3;

function bearingDeg(a, b) {
  const toRad = x => x * Math.PI / 180, toDeg = x => x * 180 / Math.PI;
  const p1 = toRad(a.lat), p2 = toRad(b.lat), dL = toRad(b.lng - a.lng);
  const y = Math.sin(dL) * Math.cos(p2);
  const x = Math.cos(p1) * Math.sin(p2) - Math.sin(p1) * Math.cos(p2) * Math.cos(dL);
  return (toDeg(Math.atan2(y, x)) + 360) % 360;
}

// 有航向→旋轉箭頭（指向艇首）；無航向（剛開機、尚未位移）→圓點
function rovIcon(h) {
  const has = typeof h === 'number' && isFinite(h);
  const inner = has
    ? '<div class="rov-arrow" style="transform:rotate(' + h.toFixed(0) + 'deg)"></div>'
    : '<div class="rov-dot"></div>';
  return L.divIcon({ className: '', html: '<div class="rov-wrap">' + inner + '</div>',
                     iconSize: [30, 30], iconAnchor: [15, 15] });
}

function updateRovMarker(lat, lng, headingFromTelem) {
  if (typeof lat !== 'number' || typeof lng !== 'number' || (lat === 0 && lng === 0) || !map) return;
  const here = { lat, lng };
  if (typeof headingFromTelem === 'number' && isFinite(headingFromTelem)) {
    rovHeading = (headingFromTelem % 360 + 360) % 360;           // 真羅盤航向（日後接上）
  } else if (lastRovFix && haversineM(lastRovFix, here) >= COURSE_MIN_M) {
    rovHeading = bearingDeg(lastRovFix, here);                   // GPS 位移航向
  }
  lastRovFix = here;
  if (!rovMarker) {
    rovMarker = L.marker(here, { icon: rovIcon(rovHeading), interactive: false, keyboard: false })
                 .addTo(map).bindTooltip('ROV');
  } else {
    rovMarker.setLatLng(here);
    rovMarker.setIcon(rovIcon(rovHeading));
  }
}

async function uploadWaypoints() {
  if (waypoints.length === 0) { setHint('沒有航點可上傳'); return; }
  const payload = { waypoints: waypoints.map((w, i) => ({ lat: w.lat, lng: w.lng, order: i })) };
  setHint('上傳中…');
  try {
    const r = await fetch('http://' + GS_HOST + '/api/waypoints', {
      method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(payload)
    });
    if (r.ok) {
      const j = await r.json().catch(() => ({}));
      setHint('✅ 已上傳 ' + (j.count != null ? j.count : waypoints.length) + ' 個航點');
    } else { setHint('✖ 上傳失敗（HTTP ' + r.status + '）'); }
  } catch (_) { setHint('✖ 上傳失敗（連線錯誤）'); }
}

// ---------- 分頁 ----------
function initTabs() {
  const tabs = document.querySelectorAll('.tab');
  tabs.forEach(tab => tab.addEventListener('click', () => {
    tabs.forEach(t => t.classList.remove('active'));
    tab.classList.add('active');
    const name = tab.dataset.tab;
    activeTab = name;                  // 控制虛擬鈕只在影像分頁出現（controlTick 讀此值）
    document.querySelectorAll('.pane').forEach(p => p.classList.remove('active'));
    document.getElementById('pane-' + name).classList.add('active');
    if (name === 'map') ensureMap();   // 首次進航點頁才載入 Leaflet 並初始化地圖
  }));
}

// 「啟動自動」開關（航點頁側欄）：切換 autoEngaged → controlTick 帶 auto 上行 → GS 設 pkt.autoMode。
// 註：ROV 需有航點且 GPS 定位有效才會實際移動；GPS 未修好前按下不會動，僅旗標生效（軟體完整、待硬體）。
function initAutoToggle() {
  const b = document.getElementById('btn-auto');
  if (!b) return;
  b.addEventListener('click', () => {
    autoEngaged = !autoEngaged;
    b.classList.toggle('primary', autoEngaged);
    b.textContent = autoEngaged ? '■ 停止自動' : '▶ 啟動自動';
  });
}

// ---------- 影像：中斷顯示佔位圖並自動重連 ----------
function initStream() {
  const img = document.getElementById('stream');
  const ph  = document.getElementById('stream-ph');
  if (!img || !ph) return;
  let retryTimer = null;
  img.addEventListener('load', () => { ph.style.display = 'none'; img.style.opacity = '1'; setDot('dot-cam', true); });
  img.addEventListener('error', () => {
    img.style.opacity = '0';
    ph.style.display = 'flex';
    ph.textContent = '📡 影像中斷，重連中…';
    setDot('dot-cam', false);
    clearTimeout(retryTimer);
    retryTimer = setTimeout(() => { img.src = STREAM_SRC + '?t=' + Date.now(); }, 3000);
  });
  img.src = STREAM_SRC;   // 由 JS 啟動串流（避免無限連線的 <img> 卡住 window.load）
}

// ---------- 全螢幕 ----------
function initFullscreen() {
  const btn = document.getElementById('btn-fs');
  if (!btn) return;
  btn.addEventListener('click', () => {
    const el = document.documentElement;
    if (!document.fullscreenElement && !document.webkitFullscreenElement) {
      const req = el.requestFullscreen || el.webkitRequestFullscreen;
      if (req) { try { const p = req.call(el); if (p && p.catch) p.catch(() => {}); } catch (_) {} }
    } else {
      const exit = document.exitFullscreen || document.webkitExitFullscreen;
      if (exit) { try { exit.call(document); } catch (_) {} }
    }
  });
}

// ---------- 控制上行：實體手把（Gamepad API）或螢幕虛擬搖桿 → WS ----------
// 兩者送出完全相同的 {t:'c',lx,ly,ry,b}，GS 端不分來源。接上手把時優先用手把並自動隱藏虛擬鈕。
function clampAxis(v) { return Math.max(-32767, Math.min(32767, Math.round((v || 0) * 32767))); }
function pressed(gp, i) { return gp.buttons[i] && gp.buttons[i].pressed ? 1 : 0; }

// 虛擬觸控狀態（無手把時生效）。b 位元同手把：bit3=Y(燈) / bit5=RB(錄影)。拍照改走 photoSeq（見下）。
const touch = { lx: 0, ly: 0, ry: 0, b: 0 };
let activeTab = 'video';
let autoEngaged = false;   // 「啟動自動」開關 → controlTick 帶 auto 欄上行；GS 設 pkt.autoMode
let photoSeq = 0;          // 拍照單調序號：每按一下 +1 並夾帶每筆 controlTick 上行；ROV 序號一變就拍一張

function firstGamepad() {
  const pads = navigator.getGamepads ? navigator.getGamepads() : [];
  for (const p of pads) { if (p && p.connected) return p; }
  return null;
}

// 虛擬鈕只在「影像分頁 + 沒接手把」時出現（橫向限制由 CSS 再把關）
function updateTouchVisible(hasPad) {
  const el = document.getElementById('touch-ctl');
  if (el) el.hidden = !(activeTab === 'video' && !hasPad);
}

function controlTick() {
  const gp = firstGamepad();
  setDot('dot-gp', !!gp);
  updateTouchVisible(!!gp);
  if (!ws || ws.readyState !== WebSocket.OPEN) return;
  let lx, ly, ry, b;
  if (gp) {
    // 位元：bit0=A,1=B,2=X,3=Y,4=LB,5=RB,6=Start(btn9),7=Back(btn8)
    b = pressed(gp,0) | (pressed(gp,1)<<1) | (pressed(gp,2)<<2) | (pressed(gp,3)<<3) |
        (pressed(gp,4)<<4) | (pressed(gp,5)<<5) | (pressed(gp,9)<<6) | (pressed(gp,8)<<7);
    lx = clampAxis(gp.axes[0]); ly = clampAxis(gp.axes[1]); ry = clampAxis(gp.axes[3]);
  } else {
    lx = touch.lx; ly = touch.ly; ry = touch.ry; b = touch.b;
  }
  // ts＝手機 UTC 紀元秒：ROV 無 RTC/NTP/GPS，靠這個設一次系統時鐘，否則 SD 照片/影片時間恆為 1980。
  // ph＝拍照單調序號：每筆都帶最新值，ROV 序號一變就拍一張（漏不掉、連點不合併、ESP-NOW 丟包自動補送）。
  ws.send(JSON.stringify({ t: 'c', lx, ly, ry, b, auto: autoEngaged ? 1 : 0,
                           ts: Math.floor(Date.now() / 1000), ph: photoSeq }));
}

function initInput() {
  window.addEventListener('gamepadconnected', () => setDot('dot-gp', true));
  window.addEventListener('gamepaddisconnected', () => setDot('dot-gp', false));
  initTouchControls();
  setInterval(controlTick, 40);   // ~25Hz 上行（GS 以最新值轉 ESP-NOW）
}

// 360° 虛擬搖桿 + 深度/燈/拍照/錄影。用 Pointer Events 支援多點觸控（左搖桿與右鈕可同時按）。
function initTouchControls() {
  const joy = document.getElementById('joy');
  const thumb = document.getElementById('joy-thumb');
  if (joy && thumb) {
    let pid = null;
    const maxR = () => joy.clientWidth / 2 - 8;          // 拇指可移動半徑
    const moveTo = (e) => {
      const r = joy.getBoundingClientRect();
      let dx = e.clientX - (r.left + r.width / 2);
      let dy = e.clientY - (r.top + r.height / 2);
      const m = maxR(), len = Math.hypot(dx, dy) || 1;
      if (len > m) { dx = dx / len * m; dy = dy / len * m; }
      thumb.style.transform = 'translate(' + dx + 'px,' + dy + 'px)';
      touch.lx = Math.round(dx / m * 32767);
      touch.ly = Math.round(dy / m * 32767);             // 螢幕下為正；上推 dy<0 → ly<0 = 前進（同手把）
    };
    joy.addEventListener('pointerdown', (e) => {
      e.preventDefault(); pid = e.pointerId;
      try { joy.setPointerCapture(pid); } catch (_) {}
      moveTo(e);
    });
    joy.addEventListener('pointermove', (e) => { if (e.pointerId === pid) moveTo(e); });
    const release = (e) => {
      if (e.pointerId !== pid) return;
      pid = null; thumb.style.transform = 'translate(0,0)'; touch.lx = 0; touch.ly = 0;
    };
    joy.addEventListener('pointerup', release);
    joy.addEventListener('pointercancel', release);
  }
  // 深度垂直搖桿：上下挪動設 ry（比例＝升降轉速/強度），放開彈回中心歸零。
  // 上推 dy<0 → ry<0 →（GS）vertMotor +（上升）；下拉 dy>0 → ry>0 → 下潛。同實體右搖桿垂直軸。
  const dj = document.getElementById('depth-joy');
  const dthumb = document.getElementById('depth-thumb');
  if (dj && dthumb) {
    let dpid = null;
    const maxY = () => dj.clientHeight / 2 - 6;          // 拇指可移動半幅
    const moveD = (e) => {
      const r = dj.getBoundingClientRect();
      let dy = e.clientY - (r.top + r.height / 2);
      const m = maxY();
      if (dy > m) dy = m; else if (dy < -m) dy = -m;
      dthumb.style.transform = 'translateY(' + dy + 'px)';
      touch.ry = Math.round(dy / m * 32767);
    };
    dj.addEventListener('pointerdown', (e) => {
      e.preventDefault(); dpid = e.pointerId;
      try { dj.setPointerCapture(dpid); } catch (_) {}
      moveD(e);
    });
    dj.addEventListener('pointermove', (e) => { if (e.pointerId === dpid) moveD(e); });
    const releaseD = (e) => {
      if (e.pointerId !== dpid) return;
      dpid = null; dthumb.style.transform = 'translateY(0)'; touch.ry = 0;
    };
    dj.addEventListener('pointerup', releaseD);
    dj.addEventListener('pointercancel', releaseD);
  }
  // 燈/錄影＝toggle，按下設 bit、放開清 bit → GS 偵測上升邊緣 toggle 一次。
  // 用 pulsePress/pulseRelease 保證最短脈衝寬，否則快速點擊會被 25Hz 上行漏掉（見下方說明）。
  bindHold('tb-light', () => pulsePress(1 << 3), () => pulseRelease(1 << 3)); // Y（燈 toggle）
  bindHold('tb-rec',   () => pulsePress(1 << 5), () => pulseRelease(1 << 5)); // RB（錄影 toggle）
  // 拍照＝按一下拍一張：用單調序號（不走 bit/邊緣），並立刻本地閃快門 → 一按就有反應、超利索。
  bindHold('tb-photo', triggerPhoto, () => {});
}

// 拍照：序號 +1（夾帶每筆 controlTick；ROV 序號變即拍，漏不掉/不合併）＋ 立刻快門回饋（不等 ROV ack）。
function triggerPhoto() {
  photoSeq = (photoSeq + 1) & 0xFF;
  flashShutter();
}

// 即時快門：本地全螢幕快閃，按下當下就有視覺回饋（ROV 真存好後另有「已拍照存檔」toast 確認）。
function flashShutter() {
  const s = document.getElementById('shutter');
  if (!s) return;
  s.classList.remove('flash');
  void s.offsetWidth;          // 強制 reflow → 連點也能每次重觸動畫
  s.classList.add('flash');
}

// 一次性動作鍵的最短脈衝寬。手機以 25Hz（controlTick 每 40ms）取樣送出「當下」的 touch.b；
// 若點擊比一個取樣週期還短，bit 在兩次 tick 之間 set→clear 完全沒被取樣 → GS 收不到上升邊緣
// → 拍照/錄影/燈沒反應，使用者得連按好幾下。按下後保證 bit 至少維持 PULSE_MS（≥3 個取樣週期），
// 上行必取樣到 ≥1 次、GS 100Hz 邊緣偵測必觸發一次。
const PULSE_MS = 120;
const pulseDownAt = {};   // bit -> 按下時間戳(ms)
const pulseTimer  = {};   // bit -> 延後清除計時器
function pulsePress(bit) {
  if (pulseTimer[bit]) { clearTimeout(pulseTimer[bit]); pulseTimer[bit] = null; }
  pulseDownAt[bit] = Date.now();
  touch.b |= bit;
}
function pulseRelease(bit) {
  const wait = Math.max(0, PULSE_MS - (Date.now() - (pulseDownAt[bit] || 0)));
  if (pulseTimer[bit]) clearTimeout(pulseTimer[bit]);
  pulseTimer[bit] = setTimeout(() => { touch.b &= ~bit; pulseTimer[bit] = null; }, wait);
}

function bindHold(id, onPress, onRelease) {
  const el = document.getElementById(id);
  if (!el) return;
  el.addEventListener('pointerdown', (e) => {
    e.preventDefault();
    try { el.setPointerCapture(e.pointerId); } catch (_) {}
    el.classList.add('active'); onPress();
  });
  const up = () => { el.classList.remove('active'); onRelease(); };
  el.addEventListener('pointerup', up);
  el.addEventListener('pointercancel', up);
}

// ---------- 羅盤校準（手機端：轉 360° 收 magX/Y min/max → 算 offset/scale）----------
// 模型對齊 ROV sensors.cpp：x=(gx-offX)*scaleX、y=(gy-offY)*scaleY、heading=atan2(y,x)。
//   offset=(max+min)/2（hard-iron 平移）；scale=avg(rX,rY)/r（soft-iron 把橢圓拉回正圓）。
const CAL_SECTORS = 12;   // 圓分 12 個 30° 扇區，全部踩過 → 確認真的轉滿一圈（涵蓋 100%）
const cal = { on: false, open: false, minX: Infinity, maxX: -Infinity, minY: Infinity, maxY: -Infinity, sectors: 0 };

function calReset() {
  cal.minX = cal.minY = Infinity;
  cal.maxX = cal.maxY = -Infinity;
  cal.sectors = 0;
}
function popcount(n) { let c = 0; while (n) { c += n & 1; n >>>= 1; } return c; }

function updateCompassCal(d) {
  if (!cal.open) return;                       // 浮層沒開就不算
  const hasMag = magAlive(d);
  setText('cal-heading', hasMag ? fmt(d.heading, 0) : '--');
  setText('cal-mx', hasMag ? d.magX.toFixed(3) : '--');
  setText('cal-my', hasMag ? d.magY.toFixed(3) : '--');
  if (!cal.on || !hasMag) return;
  if (d.magX < cal.minX) cal.minX = d.magX;
  if (d.magX > cal.maxX) cal.maxX = d.magX;
  if (d.magY < cal.minY) cal.minY = d.magY;
  if (d.magY > cal.maxY) cal.maxY = d.magY;
  // 原始 atan2 當扇區索引（offset 只是平移，整圈仍掃過 360°）→ 確認轉滿一圈
  const ang = (Math.atan2(d.magY, d.magX) * 180 / Math.PI + 360) % 360;
  cal.sectors |= (1 << Math.floor(ang / (360 / CAL_SECTORS)));
  const filled = popcount(cal.sectors);
  setText('cal-cover', Math.round(filled / CAL_SECTORS * 100));
  renderCalOut(filled);
}

function calConstants() {
  const offX = (cal.maxX + cal.minX) / 2, offY = (cal.maxY + cal.minY) / 2;
  const rX = (cal.maxX - cal.minX) / 2, rY = (cal.maxY - cal.minY) / 2;
  const avg = (rX + rY) / 2;
  return { offX, offY, scX: rX > 1e-6 ? avg / rX : 1, scY: rY > 1e-6 ? avg / rY : 1 };
}

function renderCalOut(filled) {
  const out = document.getElementById('cal-out');
  const copy = document.getElementById('cal-copy');
  if (!out) return;
  if (!isFinite(cal.minX) || !isFinite(cal.minY)) {
    out.textContent = '收集中…轉動潛水艇'; if (copy) copy.disabled = true; return;
  }
  const c = calConstants();
  const ok = filled >= CAL_SECTORS;            // 12 扇區全踩過才算轉滿
  out.textContent =
    '// 貼進 sensors.cpp（取代 g_offset*/g_scale* 預設值）\n' +
    'g_offsetX = ' + c.offX.toFixed(3) + 'f;  g_offsetY = ' + c.offY.toFixed(3) + 'f;\n' +
    'g_scaleX  = ' + c.scX.toFixed(4) + 'f;  g_scaleY  = ' + c.scY.toFixed(4) + 'f;\n' +
    (ok ? '// ✅ 已轉滿一圈' : '// ⚠ 尚未轉滿一圈，數值可能不準');
  if (copy) copy.disabled = false;
}

function initCompassCal() {
  const ov    = document.getElementById('cal-overlay');
  const open  = document.getElementById('btn-cal');
  const close = document.getElementById('cal-close');
  const start = document.getElementById('cal-start');
  const reset = document.getElementById('cal-reset');
  const copy  = document.getElementById('cal-copy');
  if (!ov || !open) return;
  const hide = () => { cal.open = false; cal.on = false; ov.hidden = true; if (start) start.textContent = '開始收集'; };
  open.addEventListener('click', () => { cal.open = true; ov.hidden = false; });
  if (close) close.addEventListener('click', hide);
  ov.addEventListener('click', (e) => { if (e.target === ov) hide(); });   // 點背景關閉
  if (start) start.addEventListener('click', () => {
    cal.on = !cal.on;
    if (cal.on) { calReset(); start.textContent = '收集中…（再按停）'; }
    else start.textContent = '開始收集';
  });
  if (reset) reset.addEventListener('click', () => {
    calReset();
    setText('cal-cover', 0);
    const out = document.getElementById('cal-out');
    if (out) out.textContent = '已重置。按「開始收集」後把艇轉一圈。';
    if (copy) copy.disabled = true;
  });
  if (copy) copy.addEventListener('click', async () => {
    const out = document.getElementById('cal-out');
    if (!out) return;
    try { await navigator.clipboard.writeText(out.textContent); copy.textContent = '已複製 ✓'; }
    catch (_) { copy.textContent = '複製失敗（手動選取）'; }
    setTimeout(() => { copy.textContent = '複製常數'; }, 1500);
  });
}

// ---------- 啟動 ----------
// 用 DOMContentLoaded（DOM 解析完即觸發），不要用 window.load——
// 影像 <img> 是無限 MJPEG 連線，window.load 可能永遠不觸發 → 整支 app 不初始化。
function boot() {
  initTabs();          // 地圖改在首次開航點分頁時才載入（ensureMap）
  initStream();        // 在此才設定 <img>.src 啟動串流
  initFullscreen();
  initInput();         // 實體手把 + 螢幕虛擬搖桿（無手把時顯示）
  initAutoToggle();    // 「啟動自動」開關（航點頁側欄）
  initCompassCal();    // 羅盤校準浮層（🧭 校準）
  connectWS();
  window.addEventListener('resize', () => { if (map) map.invalidateSize(); });
  if ('serviceWorker' in navigator) navigator.serviceWorker.register('/sw.js').catch(() => {});
}
if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', boot);
else boot();
