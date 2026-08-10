#!/usr/bin/env node

// Real-browser boundary test for the touch-hold and IDBFS restart paths. It launches an
// installed Chromium-family browser with mobile emulation, sends one trusted
// CDP touchStart, holds it for one second, and sends one touchEnd.  The WASM
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
  }

  async open() {
    await new Promise((resolve, reject) => {
      this.socket.onopen = resolve;
      this.socket.onerror = reject;
    });
    this.socket.onmessage = event => {
      const message = JSON.parse(event.data);
      if (!message.id || !this.pending.has(message.id)) return;
      const { resolve, reject } = this.pending.get(message.id);
      this.pending.delete(message.id);
      if (message.error) reject(new Error(JSON.stringify(message.error)));
      else resolve(message.result);
    };
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


function sha256(filename) {
  return crypto.createHash('sha256').update(fs.readFileSync(filename)).digest('hex');
}


const options = parseArguments(process.argv.slice(2));
for (const name of ['index.html', 'index.js', 'index.wasm', 'index.data']) {
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
  const url = `http://127.0.0.1:${httpPort}/?input-self-test=1`;
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

  // MEO accepts three confirms. After its fade, RPG Continue uses Down,
  // Confirm, slot-one Confirm and default-Yes Confirm.
  await sleep(1_200);
  for (let index = 0; index < 3; ++index) await key('Enter', 'Enter');
  await sleep(1_500);
  await key('ArrowDown', 'ArrowDown');
  for (let index = 0; index < 3; ++index) await key('Enter', 'Enter');
  await sleep(2_500);

  await cdp.evaluate(
    `Module.swd2InputDeliveries.length = 0;
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
  await sleep(1_000);
  const held = await cdp.evaluate(`({
    held: Module.swd2HeldDirection,
    queue: Module.swd2DirectionQueue.slice(),
    deliveries: Module.swd2InputDeliveries.slice()
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
  if (held.deliveries.length < 10 ||
      held.deliveries.some(entry => entry.direction !== 4 || !entry.everyFrame)) {
    fail(`one-second world hold produced ${held.deliveries.length} valid deliveries`);
  }
  const deliverySpan = held.deliveries.at(-1).milliseconds -
    held.deliveries[0].milliseconds;
  if (deliverySpan < 500) {
    fail(`direction deliveries did not span the hold (${deliverySpan} ms)`);
  }
  if (releaseFence.held !== 0 || releaseFence.queue.length !== 0 ||
      released.held !== 0 || released.queue.length !== 0 ||
      released.deliveries.length !== releaseFence.deliveries.length) {
    fail('trusted touchEnd did not stop the world direction immediately: ' +
      JSON.stringify({ held, releaseFence, released }));
  }
  if (!released.rotated) fail('portrait mobile layout did not apply rotation');

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
    layout: { portrait_rotation_applied: true, right_button_rect: directionRect },
    gesture: {
      touch_start_events: 1,
      hold_milliseconds: 1_000,
      touch_end_events: 1,
      direction: 'right',
      wasm_world_deliveries: held.deliveries.length,
      first_to_last_delivery_milliseconds: deliverySpan,
      deliveries_before_release_fence:
        releaseFence.deliveries.length - held.deliveries.length,
      deliveries_after_release_fence: 0,
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
      ['index.html', 'index.js', 'index.wasm', 'index.data'].map(
        name => [name, sha256(path.join(options.site, name))])),
  };
  if (options.output) {
    fs.mkdirSync(path.dirname(options.output), { recursive: true });
    fs.writeFileSync(options.output, JSON.stringify(report, null, 2) + '\n');
  }
  console.log(
    `WASM browser input: OK (${held.deliveries.length} world-frame ` +
    `deliveries from one 1000ms trusted touch hold; release stopped at once; ` +
    `${options.idbfsCycles} IDBFS restart cycles passed)`);
} catch (error) {
  if (browserOutput) console.error(browserOutput.slice(-4_000));
  throw error;
} finally {
  if (cdp) cdp.close();
  browser.kill('SIGTERM');
  await new Promise(resolve => server.close(resolve));
  fs.rmSync(profile, { recursive: true, force: true });
}
