// Keeps the page on the phone after the first visit, so a tap or scan opens
// it with no signal. The page is the same for every device (?n= only picks
// which one to connect to), so every link is answered from one cached copy.
const CACHE = 'tapui-v1';
const FILES = ['./', 'index.html', 'app.js', 'proto.js', 'style.css', 'manifest.webmanifest', 'icon.svg'];

self.addEventListener('install', (event) => {
  event.waitUntil(caches.open(CACHE).then((c) => c.addAll(FILES)).then(() => self.skipWaiting()));
});

self.addEventListener('activate', (event) => {
  event.waitUntil((async () => {
    for (const key of await caches.keys()) if (key !== CACHE) await caches.delete(key);
    await self.clients.claim();
  })());
});

// Cache first, refresh behind it: off grid a weak signal would otherwise
// leave the page hanging on a request that never finishes.
self.addEventListener('fetch', (event) => {
  const req = event.request;
  if (req.method !== 'GET' || new URL(req.url).origin !== location.origin) return;
  const key = req.mode === 'navigate' ? 'index.html' : req;
  event.respondWith((async () => {
    const cache = await caches.open(CACHE);
    const cached = await cache.match(key, { ignoreSearch: true });
    const fresh = fetch(req).then(async (res) => {
      if (res.ok) await cache.put(key, res.clone());
      return res;
    });
    if (cached) {
      event.waitUntil(fresh.catch(() => {}));
      return cached;
    }
    return fresh;
  })());
});
