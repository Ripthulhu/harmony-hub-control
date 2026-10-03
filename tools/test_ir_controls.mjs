// node tools/test_ir_controls.mjs; no browser, network, or device required.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import {webcrypto} from 'node:crypto';

const source = fs.readFileSync(new URL('../payload/source/codex_webui.c', import.meta.url), 'utf8');
const pageEnd = source.slice(source.indexOf('static void page_end(FILE *f) {'), source.indexOf('static void status_panel(FILE *f,'));
const script = [...pageEnd.matchAll(/^\s*"((?:\\.|[^"\\])*)"\s*$/gm)]
  .map(m => JSON.parse('"' + m[1] + '"')).join('');
new vm.Script(script);
const controls = script.slice(script.indexOf('function installIrHoldControls(){'),
  script.indexOf('installIrHoldControls();') + 'installIrHoldControls();'.length);
assert(controls.includes("phase:'start'"));

function setup() {
  let now = 0, id = 0, pending, failRenew = false;
  const timers = new Map(), handlers = {}, calls = [], statuses = [];
  const button = {
    disabled: false, classList: {add(){}, remove(){}}, setAttribute(){}, focus(){},
    setPointerCapture(){}, getBoundingClientRect: () => ({left:0, right:50, top:0, bottom:50}),
    closest: s => s === '.ir-send-form' ? form : s === '.ir-send-form button' ? button : null
  };
  const form = {querySelector: () => button, matches: s => s === '.ir-send-form'};
  const surface = {hidden:false, addEventListener: (name, fn) => (handlers[name] ??= []).push(fn)};
  const context = vm.createContext({
    document:surface, window:surface, crypto:webcrypto, Uint32Array, URLSearchParams,
    irFormData: () => ({deviceId:'tv', command:'Volume Up'}),
    irStatus: (...args) => statuses.push(args),
    setTimeout: (fn, ms) => {timers.set(++id, {fn, at:now+ms});return id;},
    clearTimeout: id => timers.delete(id),
    postJson: async (url, data) => {
      calls.push({url, ...data});
      if (data.phase === 'start') return new Promise((resolve,reject) => pending={resolve,reject});
      if (failRenew && data.phase === 'keepalive') throw Error('offline');
      return {ok:true};
    },
    fetch: async (url, opts) => {
      calls.push({url, ...Object.fromEntries(opts.body)});
      assert.equal(opts.keepalive, true);
      return {ok:true};
    }
  });
  vm.runInContext(controls, context);
  const flush = async () => {for(let i=0;i<8;i++) await Promise.resolve();};
  async function advance(ms) {
    const end = now+ms;
    while(true) {
      const due = [...timers].filter(([,t]) => t.at<=end).sort((a,b)=>a[1].at-b[1].at)[0];
      if (!due) break;
      now=due[1].at;timers.delete(due[0]);due[1].fn();await flush();
    }
    now=end;await flush();
  }
  function event(type, data={}) {
    const e={target:button, pointerId:1, button:0, isPrimary:true, detail:1,
      clientX:25, clientY:25, preventDefault(){this.prevented=true;}, ...data};
    for(const fn of handlers[type] || [])fn(e);
    return e;
  }
  return {calls,statuses,event,advance,flush,form,surface,timers,
    finish: async error => {error ? pending.reject(Error(error)) : pending.resolve({ok:true});await flush();},
    failRenew: () => failRenew=true};
}

let t=setup();
t.event('pointerdown');await t.advance(100);t.event('pointerup');await t.flush();
assert.equal(t.calls.filter(c=>c.url==='/api/ir-send').length,1);
assert(t.event('click').prevented);
assert(!t.event('click',{detail:0}).prevented); // Assistive click can still submit.
t.event('submit',{target:t.form});await t.flush();
assert.equal(t.calls.length,2);

for(const stop of ['pointerup','pointercancel','lostpointercapture','pointermove','blur','pagehide','hashchange','visibilitychange','keyup']) {
  t=setup();
  if(stop==='keyup')t.event('keydown',{key:'Enter'});else t.event('pointerdown');
  await t.advance(299);assert.equal(t.calls.length,0);
  await t.advance(501);
  assert.equal(t.calls.filter(c=>c.phase==='start').length,1);
  assert.equal(t.calls.filter(c=>c.phase==='keepalive').length,2);
  t.event('pointerdown',{pointerId:2}); // A second button/finger cannot start another hold.
  t.surface.hidden=true;
  t.event(stop,{clientX:80,key:'Enter'});
  t.event(stop,{clientX:80,key:'Enter'}); // Repeated releases are harmless.
  await t.advance(1000);
  assert.equal(t.calls.filter(c=>c.url==='/api/ir-cancel').length,1,stop);
  assert(!t.calls.some(c=>c.url==='/api/ir-send'),stop);
  await t.finish();assert.equal(t.timers.size,0);
}

t=setup();t.event('pointerdown');t.event('pointermove',{clientX:80});
await t.advance(1000);assert.equal(t.calls.length,0);assert(t.event('click').prevented);
t=setup();t.event('keydown',{key:' '});t.event('keydown',{key:' ',repeat:true});
await t.advance(50);t.event('keyup',{key:' '});await t.flush();
assert.equal(t.calls.length,1);assert.equal(t.calls[0].url,'/api/ir-send');

t=setup();t.event('pointerdown');await t.advance(300);t.failRenew();await t.advance(250);
assert(t.calls.some(c=>c.url==='/api/ir-cancel'));await t.finish();
t=setup();t.event('pointerdown');await t.advance(300);await t.finish('rejected');
assert(t.calls.some(c=>c.url==='/api/ir-cancel'));assert.equal(t.timers.size,0);
t=setup();t.event('pointerdown');await t.advance(300);await t.finish();
t.event('pointerup');await t.flush();assert(!t.calls.some(c=>c.url==='/api/ir-send'));

console.log('IR tap, hold, keepalive, release, keyboard, cancellation, and failure checks passed');
