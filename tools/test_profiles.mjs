import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const context = vm.createContext({
  window: {}, atob,
  document: {createElement: () => ({
    set innerHTML(value) {this.value = String(value).replaceAll('&amp;', '&').replaceAll('&lt;', '<').replaceAll('&gt;', '>');}
  })},
  DOMParser: class {parseFromString() {return {querySelector: () => null, querySelectorAll: () => []};}}
});
vm.runInContext(fs.readFileSync(new URL('../payload/www/profiles.js', import.meta.url), 'utf8'), context);
const parse = context.window.harmonyParseProfile;
const pronto = '0000 006D 0002 0000 0157 00AC 0015 0015';
const fixtures = [
  ['irdb', 'tv.csv', 'functionname,protocol,device,subdevice,function\nPower,NEC,4,-1,8'],
  ['flipper', 'tv.ir', 'Filetype: IR signals file\nVersion: 1\n#\nname: Power\ntype: parsed\nprotocol: NEC\naddress: 04 00 00 00\ncommand: 08 00 00 00'],
  ['flipper', 'raw.ir', 'Filetype: IR signals file\nVersion: 1\n#\nname: Power\ntype: raw\nfrequency: 38000\nduty_cycle: 0.33\ndata: 9000 4500 560 560'],
  ['lirc', 'tv.conf', 'begin remote\nname tv\nfrequency 38000\nbegin raw_codes\nname Power\n9000 4500 560 560\nend raw_codes\nend remote'],
  ['smartir', 'tv.json', '{"commands":{"Power":"F9470P100S100P100S100"}}'],
  ['custom', 'tv.csv', 'name,raw\nPower,F9470P100S100P100S100'],
  ['custom', 'tv.girr', '<girr><command name="Power"><raw>9000 4500 560 560</raw></command></girr>'],
  ['custom', 'tv.txt', 'Power: '+pronto],
  ['custom', 'tv.txt', 'Power: 9000 4500 560 560'],
  ['custom', 'tv.txt', 'Power: sendir,1:1,1,38000,1,1,342,171,21,21'],
  ['remotecentral', 'tv.html', '<html>Power<br>'+pronto+' 0015 0015 0015 0015 0015 0015</html>']
];
for (const [source, path, text] of fixtures) {
  const rows = parse(text, source, path);
  assert(rows.some(row => row.raw || row.keycode), source+' / '+path);
}
const compact = parse(fixtures[0][2], 'irdb', 'tv.csv');
assert.equal(compact[0].keycode, 'G:Toshiba 32 Bit:(0x20DF10EF)(Repeat)():3');
assert.equal(parse(fixtures[0][2]+'\nPower again,NEC,4,-1,8', 'irdb', 'tv.csv').length, 1);
assert.equal(parse('{broken', 'smartir', 'tv.json').length, 0);
assert.equal(parse('', 'custom', '').length, 0);
console.log('Standalone profile parsers: '+fixtures.length+' offline formats, NEC encoding, duplicate removal and malformed input passed');
