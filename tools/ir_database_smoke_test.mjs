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
const seedText = arg('seed', String(Date.now()));
const sourceArg = arg('source', 'all').toLowerCase();
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
  const script = fs.readFileSync(pathModule.join(__dirname, '..', 'payload/www/profiles.js'), 'utf8');
  const context = {
    window: {},
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
  };
  vm.createContext(context);
  vm.runInContext(script, context);
  webuiParseIrText = (body, source, path) => context.window.harmonyParseProfile(body, source, path);
  return webuiParseIrText;
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

async function main() {
  if (has('configure') || arg('hub')) throw new Error('This parser check is read-only. Import profiles through the paired hub UI.');
  console.log(`seed=${seedText} source=${sourceArg} sample=${sampleCount} readOnly=true`);
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
  const totals = { files: 0, rows: 0, supported: 0, compact: 0, raw: 0, errors: 0 };
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
      const unsupported = parsed.rows.length - sum.supported;
      console.log(`${i + 1}. ${entry.source} ${entry.path}: rows=${parsed.rows.length} supported=${sum.supported} compact=${sum.compact} raw=${sum.raw} unsupported=${unsupported}`);
      if (showUnsupported && unsupported) {
        const examples = parsed.rows
          .filter((row) => !(row.raw || row.keycode))
          .slice(0, 8)
          .map((row) => `${row.name || '(unnamed)'} / ${row.protocol || row.meta || 'unknown'}`);
        console.log(`   unsupported examples: ${examples.join(' | ')}`);
      }
    } catch (e) {
      console.log(`${i + 1}. ${entry.source} ${entry.path}: ERROR ${e.message || e}`);
      totals.errors++;
    }
  }
  console.log('\nsummary');
  console.log(JSON.stringify(totals, null, 2));
  if (totals.errors) process.exitCode = 1;
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
