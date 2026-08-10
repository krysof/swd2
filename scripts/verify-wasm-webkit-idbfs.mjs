#!/usr/bin/env node

// Independent WebKit boundary for the browser save mount.  The Chromium CDP
// runner also verifies trusted touch input; this runner deliberately limits
// its claim to an actual WebKit process executing the shell's two-document
// IDBFS write/reload/read/delete cycle.

import crypto from 'node:crypto';
import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import { pathToFileURL } from 'node:url';


function fail(message) {
  throw new Error(`WASM WebKit IDBFS: ${message}`);
}


function parseArguments(argv) {
  if (argv.length < 1) {
    fail('usage: verify-wasm-webkit-idbfs.mjs SITE ' +
      '[--playwright PACKAGE_OR_INDEX] [--cycles N] ' +
      '[--reference FILE] [--output FILE]');
  }
  const result = {
    site: path.resolve(argv[0]), playwright: '', cycles: 1,
    reference: '', output: '',
  };
  for (let index = 1; index < argv.length; ++index) {
    const option = argv[index];
    if (option === '--cycles' && index + 1 < argv.length) {
      result.cycles = Number(argv[++index]);
    } else if ((option === '--playwright' || option === '--reference' ||
         option === '--output') &&
        index + 1 < argv.length) {
      result[option.slice(2)] = path.resolve(argv[++index]);
    } else {
      fail(`unknown or incomplete option: ${option}`);
    }
  }
  if (!Number.isInteger(result.cycles) || result.cycles < 1 ||
      result.cycles > 10_000) {
    fail('--cycles must be an integer from 1 through 10000');
  }
  return result;
}


function contentType(filename) {
  return {
    '.html': 'text/html; charset=utf-8',
    '.js': 'text/javascript; charset=utf-8',
    '.wasm': 'application/wasm',
    '.data': 'application/octet-stream',
  }[path.extname(filename)] || 'application/octet-stream';
}


async function listen(server) {
  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', resolve);
  });
  return server.address().port;
}


function sha256(filename) {
  return crypto.createHash('sha256').update(fs.readFileSync(filename)).digest('hex');
}


async function loadPlaywright(requested) {
  if (!requested) {
    try {
      return await import('playwright');
    } catch (error) {
      fail('Playwright is not importable; pass --playwright PACKAGE_OR_INDEX ' +
        `(${error.message})`);
    }
  }
  let entry = requested;
  if (fs.statSync(entry, { throwIfNoEntry: false })?.isDirectory()) {
    entry = path.join(entry, 'index.mjs');
  }
  if (!fs.statSync(entry, { throwIfNoEntry: false })?.isFile()) {
    fail(`Playwright entry does not exist: ${entry}`);
  }
  return await import(pathToFileURL(entry).href);
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

const port = await listen(server);
const playwright = await loadPlaywright(
  options.playwright || process.env.SWD2_PLAYWRIGHT || '');
if (!playwright.webkit) fail('Playwright package has no WebKit browser type');

let browser;
try {
  browser = await playwright.webkit.launch({ headless: true });
  const context = await browser.newContext({
    viewport: { width: 390, height: 844 },
    deviceScaleFactor: 2,
    isMobile: true,
    hasTouch: true,
  });
  const page = await context.newPage();
  const errors = [];
  let topLevelNavigations = 0;
  page.on('pageerror', error => errors.push(error.message));
  page.on('framenavigated', frame => {
    if (frame === page.mainFrame() && frame.url().startsWith('http://127.0.0.1:')) {
      ++topLevelNavigations;
    }
  });

  let state;
  let reloadAbortErrorCount = 0;
  for (let cycle = 0; cycle < options.cycles; ++cycle) {
    const navigationStart = topLevelNavigations;
    const errorStart = errors.length;
    const token = `webkit-restart-roundtrip-${cycle}`;
    await page.goto(`http://127.0.0.1:${port}/?idbfs-self-test=${token}`, {
      waitUntil: 'domcontentloaded', timeout: 30_000,
    });
    const deadline = Date.now() + 45_000;
    let result = '';
    while (Date.now() < deadline) {
      try {
        result = await page.evaluate(
          `document.documentElement.dataset.idbfsSelfTest || ''`);
        if (result === 'pass' || result === 'fail') break;
      } catch (_) {
        // The first document intentionally reloads while this loop is polling.
      }
      await new Promise(resolve => setTimeout(resolve, 100));
    }
    if (result !== 'pass') {
      fail(`two-load probe cycle ${cycle} finished with ${result || 'timeout'}`);
    }
    state = await page.evaluate(`({
      result: document.documentElement.dataset.idbfsSelfTest,
      status: document.getElementById('status').textContent,
      error: document.getElementById('error').textContent,
      gateHidden: document.getElementById('start-gate').hidden,
      sessionToken: sessionStorage.getItem('swd2-idbfs-self-test-token'),
      navigationType: performance.getEntriesByType('navigation')[0]?.type || '',
      userAgent: navigator.userAgent
    })`);
    // WebKit reports one rejected loader promise when the first self-test
    // document intentionally reloads while index.data/index.wasm requests are
    // still in flight. It is an observed navigation abort, not an IDBFS error.
    const cycleErrors = errors.slice(errorStart);
    const reloadAbortErrors = cycleErrors.filter(
      error => error === 'TypeError: Load failed');
    const unexpectedErrors = cycleErrors.filter(
      error => error !== 'TypeError: Load failed');
    if (state.error || !state.gateHidden || state.sessionToken !== null ||
        topLevelNavigations - navigationStart !== 2 ||
        reloadAbortErrors.length > 1 || unexpectedErrors.length !== 0) {
      fail(`restart cycle ${cycle} differs: ${JSON.stringify({
        state,
        topLevelNavigations: topLevelNavigations - navigationStart,
        reloadAbortErrors,
        unexpectedErrors,
      })}`);
    }
    reloadAbortErrorCount += reloadAbortErrors.length;
  }

  const report = {
    schema_version: 1,
    kind: 'wasm_webkit_idbfs_restart',
    status: 'verified',
    limitation: 'Headless Playwright WebKit is not a physical iOS device or branded Safari test.',
    browser: `Playwright WebKit ${browser.version()}`,
    host: { platform: process.platform, architecture: process.arch },
    viewport: { width: 390, height: 844, device_scale_factor: 2 },
    idbfs: {
      cycles: options.cycles,
      initial_sync: 'completed',
      exact_probe_write: 'completed',
      top_level_navigations: topLevelNavigations,
      restored_bytes: 'exact',
      cleanup_sync: 'completed',
      intentional_reload_abort_errors: reloadAbortErrorCount,
      result: state.result,
    },
    user_agent: state.userAgent,
    assets: Object.fromEntries(
      ['index.html', 'index.js', 'index.wasm', 'index.data'].map(
        name => [name, sha256(path.join(options.site, name))])),
  };
  if (options.reference) {
    const expected = JSON.parse(fs.readFileSync(options.reference, 'utf8'));
    delete expected.captured_at;
    if (JSON.stringify(report) !== JSON.stringify(expected)) {
      fail('runtime report differs from the checked WebKit reference');
    }
  }
  if (options.output) {
    fs.mkdirSync(path.dirname(options.output), { recursive: true });
    fs.writeFileSync(options.output, JSON.stringify(report, null, 2) + '\n');
  }
  console.log(
    `WASM WebKit IDBFS: OK (${options.cycles} cycles, ` +
    `${topLevelNavigations} documents, exact restore and cleanup)`);
} finally {
  if (browser) await browser.close();
  await new Promise(resolve => server.close(resolve));
}
