/*
 * Keeps CalcAll working offline once it has been opened. The build puts
 * the commit it was made from in VERSION, so each update replaces the
 * stored copy.
 */
const VERSION = '__VERSION__';
const FILES = ['./', 'index.html', 'index.js', 'index.wasm', 'index.data', 'manifest.webmanifest',
               'icon-256.png', 'icon-512.png', 'icon-maskable-512.png', 'apple-touch-icon.png'];

self.addEventListener('install', (event) => {
  event.waitUntil(caches.open(VERSION).then((cache) => cache.addAll(FILES)).then(() => self.skipWaiting()));
});

self.addEventListener('activate', (event) => {
  event.waitUntil(caches.keys()
    .then((names) => Promise.all(names.filter((n) => n !== VERSION).map((n) => caches.delete(n))))
    .then(() => self.clients.claim()));
});

/* Use the network when there is one (so updates arrive), the stored copy when there isn't. */
self.addEventListener('fetch', (event) => {
  if (event.request.method !== 'GET') return;
  event.respondWith(
    fetch(event.request)
      .then((response) => {
        const copy = response.clone();
        caches.open(VERSION).then((cache) => cache.put(event.request, copy));
        return response;
      })
      .catch(() => caches.match(event.request, {ignoreSearch: true})));
});
