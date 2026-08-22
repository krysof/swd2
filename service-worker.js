/* Versioned persistent cache for the WebAssembly release.  build-wasm.sh
   replaces the token below and ties every cached URL to the visible build. */
const releaseVersion = '2026.08.22.5';
const cachePrefix = 'swd2-web-';
const cacheName = cachePrefix + releaseVersion;
const releaseAssets = [
  './index.html',
  `./index.js?v=${releaseVersion}`,
  `./index.wasm?v=${releaseVersion}`,
  `./index.data?v=${releaseVersion}`,
];

self.addEventListener('install', event => {
  event.waitUntil((async () => {
    const cache = await caches.open(cacheName);
    await cache.addAll(releaseAssets);
    await self.skipWaiting();
  })());
});

self.addEventListener('activate', event => {
  event.waitUntil((async () => {
    const names = await caches.keys();
    await Promise.all(names
      .filter(name => name.startsWith(cachePrefix) && name !== cacheName)
      .map(name => caches.delete(name)));
    await self.clients.claim();
  })());
});

self.addEventListener('fetch', event => {
  if (event.request.method !== 'GET') return;
  const url = new URL(event.request.url);
  if (url.origin !== self.location.origin) return;

  if (event.request.mode === 'navigate') {
    // Always check the tiny HTML entry point so a new release is discovered;
    // the cached page remains an offline fallback.
    event.respondWith(fetch(event.request).catch(async () => {
      const cache = await caches.open(cacheName);
      return cache.match('./index.html');
    }));
    return;
  }

  const relative = './' + url.pathname.split('/').pop() + url.search;
  if (!releaseAssets.includes(relative)) return;
  event.respondWith((async () => {
    const cache = await caches.open(cacheName);
    const cached = await cache.match(event.request);
    if (cached) return cached;
    const response = await fetch(event.request);
    if (response.ok) await cache.put(event.request, response.clone());
    return response;
  })());
});
