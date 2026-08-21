#!/usr/bin/env node

// Real-browser boundary test for touch hold, persistent release caching,
// original title/Continue loading after a browser restart, device-rate music,
// and IDBFS restart. The WASM
// polling boundary records deliveries only when ?input-self-test=1 is present,
// proving the input crossed DOM -> generated JS -> ASYNCIFY -> SDL C++.

import childProcess from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import http from 'node:http';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';


function fail(message) {
  throw new Error(`WASM browser input: ${message}`);
}


function parseArguments(argv) {
  if (argv.length < 1) {
    fail('usage: verify-wasm-browser-input.mjs SITE [--browser PATH] ' +
      '[--idbfs-cycles N] [--output FILE]');
  }
  const result = {
    site: path.resolve(argv[0]), browser: '', idbfsCycles: 1, output: '',
  };
  for (let index = 1; index < argv.length; ++index) {
    const option = argv[index];
    if (option === '--idbfs-cycles' && index + 1 < argv.length) {
      result.idbfsCycles = Number(argv[++index]);
    } else if ((option === '--browser' || option === '--output') &&
               index + 1 < argv.length) {
      result[option.slice(2)] = path.resolve(argv[++index]);
    } else {
      fail(`unknown or incomplete option: ${option}`);
    }
  }
  if (!Number.isInteger(result.idbfsCycles) || result.idbfsCycles < 1 ||
      result.idbfsCycles > 10_000) {
    fail('--idbfs-cycles must be an integer from 1 through 10000');
  }
  return result;
}


function browserPath(requested) {
  const candidates = [
    requested,
    process.env.SWD2_BROWSER || '',
    '/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge',
    '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    '/Applications/Chromium.app/Contents/MacOS/Chromium',
    '/usr/bin/microsoft-edge',
    '/usr/bin/google-chrome',
    '/usr/bin/chromium',
  ].filter(Boolean);
  const found = candidates.find(candidate => fs.existsSync(candidate));
  if (!found) fail('no Chromium-family browser found; use --browser PATH');
  return found;
}


function contentType(filename) {
  const extension = path.extname(filename);
  return {
    '.html': 'text/html; charset=utf-8',
    '.js': 'text/javascript; charset=utf-8',
    '.wasm': 'application/wasm',
    '.data': 'application/octet-stream',
  }[extension] || 'application/octet-stream';
}


async function listen(server) {
  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', resolve);
  });
  return server.address().port;
}


async function unusedPort() {
  const probe = net.createServer();
  const port = await listen(probe);
  await new Promise(resolve => probe.close(resolve));
  return port;
}


async function waitForJson(url, timeoutMilliseconds = 15_000) {
  const deadline = Date.now() + timeoutMilliseconds;
  let lastError;
  while (Date.now() < deadline) {
    try {
      const response = await fetch(url);
      if (response.ok) return await response.json();
    } catch (error) {
      lastError = error;
    }
    await new Promise(resolve => setTimeout(resolve, 100));
  }
  fail(`browser debugging endpoint did not start: ${lastError || url}`);
}


class CdpConnection {
  constructor(url) {
    this.socket = new WebSocket(url);
    this.nextId = 1;
    this.pending = new Map();
    this.listeners = new Map();
  }

  async open() {
    await new Promise((resolve, reject) => {
      this.socket.onopen = resolve;
      this.socket.onerror = reject;
    });
    this.socket.onmessage = event => {
      const message = JSON.parse(event.data);
      if (message.id && this.pending.has(message.id)) {
        const { resolve, reject } = this.pending.get(message.id);
        this.pending.delete(message.id);
        if (message.error) reject(new Error(JSON.stringify(message.error)));
        else resolve(message.result);
        return;
      }
      for (const listener of this.listeners.get(message.method) || []) {
        listener(message.params || {});
      }
    };
  }

  on(method, listener) {
    const listeners = this.listeners.get(method) || [];
    listeners.push(listener);
    this.listeners.set(method, listeners);
  }

  send(method, params = {}) {
    const id = this.nextId++;
    return new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
      this.socket.send(JSON.stringify({ id, method, params }));
    });
  }

  async evaluate(expression) {
    const response = await this.send('Runtime.evaluate', {
      expression, awaitPromise: true, returnByValue: true,
    });
    if (response.exceptionDetails) {
      fail(`page evaluation failed: ${response.exceptionDetails.text}`);
    }
    return response.result.value;
  }

  close() {
    this.socket.close();
  }
}


