// Execute the firmware's actual JS. Hardware/API behavior is tested separately.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

const page = readFileSync(new URL('../fleabyte/web_assets.h', import.meta.url), 'utf8');
const js = page.match(/<script>([\s\S]*?)<\/script>/)[1];
function element() {
  return {
    value: '', textContent: '', hidden: false, disabled: false, dataset: {}, attrs: {},
    classList: { toggle() {} }, addEventListener() {}, appendChild() {},
    scrollIntoView() {}, querySelector() { return element(); },
    setAttribute(key, value) { this.attrs[key] = String(value); },
  };
}
const elements = new Map();
const get = id => {
  if (!elements.has(id)) elements.set(id, element());
  return elements.get(id);
};
const calls = [];
const orientation = [1, 3, 0, 2].map(rot => {
  const button = element(); button.dataset.rot = rot; return button;
});
let locked = false;
let rejectSecurity = false;
let rejectHardLock = false, rejectReset = false, confirmAction = true;
const settings = {
  startDelay: 0, standbyOnBoot: 1, screenLocked: 0, screen: 1, screenBright: 100,
  led: 1, ledBright: 20, rotation: 1, showAccess: 1, layout: 'us', layouts: [],
  unlockSeq: 'SSSLL', hardlockSeq: 'SSSSS', hardlockEnabled: 1,
  longPressMs: 200, hardlockReinserts: 1, longPressMin: 50, longPressMax: 5000,
};
const state = { state: 'idle', payloads: [], layout: 'us', arrangement: 'QWERTY', armed: 0 };
const context = vm.createContext({
  document: { querySelector: get,
    querySelectorAll: selector => selector === '.orient button' ? orientation : [],
    createElement: element },
  window: { scrollTo() {} }, console, URLSearchParams, setInterval() {},
  setTimeout, clearTimeout, confirm: () => confirmAction,
  async fetch(path, options) {
    const body = options ? Object.fromEntries(options.body) : {};
    calls.push({ path, body });
    if (path === '/api/lock-display') locked = true;
    if (path === '/api/unlock-display') locked = false;
    let value = { ok: true, screenLocked: +locked };
    if (path === '/api/state') value = { ...state, screenLocked: +locked };
    if (path === '/api/log?since=0') value = { seq: 0, text: '' };
    if (path === '/api/settings') value = { ...settings, screenLocked: +locked };
    if (path === '/api/hard-lock') value = { ok: true, hardlockReinserts: 2 };
    const failed = (path === '/api/settings/security' && rejectSecurity) ||
      (path === '/api/hard-lock' && rejectHardLock) ||
      (path === '/api/settings/reset' && rejectReset);
    return { ok: !failed, status: failed ? 400 : 200,
      json: async () => failed ? { error: path === '/api/hard-lock' ? 'Flash write failed' :
        path === '/api/settings/reset' ? 'Reset failed' : 'Server rejected gestures' } : value };
  },
});
vm.runInContext(js, context);
await new Promise(resolve => setImmediate(resolve));
calls.length = 0;

for (const [unlock, hard] of [['S', 'S'], ['LSSLL', 'SS'], ['SS', 'LSSL']]) {
  get('#unlockseq').value = unlock;
  get('#hardlockseq').value = hard;
  await get('#savesecurity').onclick();
  assert.match(get('#securitystatus').textContent, /neither may contain/);
  assert.equal(calls.length, 0, 'Conflicting gestures must not be submitted');
}
get('#unlockseq').value = ' sssll ';
get('#hardlockseq').value = ' sssss ';
await get('#savesecurity').onclick();
assert.equal(calls.at(-1).body.unlockSeq, 'SSSLL');
assert.equal(calls.at(-1).body.hardlockSeq, 'SSSSS');
assert.match(get('#securitystatus').textContent, /next boot/);
rejectSecurity = true;
await get('#savesecurity').onclick();
assert.equal(get('#securitystatus').textContent, 'Server rejected gestures');
rejectSecurity = false;
get('#standbysw').onclick();
assert.equal(get('#standbysw').attrs['aria-checked'], 'false');
await get('#savesecurity').onclick();
assert.equal(calls.at(-1).body.standbyOnBoot, '0');

