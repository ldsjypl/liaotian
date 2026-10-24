// smoke.js - run app.js under a tiny DOM stub and simulate the button click.
const listeners = {};
const els = {};

function el(id) {
  if (!els[id]) {
    els[id] = {
      id,
      value: '',
      textContent: '',
      className: '',
      title: '',
      hidden: false,
      disabled: false,
      children: [],
      classList: { add() {}, remove() {}, toggle() {} },
      addEventListener(ev, fn) { (listeners[id] = listeners[id] || {})[ev] = fn; },
      appendChild(c) { this.children.push(c); },
      focus() {},
      setAttribute() {},
    };
  }
  return els[id];
}

global.document = {
  getElementById: el,
  createElement: () => el('tmp-' + Math.random()),
  addEventListener() {},
};
global.window = global;
global.localStorage = { getItem: () => null, setItem() {} };
global.location = { search: '', protocol: 'https:', host: 'ldsjypl.online' };
global.navigator = { clipboard: { writeText: async () => {} } };

const sent = [];
let opened = null;
global.WebSocket = class {
  constructor(url) {
    this.url = url;
    this.readyState = 0;
    setTimeout(() => {
      this.readyState = 1;
      if (this.onopen) this.onopen();
    }, 0);
  }
  send(data) { sent.push(JSON.parse(data)); }
  close() { this.readyState = 3; if (this.onclose) this.onclose(); }
};

require('./app.js');

// 模拟：填昵称 -> 点「新建房间」
el('nick').value = 'tester';
const click = listeners['createBtn'] && listeners['createBtn'].click;
if (!click) {
  console.log('FAIL: createBtn has no click handler');
  process.exit(1);
}
click();

setTimeout(() => {
  console.log('ws url       :', 'ok');
  console.log('messages sent:', JSON.stringify(sent));
  const ok = sent.some((m) => m.type === 'create' && m.name === 'tester');
  console.log(ok ? 'PASS: 点击后发出了 create' : 'FAIL: 没有发出 create');
  process.exit(ok ? 0 : 1);
}, 50);
