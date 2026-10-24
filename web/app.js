// app.js - 大厅 + 聊天。原生 JS，无框架。
// 房号由服务端随机分配且不对外公开，只有拿到房号的人才能进。
(() => {
  'use strict';

  const $ = (id) => document.getElementById(id);
  const lobby = $('lobby');
  const chat = $('chat');
  const nickInput = $('nick');
  const roomInput = $('roomInput');
  const createBtn = $('createBtn');
  const joinBtn = $('joinBtn');
  const lobbyErr = $('lobbyErr');
  const lobbyStatus = $('lobbyStatus');
  const roomTitle = $('roomTitle');
  const onlineEl = $('online');
  const usersBtn = $('usersBtn');
  const usersPanel = $('usersPanel');
  const leaveBtn = $('leaveBtn');
  const messages = $('messages');
  const input = $('input');
  const sendBtn = $('send');
  const form = $('composer');
  const hint = $('hint');
  const statusDot = $('status');
  const toastEl = $('toast');

  const params = new URLSearchParams(location.search);
  const S = {
    ws: null,
    name: localStorage.getItem('lt.name') || '',
    room: '',
    joined: false,
    users: [],
    retry: 0,
    reconnectTimer: null,
    pingTimer: null,
    pending: null,   // 首次连接成功后要执行的动作
    busy: false,     // 防止连点重复建房
    manualClose: false,
    key: params.get('k') || '',
  };

  // ---------------------------------------------------------------- 工具
  function two(n) { return n < 10 ? '0' + n : '' + n; }
  function clock(ts) {
    const d = new Date((ts || Math.floor(Date.now() / 1000)) * 1000);
    return two(d.getHours()) + ':' + two(d.getMinutes());
  }
  function nearBottom() {
    return messages.scrollHeight - messages.scrollTop - messages.clientHeight < 140;
  }
  function scrollDown(force) {
    if (force || nearBottom()) messages.scrollTop = messages.scrollHeight;
  }
  function setLobbyStatus(text) {
    if (lobbyStatus) lobbyStatus.textContent = text || '';
  }
  let toastTimer = null;
  function toast(text) {
    toastEl.textContent = text;
    toastEl.classList.add('show');
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => toastEl.classList.remove('show'), 2600);
  }

  // ---------------------------------------------------------------- 视图
  function showLobby() {
    chat.hidden = true;
    lobby.hidden = false;
    S.room = '';
    S.joined = false;
    S.busy = false;
    S.users = [];
  }

  function showChat(msg) {
    lobby.hidden = true;
    chat.hidden = false;
    S.room = msg.room;
    S.joined = true;
    S.busy = false;
    S.name = msg.name;
    localStorage.setItem('lt.name', msg.name);
    localStorage.setItem('lt.room', msg.room);
    roomTitle.textContent = '聊天室 ' + msg.room;
    roomTitle.title = '点击复制房号';
    messages.textContent = '';
    (msg.history || []).forEach(addMessage);
    setUsers(msg.users || []);
    setStatus('on', '');
    if (msg.created) {
      addSys('房间已创建，房号 ' + msg.room + '。房号不公开，想邀请谁就把这串数字发给谁');
    } else {
      addSys('已进入 ' + msg.room + '，共 ' + (msg.history || []).length + ' 条历史消息');
    }
    scrollDown(true);
    input.focus();
  }

  function setStatus(kind, text) {
    statusDot.className = 'dot ' + kind;
    hint.textContent = text;
    hint.hidden = !text;
    const on = kind === 'on';
    input.disabled = !on;
    sendBtn.disabled = !on;
  }

  function setUsers(list) {
    S.users = list || [];
    onlineEl.textContent = S.users.length;
    usersPanel.textContent = '';
    S.users.forEach((u) => {
      const s = document.createElement('span');
      s.textContent = u === S.name ? u + '（我）' : u;
      usersPanel.appendChild(s);
    });
  }

  function addSys(text) {
    const el = document.createElement('div');
    el.className = 'sys';
    el.textContent = text;
    messages.appendChild(el);
    scrollDown(false);
  }

  function addMessage(m) {
    const wrap = document.createElement('div');
    const mine = m.from === S.name;
    wrap.className = 'msg ' + (mine ? 'me' : 'them');
    const head = document.createElement('div');
    head.className = 'head';
    head.textContent = (mine ? '我' : m.from) + ' · ' + clock(m.ts);
    const bubble = document.createElement('div');
    bubble.className = 'bubble';
    bubble.textContent = m.text;  // 只用 textContent，杜绝 HTML 注入
    wrap.appendChild(head);
    wrap.appendChild(bubble);
    messages.appendChild(wrap);
    scrollDown(false);
  }

  // ---------------------------------------------------------------- 连接
  function wsUrl() {
    const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
    let url = proto + '//' + location.host + '/ws';
    if (S.key) url += '?k=' + encodeURIComponent(S.key);
    return url;
  }

  // 大厅不发任何请求，点「新建 / 加入」时才建连接（省连接数）
  function ensureConnected(action) {
    if (S.ws && S.ws.readyState === 1) {
      action();
      return;
    }
    S.pending = action;
    connect();
  }

  function connect() {
    clearTimeout(S.reconnectTimer);

    let ws;
    try {
      ws = new WebSocket(wsUrl());
    } catch (e) {
      S.busy = false;
      setLobbyStatus('无法建立连接，正在重试…');
      scheduleReconnect();
      return;
    }
    S.ws = ws;

    // 8 秒还没连上就放弃这一次，给用户明确反馈，而不是一直"没反应"
    const connectTimer = setTimeout(() => {
      if (ws.readyState !== 1) {
        try { ws.close(); } catch (e) { /* ignore */ }
        S.busy = false;
        setLobbyStatus('连接服务器超时，正在重试…');
      }
    }, 8000);

    ws.onopen = () => {
      clearTimeout(connectTimer);
      setLobbyStatus('');
      S.retry = 0;
      clearInterval(S.pingTimer);
      S.pingTimer = setInterval(() => {
        if (ws.readyState === 1) ws.send(JSON.stringify({ type: 'ping' }));
      }, 45000);
      if (S.pending) {
        const fn = S.pending;
        S.pending = null;
        fn();
      } else if (S.room) {
        ws.send(JSON.stringify({ type: 'join', room: S.room, name: S.name }));
      }
    };

    ws.onmessage = (ev) => {
      let msg;
      try { msg = JSON.parse(ev.data); } catch (e) { return; }
      onMessage(msg);
    };

    ws.onclose = () => {
      clearTimeout(connectTimer);
      clearInterval(S.pingTimer);
      S.joined = false;
      S.busy = false;
      setStatus('off', '连接断开，正在重连…');
      if (!S.room) setLobbyStatus('连不上服务器，正在重试…');
      if (!S.manualClose) scheduleReconnect();
      S.manualClose = false;
    };

    ws.onerror = () => {};
  }

  function scheduleReconnect() {
    clearTimeout(S.reconnectTimer);
    const delay = Math.min(1000 * Math.pow(2, S.retry), 15000);
    S.retry += 1;
    S.reconnectTimer = setTimeout(connect, delay);
  }

  function send(obj) {
    if (!S.ws || S.ws.readyState !== 1) {
      toast('还没连上，稍等一下');
      return false;
    }
    S.ws.send(JSON.stringify(obj));
    return true;
  }

  // ---------------------------------------------------------------- 消息
  function onMessage(msg) {
    switch (msg.type) {
      case 'joined':
        S.busy = false;
        setLobbyStatus('');
        showChat(msg);
        break;
      case 'msg':
        if (msg.room === S.room) addMessage(msg);
        break;
      case 'sys':
        if (msg.room === S.room) addSys(msg.text);
        break;
      case 'users':
        if (msg.room === S.room) setUsers(msg.list);
        break;
      case 'left':
        showLobby();
        toast('已离开房间');
        break;
      case 'error':
        S.busy = false;
        lobbyErr.textContent = msg.text || '';
        toast(msg.text || '出错了');
        if (/房间不存在|房间已满|房间数量/.test(msg.text || '')) showLobby();
        break;
      default:
        break;
    }
  }

  // ---------------------------------------------------------------- 动作
  function currentName() {
    const n = (nickInput.value || S.name || '').trim().slice(0, 24);
    if (!n) {
      lobbyErr.textContent = '先填个昵称';
      nickInput.focus();
      return '';
    }
    S.name = n;
    localStorage.setItem('lt.name', n);
    lobbyErr.textContent = '';
    return n;
  }

  function createRoom() {
    if (S.busy) return;
    const n = currentName();
    if (!n) return;
    S.busy = true;
    setLobbyStatus('正在连接服务器…');
    ensureConnected(() => {
      if (!send({ type: 'create', name: n })) {
        S.busy = false;
        setLobbyStatus('连接还没就绪，请再点一次');
      }
    });
  }

  function joinRoom() {
    if (S.busy) return;
    const n = currentName();
    if (!n) return;
    const r = (roomInput.value || '').trim();
    if (!/^\d{6}$/.test(r)) {
      lobbyErr.textContent = '房号是 6 位数字';
      roomInput.focus();
      return;
    }
    S.busy = true;
    lobbyErr.textContent = '';
    setLobbyStatus('正在连接服务器…');
    ensureConnected(() => {
      if (!send({ type: 'join', room: r, name: n })) {
        S.busy = false;
        setLobbyStatus('连接还没就绪，请再点一次');
      }
    });
  }

  createBtn.addEventListener('click', createRoom);
  joinBtn.addEventListener('click', joinRoom);
  roomInput.addEventListener('keydown', (e) => { if (e.key === 'Enter') joinRoom(); });
  nickInput.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') {
      if (/^\d{6}$/.test(roomInput.value.trim())) joinRoom(); else createRoom();
    }
  });

  leaveBtn.addEventListener('click', () => {
    if (S.ws && S.ws.readyState === 1) send({ type: 'leave' });
    else showLobby();
  });

  roomTitle.addEventListener('click', async () => {
    if (!S.room) return;
    try {
      await navigator.clipboard.writeText(S.room);
      toast('房号已复制：' + S.room);
    } catch (e) {
      toast('房号：' + S.room);
    }
  });

  usersBtn.addEventListener('click', () => {
    usersPanel.hidden = !usersPanel.hidden;
  });

  form.addEventListener('submit', (e) => {
    e.preventDefault();
    const text = input.value.trim();
    if (!text) return;
    if (!S.joined) { toast('还没进房间'); return; }
    if (send({ type: 'msg', text })) {
      input.value = '';
      input.focus();
    }
  });

  // ---------------------------------------------------------------- 启动
  if (S.name) nickInput.value = S.name;
  showLobby();
  nickInput.focus();
})();