// A pending brightness preview must be cancelled when locking.
get('#screenbright').value = '60';
get('#screenbright').oninput();

await get('#lockdisplay').onclick();
assert.equal(calls.at(-1).path, '/api/lock-display');
assert.equal(get('#lockdisplay').attrs['aria-label'], 'Unlock screen');
assert.equal(get('#screenlockhint').hidden, false);
assert.equal(get('#screenlocknotice').hidden, false);
assert.equal(get('#unlockscreen').hidden, false);
for (const control of [get('#screensw'), get('#screenbright'), get('#accesssw'), ...orientation]) {
  assert.equal(control.disabled, true);
}
calls.length = 0;
get('#screenbright').value = '40';
get('#screenbright').oninput();
get('#screensw').onclick();
get('#accesssw').onclick();
orientation[0].onclick();
await new Promise(resolve => setTimeout(resolve, 100));
assert.equal(calls.length, 0, 'Locked controls and cancelled previews must not submit');

// LED remains usable, and neither its preview nor Apply sends screen fields.
get('#ledbright').value = '40';
get('#ledbright').oninput();
await new Promise(resolve => setTimeout(resolve, 100));
assert.equal(calls.at(-1).path, '/api/settings/display');
assert.equal(calls.at(-1).body.preview, '1');
assert.equal(calls.at(-1).body.ledBright, '40');
for (const key of ['screen', 'screenBright', 'rotation', 'showAccess']) assert.equal(key in calls.at(-1).body, false);
await get('#savedisplay').onclick();
assert.equal(get('#dispstatus').textContent, 'LED saved');
for (const key of ['screen', 'screenBright', 'rotation', 'showAccess']) assert.equal(key in calls.at(-1).body, false);
await get('#unlockscreen').onclick();
assert.equal(calls.at(-1).path, '/api/unlock-display');
assert.equal(get('#lockdisplay').attrs['aria-label'], 'Lock screen');
assert.equal(get('#screenlockhint').hidden, true);
assert.equal(get('#screenlocknotice').hidden, true);
assert.equal(get('#unlockscreen').hidden, true);
assert.equal(get('#screenbright').disabled, false);
get('#screenbright').value = '40';
get('#screenbright').oninput();
await new Promise(resolve => setTimeout(resolve, 100));
assert.equal(calls.at(-1).body.screenBright, '40');

// Physical button locks arrive through polling and disable the same controls.
locked = true;
await vm.runInContext('refresh()', context);
assert.equal(get('#screenbright').disabled, true);
await get('#unlockscreen').onclick();

// Reset errors stay beside the reset action instead of appearing under Wi-Fi.
rejectReset = true;
get('#wifistatus').textContent = '';
await get('#reset').onclick();
assert.equal(get('#resetstatus').textContent, 'Reset failed');
assert.equal(get('#wifistatus').textContent, '');
rejectReset = false;

// Hard lock cancels cleanly, reports persistence failures, and shows recovery.
calls.length = 0;
get('#overlay').hidden = true;
confirmAction = false;
await get('#hardlocknow').onclick();
assert.equal(calls.length, 0);
confirmAction = true;
rejectHardLock = true;
await get('#hardlocknow').onclick();
assert.equal(get('#lockresetstatus').textContent, 'Flash write failed');
assert.equal(get('#hardlocknow').disabled, false);
assert.equal(get('#overlay').hidden, true);
rejectHardLock = false;
await get('#hardlocknow').onclick();
assert.equal(calls.at(-1).path, '/api/hard-lock');
assert.equal(get('#overlay').hidden, false);
assert.equal(get('#overlaytitle').textContent, 'Device hard-locked');
assert.match(get('#newnet').textContent, /Replug 3 times/);
assert.equal(vm.runInContext('poll', context), false);
console.log('PASS: disabled screen controls, UI unlock, LED updates, startup switch, reset errors, hard-lock action/errors');