async function waitUntil(action, description, timeoutMilliseconds = 20_000) {
  const deadline = Date.now() + timeoutMilliseconds;
  while (Date.now() < deadline) {
    try {
      if (await action()) return;
    } catch (_) {
      // A navigation/reload temporarily destroys the JavaScript execution
      // context. Retry against the replacement document until the deadline.
    }
    await new Promise(resolve => setTimeout(resolve, 100));
  }
  fail(`timed out waiting for ${description}`);
}


async function stopChild(child, timeoutMilliseconds = 5_000) {
  if (child.exitCode !== null || child.signalCode !== null) return;
  child.kill('SIGTERM');
  const exited = await Promise.race([
    new Promise(resolve => child.once('exit', () => resolve(true))),
    new Promise(resolve => setTimeout(() => resolve(false), timeoutMilliseconds)),
  ]);
  if (!exited && child.exitCode === null && child.signalCode === null) {
    child.kill('SIGKILL');
    await new Promise(resolve => child.once('exit', resolve));
  }
}


function sha256(filename) {
  return crypto.createHash('sha256').update(fs.readFileSync(filename)).digest('hex');
}


const options = parseArguments(process.argv.slice(2));
for (const name of [
  'index.html', 'index.js', 'index.wasm', 'index.data', 'service-worker.js',
]) {
  if (!fs.statSync(path.join(options.site, name), { throwIfNoEntry: false })?.isFile()) {
    fail(`site asset is absent: ${name}`);
  }
}

const server = http.createServer((request, response) => {
  const requestPath = new URL(request.url, 'http://127.0.0.1/').pathname;
  const relative = requestPath === '/' ? 'index.html' : requestPath.slice(1);
  const filename = path.resolve(options.site, relative);
  if (!filename.startsWith(options.site + path.sep) || !fs.existsSync(filename)) {
    response.writeHead(404).end('not found');
    return;
  }
  response.writeHead(200, {
    'Content-Type': contentType(filename),
    'Cache-Control': 'no-store',
    'Cross-Origin-Opener-Policy': 'same-origin',
    'Cross-Origin-Embedder-Policy': 'require-corp',
  });
  fs.createReadStream(filename).pipe(response);
});

const httpPort = await listen(server);
const debuggingPort = await unusedPort();
const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'swd2-browser-input-'));
const executable = browserPath(options.browser);
let browserOutput = '';
const browser = childProcess.spawn(executable, [
  '--headless=new',
  '--disable-gpu',
  '--disable-background-networking',
  '--no-first-run',
  '--no-default-browser-check',
  '--autoplay-policy=no-user-gesture-required',
  `--remote-debugging-port=${debuggingPort}`,
  `--user-data-dir=${profile}`,
  'about:blank',
], { stdio: ['ignore', 'ignore', 'pipe'] });
browser.stderr.on('data', chunk => {
  if (browserOutput.length < 16_384) browserOutput += chunk.toString();
});

