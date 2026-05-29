'use strict';
// =============================================================================
//  Service Worker — 快取 App 殼（地面站本機供檔，離線本即可用）
//  註：http://192.168.4.1 非安全來源，部分瀏覽器不會註冊 SW；註冊失敗無妨，
//  standalone 仍由 manifest / 全螢幕鈕達成。
// =============================================================================
const CACHE = 'rov-gs-v1';
const SHELL = [
  '/', '/index.html', '/style.css', '/app.js',
  '/leaflet.js', '/leaflet.css', '/manifest.json', '/icon.svg'
];

self.addEventListener('install', (e) => {
  e.waitUntil(caches.open(CACHE).then((c) => c.addAll(SHELL)).then(() => self.skipWaiting()));
});

self.addEventListener('activate', (e) => {
  e.waitUntil(
    caches.keys()
      .then((ks) => Promise.all(ks.filter((k) => k !== CACHE).map((k) => caches.delete(k))))
      .then(() => self.clients.claim())
  );
});

self.addEventListener('fetch', (e) => {
  const u = new URL(e.request.url);
  // 只接管本機同源的 GET 靜態資源；影像(192.168.4.100)、磁磚為跨源 → 不攔。
  if (e.request.method !== 'GET' || u.origin !== location.origin) return;
  // 動態端點（航點 API / WebSocket 升級）一律走網路。
  if (u.pathname.startsWith('/api') || u.pathname.startsWith('/ws')) return;

  e.respondWith(
    caches.match(e.request).then((hit) => hit || fetch(e.request).then((resp) => {
      const copy = resp.clone();
      caches.open(CACHE).then((c) => c.put(e.request, copy));
      return resp;
    }).catch(() => hit))
  );
});
