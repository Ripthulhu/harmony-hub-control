import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
const source=fs.readFileSync(new URL('../payload/www/app.js',import.meta.url),'utf8');
new vm.Script(source);
const code=source.slice(source.indexOf('async function tap('),source.indexOf("['blur','pagehide']"));
function setup(){
  let now=0, sequence=0;
  const timers=new Map(), calls=[], handlers={}, state={online:true,hold:null,devices:[{id:'tv',transport:'ir'}]};
  const button={dataset:{device:'tv',command:'VolumeUp'},classList:{add(){},remove(){}},setAttribute(){},setPointerCapture(){},
    getBoundingClientRect:()=>({left:0,top:0,right:60,bottom:60}),addEventListener:(name,fn)=>handlers[name]=fn};
  const context=vm.createContext({state,Date:{now:()=>now},notice(){},
    setTimeout:(fn,ms)=>{timers.set(++sequence,{fn,at:now+ms});return sequence;},clearTimeout:id=>timers.delete(id),
    setInterval:()=>++sequence,clearInterval(){},
    api:async(path,body)=>{calls.push({path,...body});return {id:'operation-1'};},operation:()=>new Promise(()=>{})});
  vm.runInContext(code+'\nbindCommands(root);',vm.createContext({...context,root:{querySelectorAll:()=>[button]}}));
  const flush=async()=>{for(let i=0;i<12;i++)await Promise.resolve();};
  async function advance(ms){now+=ms;for(const [id,t] of [...timers])if(t.at<=now){timers.delete(id);t.fn();}await flush();}
  function event(type,props={}){handlers[type]?.({button:0,pointerId:1,key:'Enter',repeat:false,clientX:30,clientY:30,preventDefault(){},...props});}
  return {event,advance,flush,calls,state};
}
let t=setup();t.event('pointerdown');await t.advance(100);t.event('pointerup');t.event('click');await t.flush();
assert.equal(t.calls.filter(c=>c.path==='commands/send').length,1);
assert.equal(t.calls[0].mode,'tap');
for(const stop of ['pointerup','keyup','pointercancel','lostpointercapture','blur','pointermove']){
  t=setup();t.event(stop==='keyup'?'keydown':'pointerdown');await t.advance(1600);
  assert.equal(t.calls[0].mode,'hold');
  t.event(stop,stop==='pointermove'?{clientX:100}:{});if(stop==='pointerup'||stop==='keyup')t.event('click');await t.flush();
  assert.equal(t.state.hold,null);
  assert.equal(t.calls.filter(c=>c.action==='cancel').length,1);
  assert.equal(t.calls.filter(c=>c.path==='commands/send').length,1,'release must not send an extra tap');
}
console.log('New remote: touch, keyboard, long-hold release, pointer cancellation and synthetic-click suppression passed');
const remoteCode=source.slice(source.indexOf('const norm ='),source.indexOf('function renderRemote()'));
const remote=vm.createContext({state:{devices:[{id:'tv',commands:[{name:'VolumeUp'}]}],config:{activities:[{id:'watch',buttons:[{slot:'volumeup',deviceId:'tv',command:'VolumeUp',label:'Volume'}]}]}},readLocal:key=>key==='harmony-activity'?'watch':'tv',escape:s=>String(s),icon:s=>s});
vm.runInContext(remoteCode,remote);
assert.match(vm.runInContext('remoteButton("volumeup",activeDevice())',remote),/data-command="VolumeUp" data-device="tv"/);
assert.equal(vm.runInContext('remoteButton("ok",activeDevice())',remote),'');
console.log('Activity remote assignments target the saved device and hide unassigned controls');
vm.runInContext("state.config.activities=[];state.devices[0].commands=[{name:'Vol_dn'},{name:'Ch_next'},{name:'Ch_prev'}]",remote);
for(const [slot,command] of [['volumedown','Vol_dn'],['channelup','Ch_next'],['channeldown','Ch_prev']]){
  assert.match(vm.runInContext(`remoteButton('${slot}',activeDevice())`,remote),new RegExp(`data-command="${command}"`));
}
console.log('Imported LG volume-down and channel command aliases resolve correctly');
vm.runInContext("state.devices[0].commands=[{name:'Power On'},{name:'Power Off'}]",remote);
assert.match(vm.runInContext('remoteButton("poweron",activeDevice())',remote),/>On<\/span>/);
assert.match(vm.runInContext('remoteButton("poweroff",activeDevice())',remote),/>Off<\/span>/);

const ui=vm.createContext({escape:value=>String(value).replaceAll('&','&amp;').replaceAll('<','&lt;'),icon:()=>'',deviceOptions:()=>'<option>TV</option>',commandOptions:()=>'<option>Power</option>'});
vm.runInContext(source.slice(source.indexOf('function deviceTypeOptions('),source.indexOf('const norm =')),ui);
assert.match(vm.runInContext('deviceTypeOptions("TV & projector")',ui),/<option selected>TV &amp; projector<\/option>/);
assert.equal((vm.runInContext('deviceTypeOptions("Television")',ui).match(/>Television</g)||[]).length,1);
vm.runInContext(source.slice(source.indexOf('function activityStep('),source.indexOf('function renderActivityEditor(')),ui);
const delay=vm.runInContext('activityStep({deviceId:"",delayMs:500},"steps",0)',ui);
assert.match(delay,/Delay \(ms\)/);assert.doesNotMatch(delay,/data-key="command"|data-key="role"/);
const action=vm.runInContext('activityStep({deviceId:"tv",delayMs:500,role:"power-on"},"steps",0)',ui);
assert.match(action,/Delay after \(ms\)/);assert.match(action,/value="power-on" selected/);

const inputs=Object.fromEntries(['learn-mode','learn-name','learn-raw','learn-code','learn-protocol','save-learned'].map(id=>[id,{value:''}]));
ui.$=id=>inputs[id];
vm.runInContext(source.slice(source.indexOf('function updateLearnSave('),source.indexOf('function renderLearn(')),ui);
function canSave(mode,name,raw='',code='',protocol='2'){
  for(const [id,value] of Object.entries({'learn-mode':mode,'learn-name':name,'learn-raw':raw,'learn-code':code,'learn-protocol':protocol}))inputs[id].value=value;
  vm.runInContext('updateLearnSave()',ui);return !inputs['save-learned'].disabled;
}
assert.equal(canSave('raw','Volume up','9000 4500 560'),true);
assert.equal(canSave('raw','Volume up','','20DF40BF'),false);
assert.equal(canSave('nec','Volume up','','20DF40BF'),true);
assert.equal(canSave('keycode','Volume up','','0020',''),false);
assert.equal(canSave('keycode','Volume up','','0020'),true);
assert.equal(canSave('nec',' ','','20DF40BF'),false);
let routed='settings';ui.state={config:{}};ui.location={hash:'#main'};ui.show=view=>{routed=view;};
ui.window={addEventListener:(name,handler)=>ui.onHash=handler};
vm.runInContext(source.slice(source.indexOf("window.addEventListener('hashchange'"),source.indexOf("$('connection').addEventListener")),ui);
ui.onHash();assert.equal(routed,'settings');
ui.location.hash='#devices';ui.onHash();assert.equal(routed,'devices');
console.log('Power labels, imported device types, activity fields, manual codes and keyboard skip navigation passed');
