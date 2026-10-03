#!/usr/bin/env node

import fs from 'node:fs';
import pathModule from 'node:path';
import { fileURLToPath } from 'node:url';
import vm from 'node:vm';

const __dirname = pathModule.dirname(fileURLToPath(import.meta.url));

const IRDB_BASE = 'https://cdn.jsdelivr.net/gh/probonopd/irdb@master/codes/';
const FLIPPER_BASE = 'https://cdn.jsdelivr.net/gh/Lucaslhm/Flipper-IRDB@main/';
const FLIPPER_INDEX = 'https://api.github.com/repos/Lucaslhm/Flipper-IRDB/git/trees/main?recursive=1';
const LIRC_BASE = 'https://raw.githubusercontent.com/probonopd/lirc-remotes/master/';
const LIRC_INDEX = 'https://api.github.com/repos/probonopd/lirc-remotes/git/trees/master?recursive=1';
const LIRC_JSDELIVR_INDEX = 'https://data.jsdelivr.com/v1/package/gh/probonopd/lirc-remotes@master/flat';
const SMARTIR_BASE = 'https://raw.githubusercontent.com/smartHomeHub/SmartIR/master/';
const SMARTIR_INDEX = 'https://api.github.com/repos/smartHomeHub/SmartIR/git/trees/master?recursive=1';
const SMARTIR_JSDELIVR_INDEX = 'https://data.jsdelivr.com/v1/package/gh/smartHomeHub/SmartIR@master/flat';

function arg(name, fallback = '') {
  const eq = process.argv.find((x) => x.startsWith(`--${name}=`));
  if (eq) return eq.slice(name.length + 3);
  const i = process.argv.indexOf(`--${name}`);
  return i >= 0 ? (process.argv[i + 1] || fallback) : fallback;
}

function has(name) {
  return process.argv.includes(`--${name}`);
}

const sampleCount = Math.max(1, Math.min(500, Number(arg('sample', '10')) || 10));
const perDevice = Math.max(1, Math.min(80, Number(arg('per-device', '12')) || 12));
const hub = arg('hub', '').replace(/\/$/, '');
const seedText = arg('seed', String(Date.now()));
const sourceArg = arg('source', 'all').toLowerCase();
const doConfigure = has('configure') && !!hub;
const doDryRun = has('dry-run') || !doConfigure;
const showUnsupported = has('show-unsupported');
const pathFilters = process.argv
  .filter((x) => x.startsWith('--path='))
  .map((x) => x.slice('--path='.length).toLowerCase())
  .filter(Boolean);
let webuiParseIrText = null;

let randState = 0;
for (const ch of seedText) randState = (randState * 31 + ch.charCodeAt(0)) >>> 0;
if (!randState) randState = 0x12345678;

function random() {
  randState = (randState * 1664525 + 1013904223) >>> 0;
  return randState / 0x100000000;
}

function pickMany(items, count) {
  const copy = items.slice();
  const out = [];
  while (copy.length && out.length < count) {
    out.push(copy.splice(Math.floor(random() * copy.length), 1)[0]);
  }
  return out;
}

function loadWebuiParser() {
  if (webuiParseIrText) return webuiParseIrText;
  const sourcePath = pathModule.join(__dirname, '..', 'payload', 'source', 'codex_webui.c');
  const c = fs.readFileSync(sourcePath, 'utf8');
  const script = [...c.matchAll(/^\s*"((?:\\.|[^"\\])*)"\s*$/gm)]
    .map((m) => JSON.parse(`"${m[1]}"`))
    .join('');
  const start = script.indexOf('const IRDB_BASE');
  const end = script.indexOf('async function postJson');
  if (start < 0 || end < 0) throw new Error('could not extract web UI IR parser');
  const context = {
    console,
    fetch,
    atob: (s) => Buffer.from(String(s || '').replace(/\s+/g, ''), 'base64').toString('binary'),
    document: {
      createElement: () => ({
        _v: '',
        set innerHTML(v) {
          this._v = String(v || '')
            .replace(/&lt;/g, '<')
            .replace(/&gt;/g, '>')
            .replace(/&amp;/g, '&')
            .replace(/&quot;/g, '"')
            .replace(/&#39;/g, "'");
        },
        get value() {
          return this._v;
        },
      }),
    },
    DOMParser: class {
      parseFromString() {
        return { querySelector: () => null, querySelectorAll: () => [] };
      }
    },
    $: () => null,
    history: {},
    location: {},
  };
  vm.createContext(context);
  vm.runInContext(`${script.slice(start, end)};this.__parseIrText=parseIrText;`, context);
  webuiParseIrText = (body, source, path) => context.__parseIrText(body, source, path);
  return webuiParseIrText;
}

