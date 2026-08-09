#!/usr/bin/env node

import fs from 'node:fs';
import vm from 'node:vm';

const htmlPath = process.argv[2];
if (!htmlPath) throw new Error('usage: verify-web-shell.mjs INDEX.HTML');
const html = fs.readFileSync(htmlPath, 'utf8');
const marker = '<script>';
if (!html.includes(marker)) throw new Error('Web shell has no inline script');
const code = html.split(marker, 2)[1].split('</script>', 1)[0];

class Element {
  constructor(id = '') {
    this.id = id;
    this.dataset = {};
    this.hidden = false;
    this.listeners = {};
    this.classList = { add() {} };
    this.textContent = '';
  }

  addEventListener(type, listener) {
    (this.listeners[type] ||= []).push(listener);
  }

  focus() {}
  setPointerCapture() {}
  dispatchEvent() {}
}

const ids = Object.fromEntries(
  ['canvas', 'status-wrap', 'status', 'progress', 'error',
   'start-gate', 'start-button'].map(id => [id, new Element(id)]));
const documentListeners = {};
const document = {
  documentElement: new Element('html'),
  visibilityState: 'visible',
  getElementById: id => ids[id],
  querySelectorAll: () => [],
  addEventListener(type, listener) {
    (documentListeners[type] ||= []).push(listener);
  },
};
const windowListeners = {};
class AudioContextMock {
  constructor() { this.state = 'suspended'; }
  resume() {
    this.state = 'running';
    return Promise.resolve();
  }
}
const window = {
  AudioContext: AudioContextMock,
  addEventListener(type, listener) {
    (windowListeners[type] ||= []).push(listener);
  },
};

let orientationRequests = 0;
const screen = {
  orientation: {
    lock(mode) {
      if (mode !== 'landscape') throw new Error('unexpected orientation mode');
      ++orientationRequests;
      return Promise.resolve();
    },
  },
};
let wakeRequests = 0;
const navigator = {
  wakeLock: {
    async request(mode) {
      if (mode !== 'screen') throw new Error('unexpected Wake Lock mode');
      ++wakeRequests;
      return { addEventListener() {} };
    },
  },
};
const dependencies = new Set();
const context = {
  console,
  document,
  window,
  screen,
  navigator,
  location: { search: '' },
  URLSearchParams,
  FS: {
    mkdir() {},
    mount() {},
    syncfs(_load, callback) { callback(null); },
  },
  IDBFS: {},
  addRunDependency(name) { dependencies.add(name); },
  removeRunDependency(name) {
    if (!dependencies.delete(name)) {
      throw new Error(`attempted to remove unknown dependency ${name}`);
    }
  },
  setTimeout,
  clearTimeout,
  setInterval,
  clearInterval,
  KeyboardEvent: class {},
};

vm.createContext(context);
vm.runInContext(code, context);
context.Module.preRun[0]();
if (!dependencies.has('swd2-user-start') ||
    dependencies.has('swd2-idbfs')) {
  throw new Error('the game did not wait only for the user start gesture');
}

await ids['start-button'].listeners.click[0]({ preventDefault() {} });
await Promise.resolve();
if (dependencies.size !== 0) {
  throw new Error('the start gesture did not release the runtime dependency');
}
if (!ids['start-gate'].hidden) throw new Error('the start gate remained visible');
if (context.Module.SDL2.audioContext.state !== 'running') {
  throw new Error('the start gesture did not unlock the SDL Web Audio context');
}
if (orientationRequests !== 1 || wakeRequests !== 1) {
  throw new Error('the start gesture did not request landscape and Wake Lock');
}

console.log('Web shell start gesture smoke: OK');
