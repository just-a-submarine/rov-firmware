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
  updateRovMarker(d.lat, d.lng);
  if (d.photoAck) showPhotoToast();
}
window.__mockTelemetry = applyTelemetry;

// ---------- UI 更新 ----------
function setText(id, v) { const el = document.getElementById(id); if (el) el.textContent = v; }
function fmt(v, dp) { return (typeof v === 'number' && isFinite(v)) ? v.toFixed(dp) : '--'; }

function updateTelemetry(d) {
  setText('t-depth',   fmt(d.depth, 2));
  setText('t-bat',     (typeof d.bat === 'number') ? d.bat : '--');
  setText('t-power',   fmt(d.power, 1));
  setText('t-current', fmt(d.current, 2));
  setText('t-rssi',    (typeof d.rssi === 'number') ? d.rssi : '--');
  setText('t-latlng',  fmt(d.lat, 6) + ', ' + fmt(d.lng, 6));
  updateLed(d.led);
}

// 燈狀態：開（琥珀亮）/ 關（灰）/ --（無遙測）
function updateLed(on) {
  const el = document.getElementById('t-led');
  if (!el) return;
  if (on === true)       { el.textContent = '開'; el.style.color = 'var(--amber)'; }
  else if (on === false) { el.textContent = '關'; el.style.color = 'var(--muted)'; }
  else                   { el.textContent = '--'; el.style.color = ''; }
}

function updateStreamModeBadge(mode) {
  const badge = document.getElementById('stream-mode-badge');
  if (!badge) return;
  if (mode === 1) { badge.textContent = '⏺ 串流 + 錄影'; badge.style.background = '#f43f5e'; badge.style.color = '#fff'; }
  else            { badge.textContent = '● 純串流';      badge.style.background = ''; badge.style.color = ''; }
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
  setTimeout(() => { if (map) map.invalidateSize(); }, 60);  // 修手機地圖不顯示
}

function haversineM(a, b) {
  const R = 6371000, toRad = x => x * Math.PI / 180;
  const dLat = toRad(b.lat - a.lat), dLng = toRad(b.lng - a.lng);
  const s = Math.sin(dLat / 2) ** 2 +
            Math.cos(toRad(a.lat)) * Math.cos(toRad(b.lat)) * Math.sin(dLng / 2) ** 2;
  return 2 * R * Math.asin(Math.sqrt(s));
}

function initMap() {
  map = L.map('map', { zoomControl: true }).setView([25.0330, 121.5654], 16);
  const tiles = L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png', {
    maxZoom: 18, attribution: '© OpenStreetMap'
  });
  let tileFailed = false;
  tiles.on('tileerror', () => { if (!tileFailed) { tileFailed = true; switchToCanvasGrid(); } });
  tiles.addTo(map);
  map.on('click', (e) => addWaypoint(e.latlng.lat, e.latlng.lng));
  document.getElementById('btn-upload').addEventListener('click', uploadWaypoints);
  document.getElementById('btn-clear').addEventListener('click', clearWaypoints);
}

function switchToCanvasGrid() {
  const el = document.getElementById('map');
  el.style.backgroundImage =
    'linear-gradient(rgba(34,211,238,.12) 1px, transparent 1px), linear-gradient(90deg, rgba(34,211,238,.12) 1px, transparent 1px)';
  el.style.backgroundSize = '40px 40px';
  setHint('離線網格模式（地圖磁磚載入失敗，航點仍可設定）');
}

function wpIcon(n) {
  return L.divIcon({ className: '', html: '<div class="wp-marker">' + n + '</div>',
                     iconSize: [24, 24], iconAnchor: [12, 12] });
}