function safeImportName(s) {
  return String(s || 'Command').replace(/[|"\r\n\\]/g, ' ').replace(/\s+/g, ' ').trim().slice(0, 96) || 'Command';
}

async function text(url) {
  const r = await fetch(url);
  if (!r.ok) throw new Error(`${url} -> HTTP ${r.status}`);
  return r.text();
}

async function json(url) {
  const r = await fetch(url);
  if (!r.ok) throw new Error(`${url} -> HTTP ${r.status}`);
  return r.json();
}

async function loadIndex() {
  const sources = [];
  if (sourceArg === 'all' || sourceArg === 'irdb') {
    const idx = await text(`${IRDB_BASE}index`);
    sources.push(...idx.replace(/\r/g, '').split('\n').map((x) => x.trim()).filter((x) => x.endsWith('.csv')).map((path) => ({ source: 'irdb', path })));
  }
  if (sourceArg === 'all' || sourceArg === 'flipper') {
    try {
      const tree = await json(FLIPPER_INDEX);
      sources.push(...(tree.tree || []).map((x) => x.path).filter((x) => x && x.endsWith('.ir')).map((path) => ({ source: 'flipper', path })));
    } catch (e) {
      console.log(`skip flipper index: ${e.message || e}`);
      if (sourceArg === 'flipper') throw e;
    }
  }
  if (sourceArg === 'all' || sourceArg === 'lirc') {
    try {
      let paths;
      const tree = await json(LIRC_INDEX);
      paths = (tree.tree || [])
        .filter((x) => x.type === 'blob')
        .map((x) => (x.path || '').replace(/^\//, ''));
      sources.push(...paths
        .filter((x) => x && x !== 'README.md' && !/\.(png|jpg|jpeg|gif|md|html)$/i.test(x))
        .map((path) => ({ source: 'lirc', path })));
    } catch (e1) {
      try {
        console.log(`fallback lirc index via jsDelivr: ${e1.message || e1}`);
        const tree = await json(LIRC_JSDELIVR_INDEX);
        const paths = (tree.files || []).map((x) => (x.name || '').replace(/^\//, ''));
        sources.push(...paths
          .filter((x) => x && x !== 'README.md' && !/\.(png|jpg|jpeg|gif|md|html)$/i.test(x))
          .map((path) => ({ source: 'lirc', path })));
      } catch (e2) {
        console.log(`skip lirc index: ${e2.message || e2}`);
        if (sourceArg === 'lirc') throw e2;
      }
    }
  }
  if (sourceArg === 'all' || sourceArg === 'smartir') {
    try {
      let paths;
      const tree = await json(SMARTIR_INDEX);
      paths = (tree.tree || [])
        .filter((x) => x.type === 'blob')
        .map((x) => (x.path || '').replace(/^\//, ''));
      sources.push(...paths
        .filter((x) => /^codes\/.+\.json$/i.test(x))
        .map((path) => ({ source: 'smartir', path })));
    } catch (e1) {
      try {
        console.log(`fallback smartir index via jsDelivr: ${e1.message || e1}`);
        const tree = await json(SMARTIR_JSDELIVR_INDEX);
        const paths = (tree.files || []).map((x) => (x.name || '').replace(/^\//, ''));
        sources.push(...paths
          .filter((x) => /^codes\/.+\.json$/i.test(x))
          .map((path) => ({ source: 'smartir', path })));
      } catch (e2) {
        console.log(`skip smartir index: ${e2.message || e2}`);
        if (sourceArg === 'smartir') throw e2;
      }
    }
  }
  if (!sources.length) throw new Error(`no database files loaded for source=${sourceArg}`);
  return sources;
}

async function parseEntry(entry) {
  let url;
  if (entry.source === 'irdb') url = `${IRDB_BASE}${entry.path}`;
  else if (entry.source === 'flipper') url = `${FLIPPER_BASE}${entry.path}`;
  else if (entry.source === 'lirc') url = `${LIRC_BASE}${entry.path}`;
  else if (entry.source === 'smartir') url = `${SMARTIR_BASE}${entry.path}`;
  else throw new Error(`unknown source ${entry.source}`);
  const body = await text(url);
  const rows = loadWebuiParser()(body, entry.source, entry.path);
  return { ...entry, url, rows };
}

function summarize(parsed) {
  const unsupported = new Map();
  let supported = 0;
  let compact = 0;
  let raw = 0;
  for (const row of parsed.rows) {
    if (row.keycode || row.raw) {
      supported++;
      if (row.raw) raw++;
      else compact++;
    } else {
      const key = row.protocol || row.meta || 'unknown';
      unsupported.set(key, (unsupported.get(key) || 0) + 1);
    }
  }
  return { supported, compact, raw, unsupported };
}

function payloadLines(parsed, limit) {
  return parsed.rows.filter((r) => r.keycode || r.raw).slice(0, limit).map((r) => {
    const name = safeImportName(r.name);
    return r.raw ? `${name}|raw|${r.raw}` : `${name}|${r.keycode}`;
  });
}

function pathProfile(path) {
  const bits = path.replace(/\.[^.]+$/, '').split(/[\\/]/).filter(Boolean);
  const model = bits[bits.length - 1] || 'Database Device';
  const manufacturer = bits.length >= 2 ? bits[bits.length - 2] : 'Database';
  return {
    manufacturer: manufacturer.replace(/[_-]+/g, ' ').slice(0, 64) || 'Database',
    model: model.replace(/[_-]+/g, ' ').slice(0, 64) || 'Database Device',
  };
}

async function postForm(path, data) {
  const body = new URLSearchParams(data);
  const r = await fetch(`${hub}${path}`, { method: 'POST', body, headers: { 'Content-Type': 'application/x-www-form-urlencoded' } });
  const bodyText = await r.text();
  if (!r.ok) throw new Error(`${path} HTTP ${r.status}: ${bodyText.slice(0, 180)}`);
  return bodyText;
}

async function postJson(path, data) {
  const body = new URLSearchParams(data);
  const r = await fetch(`${hub}${path}`, { method: 'POST', body, headers: { 'Content-Type': 'application/x-www-form-urlencoded' } });
  const bodyText = await r.text();
  let parsed;
  try {
    parsed = JSON.parse(bodyText);
  } catch {
    throw new Error(`${path} returned non-JSON: ${bodyText.slice(0, 180)}`);
  }
  if (!r.ok || parsed.ok === false) throw new Error(parsed.error || parsed.message || `${path} HTTP ${r.status}`);
  return parsed;
}

async function inventory() {
  const r = await fetch(`${hub}/api/inventory`);
  if (!r.ok) throw new Error(`/api/inventory HTTP ${r.status}`);
  return r.json();
}

async function configureParsed(parsed, index) {
  const lines = payloadLines(parsed, perDevice);
  if (!lines.length) return { configured: false, message: 'no supported commands to import' };
  const profile = pathProfile(parsed.path);
  const name = `Smoke ${index + 1} ${profile.manufacturer} ${profile.model}`.replace(/\s+/g, ' ').slice(0, 96);
  await postForm('/ir/new-device', {
    name,
    manufacturer: profile.manufacturer,
    model: profile.model,
    type: 'HomeAppliance',
  });
  const inv = await inventory();
  const device = (inv.devices || []).find((d) => d.name === name);
  if (!device) throw new Error(`created device not found in inventory: ${name}`);
  const result = await postJson('/api/irdb-import', { deviceId: device.id, payload: lines.join('\n') });
  return { configured: true, deviceId: device.id, name, message: result.message || '' };
}

async function main() {
  console.log(`seed=${seedText} source=${sourceArg} sample=${sampleCount} perDevice=${perDevice} configure=${doConfigure} dryRun=${doDryRun}`);
  const index = await loadIndex();
  console.log(`loaded ${index.length} database file entries`);
  let sample = pickMany(index, sampleCount);
  if (pathFilters.length) {
    sample = [];
    for (const filter of pathFilters) {
      const found = index.find((entry) => entry.path.toLowerCase() === filter)
        || index.find((entry) => entry.path.toLowerCase().includes(filter));
      if (!found) throw new Error(`path filter did not match a database file: ${filter}`);
      sample.push(found);
    }
  }
  const report = [];
  const totals = { files: 0, rows: 0, supported: 0, compact: 0, raw: 0, configured: 0 };
  const protocolGaps = new Map();
  for (let i = 0; i < sample.length; i++) {
    const entry = sample[i];
    try {
      const parsed = await parseEntry(entry);
      const sum = summarize(parsed);
      totals.files++;
      totals.rows += parsed.rows.length;
      totals.supported += sum.supported;
      totals.compact += sum.compact;
      totals.raw += sum.raw;
      for (const [k, v] of sum.unsupported.entries()) protocolGaps.set(k, (protocolGaps.get(k) || 0) + v);
      let config = { configured: false, message: doDryRun ? 'dry-run' : '' };
      if (doConfigure) {
        config = await configureParsed(parsed, i);
        if (config.configured) totals.configured++;
      }
      report.push({ entry, sum, config });
      const unsupported = parsed.rows.length - sum.supported;
      console.log(`${i + 1}. ${entry.source} ${entry.path}: rows=${parsed.rows.length} supported=${sum.supported} compact=${sum.compact} raw=${sum.raw} unsupported=${unsupported}${config.configured ? ` -> ${config.name} (${config.deviceId})` : ''}`);
      if (showUnsupported && unsupported) {
        const examples = parsed.rows
          .filter((row) => !(row.raw || row.keycode))
          .slice(0, 8)
          .map((row) => `${row.name || '(unnamed)'} / ${row.protocol || row.meta || 'unknown'}`);
        console.log(`   unsupported examples: ${examples.join(' | ')}`);
      }
    } catch (e) {
      console.log(`${i + 1}. ${entry.source} ${entry.path}: ERROR ${e.message || e}`);
      report.push({ entry, error: String(e.message || e) });
    }
  }
  console.log('\nsummary');
  console.log(JSON.stringify(totals, null, 2));
  const gaps = Array.from(protocolGaps.entries()).sort((a, b) => b[1] - a[1]).slice(0, 20);
  if (gaps.length) {
    console.log('\nunsupported protocols / parser gaps');
    for (const [name, count] of gaps) console.log(`${String(name).padEnd(24)} ${count}`);
  }
}

main().catch((e) => {
  console.error(e.stack || e.message || e);
  process.exit(1);
});