let cdp;
try {
  await waitForJson(`http://127.0.0.1:${debuggingPort}/json/version`);
  const targets = await waitForJson(`http://127.0.0.1:${debuggingPort}/json/list`);
  const target = targets.find(entry => entry.type === 'page');
  if (!target) fail('browser did not create a page target');
  cdp = new CdpConnection(target.webSocketDebuggerUrl);
  await cdp.open();
  await cdp.send('Runtime.enable');
  await cdp.send('Page.enable');
  await cdp.send('Network.enable');
  const networkResponses = [];
  cdp.on('Network.responseReceived', event => {
    const response = event.response || {};
    networkResponses.push({
      url: response.url || '',
      status: response.status || 0,
      fromServiceWorker: Boolean(response.fromServiceWorker),
      fromDiskCache: Boolean(response.fromDiskCache),
    });
  });
  await cdp.send('Network.clearBrowserCache');
  await cdp.send('Emulation.setDeviceMetricsOverride', {
    width: 390,
    height: 844,
    deviceScaleFactor: 2,
    mobile: true,
    screenWidth: 390,
    screenHeight: 844,
    screenOrientation: { type: 'portraitPrimary', angle: 0 },
  });
  await cdp.send('Emulation.setTouchEmulationEnabled', {
    enabled: true, maxTouchPoints: 1,
  });
  const url = `http://127.0.0.1:${httpPort}/?input-self-test=1` +
    `&audio-rate-self-test=48000`;
  await cdp.send('Page.navigate', { url });
  await waitUntil(
    () => cdp.evaluate(`Boolean(globalThis.Module &&
      Module.swd2InputSelfTestEnabled && document.getElementById('start-button'))`),
    'instrumented Web shell');

  const startRect = await cdp.evaluate(
    `document.getElementById('start-button').getBoundingClientRect().toJSON()`);
  const startX = startRect.x + startRect.width / 2;
  const startY = startRect.y + startRect.height / 2;
  await cdp.send('Input.dispatchMouseEvent', {
    type: 'mousePressed', x: startX, y: startY, button: 'left', clickCount: 1,
  });
  await cdp.send('Input.dispatchMouseEvent', {
    type: 'mouseReleased', x: startX, y: startY, button: 'left', clickCount: 1,
  });
  await waitUntil(
    () => cdp.evaluate(
      `document.documentElement.dataset.started === 'true' &&
       Array.isArray(Module.swd2InputDeliveries)`),
    'trusted start gesture and WASM runtime');
  await waitUntil(
    () => cdp.evaluate(
      `Boolean(document.documentElement.dataset.canvasBackingAspect)`),
    'SDL canvas backing-store initialization', 60_000);

  const canvasGeometry = await cdp.evaluate(`(() => {
    const canvas = document.getElementById('canvas');
    const stage = document.getElementById('stage').getBoundingClientRect();
    return {
      backingWidth: canvas.width,
      backingHeight: canvas.height,
      backingAspect: document.documentElement.dataset.canvasBackingAspect || '',
      stageShort: Math.min(stage.width, stage.height),
      stageLong: Math.max(stage.width, stage.height),
    };
  })()`);
  if (canvasGeometry.backingWidth * 5 !== canvasGeometry.backingHeight * 8 ||
      canvasGeometry.backingAspect !== '8:5' ||
      Math.abs(canvasGeometry.stageLong / canvasGeometry.stageShort - 8 / 5) > 0.01) {
    fail(`portrait mobile canvas does not preserve 320x200 geometry: ${
      JSON.stringify(canvasGeometry)}`);
  }

  const sleep = milliseconds => new Promise(
    resolve => setTimeout(resolve, milliseconds));
  const key = async (name, code) => {
    const virtualCode = name === 'Enter' ? 13 : 40;
    await cdp.send('Input.dispatchKeyEvent', {
      type: 'keyDown', key: name, code,
      windowsVirtualKeyCode: virtualCode, nativeVirtualKeyCode: virtualCode,
    });
    await cdp.send('Input.dispatchKeyEvent', {
      type: 'keyUp', key: name, code,
      windowsVirtualKeyCode: virtualCode, nativeVirtualKeyCode: virtualCode,
    });
    await sleep(300);
  };

  // MEO accepts three confirms. Deliberately send one more Enter while its
  // uninterruptible fade is still running: it must be fenced before RPG's
  // title rather than immediately selecting the default New Game entry.
  await sleep(1_200);
  for (let index = 0; index < 3; ++index) await key('Enter', 'Enter');
  await key('Enter', 'Enter');
  await waitUntil(
    () => cdp.evaluate(`Module.swd2OpeningMenuEntries >= 1`),
    'RPG opening menu after repeated password confirmation', 30_000);
  await sleep(250);
  const titleInputFence = await cdp.evaluate(`({
    entries: Module.swd2OpeningMenuEntries,
    responses: Module.swd2OpeningMenuResponses,
    waiting: Module.swd2OpeningMenuWaiting,
    worldPolls: Module.swd2WorldPolls
  })`);
  if (titleInputFence.entries !== 1 || titleInputFence.responses !== 0 ||
      !titleInputFence.waiting || titleInputFence.worldPolls !== 0) {
    fail(`password confirmation leaked through the title input fence: ${
      JSON.stringify(titleInputFence)}`);
  }
  const meoChallengePositions = await cdp.evaluate(
    `Module.swd2MeoChallengePositions.slice()`);
  const meoXs = meoChallengePositions.map(entry => entry.x);
  if (meoChallengePositions.length !== 3 ||
      meoChallengePositions.some(entry =>
        entry.x !== Math.min(entry.hundredth, 99) * 2 + 15 ||
        entry.y !== Math.min(entry.second, 55) * 3 + 5) ||
      new Set(meoXs).size !== 3 || Math.max(...meoXs) - Math.min(...meoXs) < 40) {
    fail(`MEO challenge arrow did not use the original second/hundredth ` +
      `positions: ${JSON.stringify(meoChallengePositions)}`);
  }

  // RPG Continue uses Down, Confirm, slot-one Confirm and default-Yes Confirm.
  await key('ArrowDown', 'ArrowDown');
  for (let index = 0; index < 3; ++index) await key('Enter', 'Enter');
  await sleep(2_500);

  await waitUntil(
    () => cdp.evaluate(
      `Number(Module.swd2AudioSynthesisRate) > 0 &&
       Number(Module.swd2AudioContextRate) > 0`),
    'browser music synthesis rate', 30_000);
  const audioRuntime = await cdp.evaluate(`({
    synthesisRate: Number(Module.swd2AudioSynthesisRate),
    contextRate: Number(Module.swd2AudioContextRate),
    contextState: Module.SDL2.audioContext.state
  })`);
  if (audioRuntime.synthesisRate !== 48_000 ||
      audioRuntime.contextRate !== 48_000 ||
      audioRuntime.contextState !== 'running') {
    fail(`browser audio synthesis did not follow the 48-kHz Web Audio ` +
      `device clock: ${JSON.stringify(audioRuntime)}`);
  }

  await cdp.evaluate(
    `Module.swd2InputDeliveries.length = 0;
     Module.swd2WorldSamples.length = 0;
     Module.swd2DirectionQueue.length = 0;
     Module.swd2HeldDirection = 0`);
  const directionRect = await cdp.evaluate(
    `document.querySelector('[data-key="ArrowRight"]').getBoundingClientRect().toJSON()`);
  const x = directionRect.x + directionRect.width / 2;
  const y = directionRect.y + directionRect.height / 2;
  await cdp.send('Input.dispatchTouchEvent', {
    type: 'touchStart',
    touchPoints: [{ x, y, id: 7, radiusX: 3, radiusY: 3, force: 1 }],
  });
  const requestedWorldFrames = 69;
  const touchStartMilliseconds = await cdp.evaluate('performance.now()');
  await waitUntil(
    () => cdp.evaluate(
      `Module.swd2InputDeliveries.length >= ${requestedWorldFrames}`),
    `${requestedWorldFrames} consecutive WASM world polls from one touchStart`,
    8_000);
  await waitUntil(
    () => cdp.evaluate(
      `Module.swd2WorldSamples.length >= ${requestedWorldFrames}`),
    `${requestedWorldFrames} presented world positions during the touch hold`,
    2_000);
  const held = await cdp.evaluate(`({
    held: Module.swd2HeldDirection,
    queue: Module.swd2DirectionQueue.slice(),
    deliveries: Module.swd2InputDeliveries.slice(),
    worldSamples: Module.swd2WorldSamples.slice(),
    now: performance.now()
  })`);
  await cdp.send('Input.dispatchTouchEvent', {
    type: 'touchEnd', touchPoints: [],
  });
  // CDP resolves dispatchTouchEvent when the renderer has accepted the task,
  // not necessarily after the DOM touchend callback has run. Fence on the
  // shell's held level reaching zero before measuring post-release delivery;
  // one world poll already executing before that fence is not a post-release
  // action and may append its diagnostic while the event task is queued.
  await waitUntil(
    () => cdp.evaluate('Module.swd2HeldDirection === 0'),
    'DOM touchend release fence', 2_000);
  const releaseFence = await cdp.evaluate(`({
    held: Module.swd2HeldDirection,
    queue: Module.swd2DirectionQueue.slice(),
    deliveries: Module.swd2InputDeliveries.slice()
  })`);
  await sleep(250);
  const released = await cdp.evaluate(`({
    held: Module.swd2HeldDirection,
    queue: Module.swd2DirectionQueue.slice(),
    deliveries: Module.swd2InputDeliveries.slice(),
    rotated: getComputedStyle(document.querySelector('main')).transform !== 'none'
  })`);

  if (held.held !== 4 || held.queue.length !== 0) {
    fail('trusted touchStart did not remain held after its quick-tap queue was consumed');
  }
  if (held.deliveries.length < requestedWorldFrames ||
      held.deliveries.some(entry => entry.direction !== 4 || !entry.everyFrame)) {
    fail(`69-world-frame hold produced ${held.deliveries.length} valid deliveries`);
  }
  if (held.worldSamples.length < requestedWorldFrames ||
      held.worldSamples.some(entry => entry.direction !== 4)) {
    fail(`69-world-frame hold produced ${held.worldSamples.length} valid world positions`);
  }
  const worldStart = held.worldSamples[0];
  const worldFinish = held.worldSamples.at(-1);
  if (worldStart.worldX === worldFinish.worldX &&
      worldStart.worldY === worldFinish.worldY) {
    fail('trusted touch hold reached the RPG world loop but did not move the actor');
  }
  const deliverySpan = held.deliveries.at(-1).milliseconds -
    held.deliveries[0].milliseconds;
  if (deliverySpan < 1_000) {
    fail(`direction deliveries did not span the hold (${deliverySpan} ms)`);
  }
  if (releaseFence.held !== 0 || releaseFence.queue.length !== 0 ||
      released.held !== 0 || released.queue.length !== 0 ||
      released.deliveries.length !== releaseFence.deliveries.length) {
    fail('trusted touchEnd did not stop the world direction immediately: ' +
      JSON.stringify({ held, releaseFence, released }));
  }
  if (!released.rotated) fail('portrait mobile layout did not apply rotation');

  // The first successful runtime fills a versioned Cache Storage release.
  // The next document must be controlled by that worker and obtain the large
  // JS/WASM/DATA assets from it instead of downloading the package again.
  await waitUntil(
    () => cdp.evaluate(
      `Boolean(document.documentElement.dataset.resourceCache)`),
    'versioned persistent release cache', 30_000);
  const cacheProbe = await cdp.evaluate(`(async () => {
    const names = await caches.keys();
    const entries = {};
    for (const name of names) {
      entries[name] = (await (await caches.open(name)).keys()).map(
        request => request.url);
    }
    return {
      state: document.documentElement.dataset.resourceCache || '',
      controller: Boolean(navigator.serviceWorker.controller),
      registrations: (await navigator.serviceWorker.getRegistrations()).map(
        registration => ({
          scope: registration.scope,
          installing: registration.installing && registration.installing.state,
          waiting: registration.waiting && registration.waiting.state,
          active: registration.active && registration.active.state
        })),
      caches: names, entries
    };
  })()`);
  if (!cacheProbe.state) {
    fail(`timed out waiting for versioned persistent release cache: ${
      JSON.stringify(cacheProbe)}`);
  }
  const resourceCache = await cdp.evaluate(`(async () => {
    const version = document.getElementById('build-version').textContent
      .replace(/^\\s*版本\\s*/, '').trim();
    const name = 'swd2-web-' + version;
    const cache = await caches.open(name);
    const entries = (await cache.keys()).map(request => request.url).sort();
    return {
      version, name, entries,
      state: document.documentElement.dataset.resourceCache,
      controller: Boolean(navigator.serviceWorker.controller),
      localVersion: localStorage.getItem('swd2-cache-version')
    };
  })()`);
  if (!['installed', 'active'].includes(resourceCache.state)) {
    fail(`persistent release cache is ${resourceCache.state}: ${
      JSON.stringify(resourceCache)}`);
  }
  for (const name of ['index.js', 'index.wasm', 'index.data']) {
    if (!resourceCache.entries.some(entry =>
      entry.endsWith(`${name}?v=${resourceCache.version}`))) {
      fail(`persistent release cache omitted ${name}`);
    }
  }
  if (resourceCache.localVersion !== resourceCache.version) {
    fail('persistent release cache did not record its completed version');
  }

  // Create the same marker written after an explicit in-game Record. Leave a
  // nonzero value in SAVE+51c as ordinary saves are allowed to do: direct
  // loading must not mistake it for OC's post-battle entity callback. Stage a
  // two-cell clear AREA1 path and cursor 1000h as well. The next document fixes
  // RPG's load-time DOS hundredth to one, producing the odd cursor 1001h; 130
  // alternating legal steps must then cross the random encounter's initial
  // enemy page and reach the FIG command compositor rather than aborting there.
  await cdp.evaluate(`new Promise((resolve, reject) => {
    const save = FS.readFile('/saves/SAVE.DA1');
    const word = (offset, value) => {
      save[offset] = value & 255;
      save[offset + 1] = value >>> 8 & 255;
    };
    word(0x012, 38);      // actor screen x -> world x 20 at viewport zero
    word(0x02a, 80);      // actor screen y -> world y 12 at viewport zero
    word(0x0a2, 9);       // face right
    word(0x40d, 8);       // AREA1 first cell
    word(0x40f, 8);
    word(0x41b, 0);       // viewport x/y
    word(0x41d, 0);
    word(0x424, 8);       // AREA1 MAPZ location directory offset
    word(0x49c, 0x1000);  // becomes deliberately odd 1001h after load
    save[0x51c] = 42;
    save[0x51d] = 0;
    FS.writeFile('/saves/SAVE.DA1', save);
    FS.writeFile('/saves/.swd2-last-slot', '1\\n');
    FS.syncfs(false, error => error ? reject(error) : resolve(true));
  })`);
  networkResponses.length = 0;
  await cdp.send('Page.navigate', {
    url: `http://127.0.0.1:${httpPort}/?input-self-test=1` +
      `&audio-rate-self-test=48000&clock-hundredth-self-test=1` +
      `&original-title=1`,
  });
  await waitUntil(
    () => cdp.evaluate(`Boolean(globalThis.Module &&
      document.getElementById('start-button') &&
      document.getElementById('start-button').textContent ===
        '点击进入并开启声音')`),
    'original-only browser entry gate', 30_000);
  const originalEntry = await cdp.evaluate(`({
    label: document.getElementById('start-button').textContent,
    externalResumeButton: Boolean(document.getElementById('title-button')),
    arguments: Module.arguments.slice()
  })`);
  const resumeStartRect = await cdp.evaluate(
    `document.getElementById('start-button').getBoundingClientRect().toJSON()`);
  const resumeStartX = resumeStartRect.x + resumeStartRect.width / 2;
  const resumeStartY = resumeStartRect.y + resumeStartRect.height / 2;
  await cdp.send('Input.dispatchMouseEvent', {
    type: 'mousePressed', x: resumeStartX, y: resumeStartY,
    button: 'left', clickCount: 1,
  });
  await cdp.send('Input.dispatchMouseEvent', {
    type: 'mouseReleased', x: resumeStartX, y: resumeStartY,
    button: 'left', clickCount: 1,
  });
  await waitUntil(
    () => cdp.evaluate(`document.documentElement.dataset.startRoute ===
        'original-title' &&
      document.documentElement.dataset.started === 'true' &&
      Array.isArray(Module.swd2InputDeliveries)`),
    'browser entry routed to original password/title flow', 30_000);
  await waitUntil(
    () => cdp.evaluate(
      `Boolean(document.documentElement.dataset.canvasBackingAspect)`),
    'restarted SDL canvas initialization', 60_000);
  await sleep(1_200);
  for (let index = 0; index < 3; ++index) await key('Enter', 'Enter');
  await waitUntil(
    () => cdp.evaluate(`Module.swd2OpeningMenuWaiting === true &&
      Module.swd2OpeningMenuEntries >= 1`),
    'original RPG title after password on restart', 30_000);
  await key('ArrowDown', 'ArrowDown');
  for (let index = 0; index < 3; ++index) await key('Enter', 'Enter');
  await sleep(2_500);
  await cdp.evaluate(
    `Module.swd2InputDeliveries.length = 0;
     Module.swd2WorldSamples.length = 0;
     Module.swd2WorldPolls = 0;
     Module.swd2BattleCommandPages.length = 0;
     Module.swd2DirectionQueue.length = 0;
     Module.swd2HeldDirection = 0`);
  await waitUntil(
    () => cdp.evaluate(`Module.swd2WorldPolls >= 1`),
    'original Continue loaded RPG world loop', 8_000);

  let encounterInputs = 0;
  for (; encounterInputs < 170; ++encounterInputs) {
    const right = (encounterInputs & 1) === 0;
    const name = right ? 'ArrowRight' : 'ArrowLeft';
    const virtualCode = right ? 39 : 37;
    await cdp.send('Input.dispatchKeyEvent', {
      type: 'keyDown', key: name, code: name,
      windowsVirtualKeyCode: virtualCode, nativeVirtualKeyCode: virtualCode,
    });
    await cdp.send('Input.dispatchKeyEvent', {
      type: 'keyUp', key: name, code: name,
      windowsVirtualKeyCode: virtualCode, nativeVirtualKeyCode: virtualCode,
    });
    await sleep(45);
    if (await cdp.evaluate(`Module.swd2BattleCommandPages.length > 0`)) {
      ++encounterInputs;
      break;
    }
  }
  await waitUntil(
    () => cdp.evaluate(`Module.swd2BattleCommandPages.length > 0`),
    'loaded odd-cursor encounter command page', 8_000);
  const resumeRuntime = await cdp.evaluate(`({
    worldPolls: Module.swd2WorldPolls,
    battleCommandPages: Module.swd2BattleCommandPages.slice(),
    clockHundredth: Module.swd2ClockHundredthSelfTest,
    route: document.documentElement.dataset.startRoute,
    openingMenuEntries: Module.swd2OpeningMenuEntries
  })`);
  const resumeAudio = await cdp.evaluate(`({
    synthesisRate: Number(Module.swd2AudioSynthesisRate),
    contextRate: Number(Module.swd2AudioContextRate),
    contextState: Module.SDL2.audioContext.state
  })`);
  const resumedBattlePage = resumeRuntime.battleCommandPages[0];
  if (resumeRuntime.route !== 'original-title' ||
      originalEntry.externalResumeButton ||
      originalEntry.arguments.includes('--resume-save') ||
      resumeRuntime.openingMenuEntries < 2 || resumeRuntime.worldPolls < 130 ||
      resumeRuntime.clockHundredth !== 1 || !resumedBattlePage ||
      (resumedBattlePage.randomCursor & 1) !== 1) {
    fail('original slot-one Continue did not reach the FIG command page with ' +
      `its loaded odd cursor: ${JSON.stringify({
        encounterInputs, resumeRuntime
      })}`);
  }
  if (resumeAudio.synthesisRate !== 48_000 ||
      resumeAudio.contextRate !== 48_000 ||
      resumeAudio.contextState !== 'running') {
    fail(`music loaded through original Continue lost the Web Audio device rate: ${
      JSON.stringify(resumeAudio)}`);
  }
  const cachedSecondLoad = Object.fromEntries(
    ['index.js', 'index.wasm', 'index.data'].map(name => {
      const suffix = `${name}?v=${resourceCache.version}`;
      const responses = networkResponses.filter(entry => entry.url.endsWith(suffix));
      return [name, responses.some(entry => entry.fromServiceWorker)];
    }));
  if (!Object.values(cachedSecondLoad).every(Boolean)) {
    fail(`second launch did not use the persistent service-worker cache: ${
      JSON.stringify({ cachedSecondLoad, networkResponses })}`);
  }

  // Exercise the Web shell's actual two-load IDBFS probe in the same browser
  // profile. The first document writes and syncs a token before reloading;
  // only the second document may expose data-idbfs-self-test=pass after it
  // reads the exact persisted bytes and removes the probe again.
  let idbfs;
  for (let cycle = 0; cycle < options.idbfsCycles; ++cycle) {
    const idbfsToken = `edge-restart-roundtrip-${cycle}`;
    await cdp.send('Page.navigate', {
      url: `http://127.0.0.1:${httpPort}/?idbfs-self-test=${idbfsToken}`,
    });
    await waitUntil(
      () => cdp.evaluate(
        `['pass', 'fail'].includes(document.documentElement.dataset.idbfsSelfTest)`),
      `IDBFS cycle ${cycle} write, reload and byte restoration`, 30_000);
    idbfs = await cdp.evaluate(`({
      result: document.documentElement.dataset.idbfsSelfTest,
      status: document.getElementById('status').textContent,
      error: document.getElementById('error').textContent,
      gateHidden: document.getElementById('start-gate').hidden,
      sessionToken: sessionStorage.getItem('swd2-idbfs-self-test-token')
    })`);
    if (idbfs.result !== 'pass' || idbfs.error || !idbfs.gateHidden ||
        idbfs.sessionToken !== null) {
      fail(`IDBFS restart cycle ${cycle} did not finish cleanly: ` +
        JSON.stringify(idbfs));
    }
  }

  const version = await cdp.send('Browser.getVersion');
  const report = {
    schema_version: 1,
    kind: 'wasm_browser_runtime_checks',
    status: 'verified',
    limitation: 'Headless desktop Edge with mobile emulation is not a physical iOS/Android device test.',
    browser: version.product,
    protocol_version: version.protocolVersion,
    host: { platform: process.platform, architecture: process.arch },
    viewport: { width: 390, height: 844, device_scale_factor: 2 },
    layout: {
      portrait_rotation_applied: true,
      canvas_backing: {
        width: canvasGeometry.backingWidth,
        height: canvasGeometry.backingHeight,
        aspect: canvasGeometry.backingAspect,
      },
      stage_long_to_short_ratio:
        canvasGeometry.stageLong / canvasGeometry.stageShort,
      right_button_rect: directionRect,
    },
    resource_cache: {
      version: resourceCache.version,
      name: resourceCache.name,
      first_runtime_state: resourceCache.state,
      first_runtime_controlled: resourceCache.controller,
      cached_urls: resourceCache.entries,
      second_launch_from_service_worker: cachedSecondLoad,
    },
    audio: {
      synthesis_rate_hz: audioRuntime.synthesisRate,
      forced_test_context_rate_hz: audioRuntime.contextRate,
      context_state: audioRuntime.contextState,
      resumed_synthesis_rate_hz: resumeAudio.synthesisRate,
      resumed_context_rate_hz: resumeAudio.contextRate,
      resumed_context_state: resumeAudio.contextState,
      conversion: 'RIX/VOC generation at the Web Audio device rate',
    },
    original_continue_after_restart: {
      marker_slot: 1,
      retained_nonzero_battle_auxiliary: 42,
      browser_gate_label: originalEntry.label,
      external_resume_button: originalEntry.externalResumeButton,
      command_line_resume_argument: originalEntry.arguments.includes('--resume-save'),
      route: resumeRuntime.route,
      password_then_original_title: true,
      original_title_menu_entries: resumeRuntime.openingMenuEntries,
      selected_original_slot: 1,
      clock_hundredth_override: resumeRuntime.clockHundredth,
      staged_random_cursor: 0x1000,
      movement_inputs_before_battle: encounterInputs,
      world_polls_before_battle: resumeRuntime.worldPolls,
      battle_command_page: resumedBattlePage,
    },
    title_input_fence: {
      repeated_confirm_during_password_fade: 'discarded',
      opening_menu_entries_before_user_choice: titleInputFence.entries,
      opening_menu_responses_before_user_choice: titleInputFence.responses,
      opening_menu_waiting_for_user_choice: titleInputFence.waiting,
    },
    meo_challenge: {
      clock_fields: 'DOS DH second / DL hundredth',
      positions: meoChallengePositions,
      horizontal_span_pixels: Math.max(...meoXs) - Math.min(...meoXs),
    },
    gesture: {
      touch_start_events: 1,
      requested_world_frames: requestedWorldFrames,
      hold_milliseconds: held.now - touchStartMilliseconds,
      touch_end_events: 1,
      direction: 'right',
      wasm_world_deliveries: held.deliveries.length,
      first_to_last_delivery_milliseconds: deliverySpan,
      deliveries_before_release_fence:
        releaseFence.deliveries.length - held.deliveries.length,
      deliveries_after_release_fence: 0,
      presented_world_samples: held.worldSamples.length,
      world_start: {
        x: worldStart.worldX, y: worldStart.worldY,
        screen_x: worldStart.screenX, screen_y: worldStart.screenY,
        viewport_x: worldStart.viewportX, viewport_y: worldStart.viewportY,
      },
      world_finish: {
        x: worldFinish.worldX, y: worldFinish.worldY,
        screen_x: worldFinish.screenX, screen_y: worldFinish.screenY,
        viewport_x: worldFinish.viewportX, viewport_y: worldFinish.viewportY,
      },
    },
    idbfs: {
      cycles: options.idbfsCycles,
      initial_sync: 'completed',
      exact_probe_write: 'completed',
      page_reloads: options.idbfsCycles,
      restored_bytes: 'exact',
      cleanup_sync: 'completed',
      result: idbfs.result,
    },
    assets: Object.fromEntries(
      ['index.html', 'index.js', 'index.wasm', 'index.data', 'service-worker.js'].map(
        name => [name, sha256(path.join(options.site, name))])),
  };
  if (options.output) {
    fs.mkdirSync(path.dirname(options.output), { recursive: true });
    fs.writeFileSync(options.output, JSON.stringify(report, null, 2) + '\n');
  }
  console.log(
    `WASM browser input: OK (${held.deliveries.length} world-frame ` +
    `deliveries and ${held.worldSamples.length} presented world positions from ` +
    `one uninterrupted trusted touch hold; release stopped at once; ` +
    `music synthesis matched the 48-kHz Web Audio device clock; ` +
    `versioned assets came from persistent cache; original password/title/` +
    `slot-one Continue reached a FIG command page after ${encounterInputs} ` +
    `loaded odd-cursor ` +
    `encounter inputs; repeated password confirmation stopped at the title; ` +
    `${options.idbfsCycles} IDBFS restart cycles passed)`);
} catch (error) {
  if (browserOutput) console.error(browserOutput.slice(-4_000));
  throw error;
} finally {
  if (cdp) cdp.close();
  await stopChild(browser);
  await new Promise(resolve => server.close(resolve));
  fs.rmSync(profile, {
    recursive: true, force: true, maxRetries: 10, retryDelay: 100,
  });
}