function addWaypoint(lat, lng) {
  for (const w of waypoints) {
    if (haversineM({ lat, lng }, w) < MIN_WP_DIST_M) {
      setHint('⚠ 太靠近既有航點（需 ≥ ' + MIN_WP_DIST_M + 'm），未新增');
      return;
    }
  }
  const marker = L.marker([lat, lng], { icon: wpIcon(waypoints.length + 1) }).addTo(map);
  waypoints.push({ lat, lng, marker });
  redraw();
  setHint('點地圖新增航點（間距需 ≥ ' + MIN_WP_DIST_M + 'm）');
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

function setHint(t) { setText('wp-hint', t); }

function updateRovMarker(lat, lng) {
  if (typeof lat !== 'number' || typeof lng !== 'number' || (lat === 0 && lng === 0) || !map) return;
  if (!rovMarker) {
    rovMarker = L.circleMarker([lat, lng], { radius: 7, color: '#34d399', fillColor: '#34d399', fillOpacity: .9 })
                 .addTo(map).bindTooltip('ROV');
  } else {
    rovMarker.setLatLng([lat, lng]);
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
    document.querySelectorAll('.pane').forEach(p => p.classList.remove('active'));
    document.getElementById('pane-' + name).classList.add('active');
    if (name === 'map') ensureMap();   // 首次進航點頁才載入 Leaflet 並初始化地圖
  }));
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

// ---------- 影像顯示比例（符合 / 填滿 / 拉伸，記憶於 localStorage）----------
const FIT_MODES = [
  { cls: 'fit-contain', label: '符合' },   // object-fit: contain（完整畫面，留黑邊）
  { cls: 'fit-cover',   label: '填滿' },   // object-fit: cover（填滿畫面，裁切邊緣）
  { cls: 'fit-fill',    label: '拉伸' },   // object-fit: fill（拉滿，會變形）
];
function initFit() {
  const img = document.getElementById('stream');
  const btn = document.getElementById('btn-fit');
  if (!img || !btn) return;
  let idx = parseInt(localStorage.getItem('fitMode') || '0', 10);
  if (!(idx >= 0 && idx < FIT_MODES.length)) idx = 0;
  const apply = () => {
    img.classList.remove('fit-contain', 'fit-cover', 'fit-fill');
    img.classList.add(FIT_MODES[idx].cls);
    btn.textContent = FIT_MODES[idx].label;
  };
  apply();
  btn.addEventListener('click', () => {
    idx = (idx + 1) % FIT_MODES.length;
    try { localStorage.setItem('fitMode', String(idx)); } catch (_) {}
    apply();
  });
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

// ---------- 手把：瀏覽器 Gamepad API → WS 上行 ----------
function clampAxis(v) { return Math.max(-32767, Math.min(32767, Math.round((v || 0) * 32767))); }
function pressed(gp, i) { return gp.buttons[i] && gp.buttons[i].pressed ? 1 : 0; }

function pollAndSendGamepad() {
  const pads = navigator.getGamepads ? navigator.getGamepads() : [];
  let gp = null;
  for (const p of pads) { if (p && p.connected) { gp = p; break; } }
  setDot('dot-gp', !!gp);
  if (!gp || !ws || ws.readyState !== WebSocket.OPEN) return;
  // 位元：bit0=A,1=B,2=X,3=Y,4=LB,5=RB,6=Start(btn9),7=Back(btn8)
  const b = pressed(gp,0) | (pressed(gp,1)<<1) | (pressed(gp,2)<<2) | (pressed(gp,3)<<3) |
            (pressed(gp,4)<<4) | (pressed(gp,5)<<5) | (pressed(gp,9)<<6) | (pressed(gp,8)<<7);
  ws.send(JSON.stringify({
    t: 'c',
    lx: clampAxis(gp.axes[0]), ly: clampAxis(gp.axes[1]), ry: clampAxis(gp.axes[3]), b
  }));
}

function initGamepad() {
  window.addEventListener('gamepadconnected', () => setDot('dot-gp', true));
  window.addEventListener('gamepaddisconnected', () => setDot('dot-gp', false));
  setInterval(pollAndSendGamepad, 40);   // ~25Hz 上行（GS 以最新值 100Hz 轉 ESP-NOW）
}

// ---------- 啟動 ----------
// 用 DOMContentLoaded（DOM 解析完即觸發），不要用 window.load——
// 影像 <img> 是無限 MJPEG 連線，window.load 可能永遠不觸發 → 整支 app 不初始化。
function boot() {
  initTabs();          // 地圖改在首次開航點分頁時才載入（ensureMap）
  initStream();        // 在此才設定 <img>.src 啟動串流
  initFit();           // 還原影像顯示比例偏好
  initFullscreen();
  initGamepad();
  connectWS();
  window.addEventListener('resize', () => { if (map) map.invalidateSize(); });
  if ('serviceWorker' in navigator) navigator.serviceWorker.register('/sw.js').catch(() => {});
}
if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', boot);
else boot();
