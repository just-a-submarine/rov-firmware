'use strict';

// =============================================================================
//  ROV 地面站手機端 App
//  - WebSocket 接收遙測（ws://192.168.4.1/ws）+ 指數退避自動重連
//  - Leaflet 地圖點擊新增航點，HTTP POST 上傳
//  - 影像由 index.html 的 <img> 直連 ROV，不在此處理
// =============================================================================

const GS_HOST = location.host || '192.168.4.1';
const MIN_WP_DIST_M = 5;        // GPS 精度限制：航點最小間距（doc/06 §五）
const ARRIVAL_RADIUS_M = 4;

// ---------- 遙測 WebSocket ----------
let ws, reconnectDelay = 1000;
let photoToastTimer = null;

function connectWS() {
  ws = new WebSocket('ws://' + GS_HOST + '/ws');
  ws.onopen = () => setConn(true);
  ws.onmessage = (e) => {
    let d;
    try { d = JSON.parse(e.data); } catch (_) { return; }
    applyTelemetry(d);
    reconnectDelay = 1000;
  };
  ws.onclose = () => {
    setConn(false);
    setTimeout(connectWS, reconnectDelay);
    reconnectDelay = Math.min(reconnectDelay * 2, 30000);
  };
  ws.onerror = () => { try { ws.close(); } catch (_) {} };
}

function setConn(on) {
  const dot = document.getElementById('conn-dot');
  if (dot) dot.className = 'conn-dot ' + (on ? 'on' : 'off');
}

// 統一套用一筆遙測到 UI（也供本機測試用：window.__mockTelemetry({...})）
function applyTelemetry(d) {
  updateTelemetry(d);
  updateStreamModeBadge(d.streamMode);
  updateRSSIWarning(d.rssi);
  updateNavStatus(d.navWpIdx, d.navDistM);
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
  const lat = fmt(d.lat, 6), lng = fmt(d.lng, 6);
  setText('t-latlng', lat + ', ' + lng);
}

function updateStreamModeBadge(mode) {
  const badge = document.getElementById('stream-mode-badge');
  if (!badge) return;
  if (mode === 1) {
    badge.textContent = '⏺ 串流 + 錄影';
    badge.style.background = '#d93025';
  } else {
    badge.textContent = '● 純串流';
    badge.style.background = '#1a73e8';
  }
}

function updateRSSIWarning(rssi) {
  const warn = document.getElementById('rssi-warning');
  if (!warn || typeof rssi !== 'number') return;
  if (rssi > -60) {
    warn.textContent = '';
  } else if (rssi > -75) {
    warn.textContent = '⚠ 訊號弱 已降質';
    warn.style.color = '#f9ab00';
  } else {
    warn.textContent = '✖ 訊號極弱 串流暫停';
    warn.style.color = '#d93025';
  }
}

function updateNavStatus(wpIdx, distM) {
  const bar = document.getElementById('nav-status');
  if (!bar) return;
  if (wpIdx === 0xFF || wpIdx === undefined || wpIdx === null) {
    bar.textContent = '手動模式';
    bar.style.background = '#444';
  } else {
    bar.textContent = '自動導航 → 航點 ' + (wpIdx + 1) +
                      '　距離 ' + (typeof distM === 'number' ? distM.toFixed(1) : '?') + ' m';
    bar.style.background = '#1a73e8';
  }
}

function showPhotoToast() {
  const toast = document.getElementById('photo-toast');
  if (!toast) return;
  toast.style.opacity = '1';
  clearTimeout(photoToastTimer);
  photoToastTimer = setTimeout(() => { toast.style.opacity = '0'; }, 1500);
}

// ---------- 地圖與航點 ----------
let map, rovMarker = null;
const waypoints = [];   // { lat, lng, marker }
let polyline = null;

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
  tiles.on('tileerror', () => {
    if (!tileFailed) { tileFailed = true; switchToCanvasGrid(); }
  });
  tiles.addTo(map);

  map.on('click', (e) => addWaypoint(e.latlng.lat, e.latlng.lng));

  document.getElementById('btn-upload').addEventListener('click', uploadWaypoints);
  document.getElementById('btn-clear').addEventListener('click', clearWaypoints);
}

// tile 載入失敗（手機停用行動數據）→ 降級為灰底網格，航點功能仍可用
function switchToCanvasGrid() {
  const el = document.getElementById('map');
  el.style.backgroundImage =
    'linear-gradient(#30363d 1px, transparent 1px), linear-gradient(90deg, #30363d 1px, transparent 1px)';
  el.style.backgroundSize = '40px 40px';
  setHint('離線網格模式（地圖磁磚載入失敗，航點仍可設定）');
}

function wpIcon(n) {
  return L.divIcon({ className: '', html: '<div class="wp-marker">' + n + '</div>',
                     iconSize: [24, 24], iconAnchor: [12, 12] });
}

function addWaypoint(lat, lng) {
  const cand = { lat, lng };
  for (const w of waypoints) {
    if (haversineM(cand, w) < MIN_WP_DIST_M) {
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
                          { color: '#1a73e8', weight: 3, opacity: .7 }).addTo(map);
  }
}

function setHint(t) { setText('wp-hint', t); }

function updateRovMarker(lat, lng) {
  if (typeof lat !== 'number' || typeof lng !== 'number' || (lat === 0 && lng === 0)) return;
  if (!map) return;
  if (!rovMarker) {
    rovMarker = L.circleMarker([lat, lng], {
      radius: 7, color: '#2ea043', fillColor: '#2ea043', fillOpacity: .9
    }).addTo(map).bindTooltip('ROV');
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
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    });
    if (r.ok) {
      const j = await r.json().catch(() => ({}));
      setHint('✅ 已上傳 ' + (j.count != null ? j.count : waypoints.length) + ' 個航點');
    } else {
      setHint('✖ 上傳失敗（HTTP ' + r.status + '）');
    }
  } catch (err) {
    setHint('✖ 上傳失敗（連線錯誤）');
  }
}

// ---------- 啟動 ----------
window.addEventListener('load', () => {
  initMap();
  connectWS();
});
