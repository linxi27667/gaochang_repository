/* ===== 举升机物联网管理平台 - 前端核心 ===== */

const API_BASE = window.location.origin + '/api';
let token = localStorage.getItem('lift_token') || '';
let currentUser = readStoredUser();
let currentPage = localStorage.getItem('lift_current_page') || 'overview';
let ws = null;
let devices = [];
let unackAlarmCount = 0;
let productMetadata = {};
const commandStateByKey = new Map();
let maintenanceReminderShown = false;
let activeFilterGroup = 'all';
let overviewOnlineFilter = 'all';
let deviceOnlineFilter = 'all';

function readStoredUser() {
  try { return JSON.parse(localStorage.getItem('lift_user') || 'null'); }
  catch { localStorage.removeItem('lift_user'); return null; }
}

// 举升机型号列表
const LIFT_MODELS = ['GC-4.0sle', 'GC-4.0sb', 'GC-4.0MSL', 'GC-4.0PRO-DW'];
const PRODUCT_CAPABILITIES = {
  screw_lift: { refill: false, photoelectric: false, rotary: false, lowerLimit: false },
  double_post: { refill: false, photoelectric: false, rotary: false, lowerLimit: false },
  small_scissor: { refill: true, photoelectric: true, rotary: false, lowerLimit: true },
  thin_scissor: { refill: true, photoelectric: true, rotary: false, lowerLimit: true },
  large_scissor: { refill: true, photoelectric: true, rotary: true, lowerLimit: true }
};
const PRODUCT_TYPE_NAMES = {
  screw_lift: '丝杆举升机',
  double_post: '两柱举升机',
  small_scissor: '小剪举升机',
  thin_scissor: '超薄小剪举升机',
  large_scissor: '大剪举升机'
};

/* ===== API Helper ===== */
async function api(path, options = {}) {
  const headers = { 'Content-Type': 'application/json', ...(options.headers || {}) };
  if (token) headers['Authorization'] = 'Bearer ' + token;
  try {
    const res = await fetch(API_BASE + path, { ...options, headers });
    if (res.status === 401) {
      const error = new Error(t('auth.expired'));
      error.code = 'AUTH_EXPIRED';
      handleLogout();
      throw error;
    }
    const data = await res.json();
    if (!res.ok) throw new Error(data.error || t('common.requestFailed'));
    return data;
  } catch (e) { throw e; }
}

/* ===== Data Sanitizer ===== */
function clampInt(v, min, max, fallback) {
  const n = Number(v);
  if (!Number.isFinite(n)) return fallback;
  return Math.max(min, Math.min(max, Math.round(n)));
}

function sanitizeDevice(d) {
  if (!d || typeof d !== 'object') return null;
  const leftMm = clampInt(d.height_left_mm, 0, 5000, 0);
  const rightMm = clampInt(d.height_right_mm, 0, 5000, 0);
  return {
    ...d,
    device_id: String(d.device_id || ''),
    name: String(d.name || d.device_id || ''),
    model: String(d.model || 'TL-5000'),
    gateway_id: String(d.gateway_id || ''),
    group: String(d.group || ''),
    // 多产品字段
    product_type: String(d.product_type || 'double_post'),
    product_type_name: String(d.product_type_name || '两柱举升机'),
    lift_role: String(d.lift_role || 'main'),
    rotary_switch: ['main', 'sub'].includes(d.rotary_switch) ? d.rotary_switch : 'main',
    online: !!d.online,
    locked: !!d.locked,
    state: String(d.state || 'idle'),
    alarm: String(d.alarm || 'none'),
    direction: ['up', 'down', 'stop'].includes(d.direction) ? d.direction : 'stop',
    height_left_mm: leftMm,
    height_right_mm: rightMm,
    height_diff_mm: clampInt(d.height_diff_mm, -5000, 5000, Math.abs(leftMm - rightMm)),
    run_count: clampInt(d.run_count, 0, 2147483647, 0),
    run_time_s: clampInt(d.run_time_s, 0, 2147483647, 0),
    uptime_s: clampInt(d.uptime_s, 0, 2147483647, 0),
    ts_ms: clampInt(d.ts_ms, 0, Number.MAX_SAFE_INTEGER, 0),
    // 多产品操作计数(工人物理操作)
    up_count: clampInt(d.up_count, 0, 2147483647, 0),
    down_count: clampInt(d.down_count, 0, 2147483647, 0),
    lock_count: clampInt(d.lock_count, 0, 2147483647, 0),
    refill_count: clampInt(d.refill_count, 0, 2147483647, 0),
    estop_count: clampInt(d.estop_count, 0, 2147483647, 0),
    photo_alarm_count: clampInt(d.photo_alarm_count, 0, 2147483647, 0),
    total_run_ms: clampInt(d.total_run_ms, 0, 2147483647, 0),
    last_run_at: d.last_run_at || '',
    upper_limit: d.upper_limit ? 1 : 0,
    lower_limit: d.lower_limit ? 1 : 0,
    stall: d.stall ? 1 : 0,
    collision_up: d.collision_up ? 1 : 0,
    collision_down: d.collision_down ? 1 : 0,
    // 碰撞检测明细(多产品细化)
    left_up_collision: d.left_up_collision ? 1 : 0,
    right_up_collision: d.right_up_collision ? 1 : 0,
    left_down_collision: d.left_down_collision ? 1 : 0,
    right_down_collision: d.right_down_collision ? 1 : 0,
    alarm_code: clampInt(d.alarm_code, 0, 65535, 0),
    has_encoder: d.has_encoder ? 1 : 0,
    has_buzzer: d.has_buzzer ? 1 : 0,
    has_pressure_sensor: d.has_pressure_sensor ? 1 : 0,
    has_display: d.has_display ? 1 : 0,
    buzzer_on: !!d.buzzer_on,
    csq: clampInt(d.csq, -1, 31, -1),
    dtu_state: String(d.dtu_state || ''),
    left_pulse: clampInt(d.left_pulse, 0, 2147483647, 0),
    right_pulse: clampInt(d.right_pulse, 0, 2147483647, 0),
    io_input_json: d.io_input_json || '{}',
    io_output_json: d.io_output_json || '{}',
    total_lift_count: clampInt(d.total_lift_count, 0, 4294967295, 0),
    maintenance_lift_count: clampInt(d.maintenance_lift_count, 0, 4294967295, 0),
    maintenance_threshold: clampInt(d.maintenance_threshold, 1, 4294967295, 5000),
    maintenance_count: clampInt(d.maintenance_count, 0, 4294967295, 0),
    last_maintenance_total: clampInt(d.last_maintenance_total, 0, 4294967295, 0),
    maintenance_due: d.maintenance_due ? 1 : 0,
    usage_epoch: clampInt(d.usage_epoch, 0, 4294967295, 0),
    updated_at: d.updated_at || '',
    received_at_ms: clampInt(d.received_at_ms, 0, Number.MAX_SAFE_INTEGER, Date.now())
  };
}

/* ===== Auth ===== */
async function handleLogin(e) {
  e.preventDefault();
  const btn = document.getElementById('login-btn');
  const errEl = document.getElementById('login-error');
  btn.disabled = true;
  btn.textContent = t('login.submit');
  errEl.style.display = 'none';
  try {
    const res = await fetch(API_BASE + '/auth/login', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        username: document.getElementById('login-username').value.trim(),
        password: document.getElementById('login-password').value
      })
    });
    const data = await res.json();
    if (!res.ok) throw new Error(data.error || t('common.error'));
    token = data.token;
    currentUser = data.user;
    localStorage.setItem('lift_token', token);
    localStorage.setItem('lift_user', JSON.stringify(currentUser));
    await showApp();
  } catch (err) {
    errEl.textContent = err.message;
    errEl.style.display = 'block';
  } finally {
    btn.disabled = false;
    btn.textContent = t('login.submit');
  }
}

function handleLogout() {
  maintenanceReminderShown = false;
  closeMaintenanceReminder();
  token = '';
  currentUser = null;
  localStorage.removeItem('lift_token');
  localStorage.removeItem('lift_user');
  localStorage.removeItem('lift_current_page');
  if (ws) { ws.close(); ws = null; }
  document.getElementById('app').style.display = 'none';
  document.getElementById('login-page').style.display = 'flex';
}

async function showApp() {
  document.getElementById('login-page').style.display = 'none';
  document.getElementById('app').style.display = 'flex';
  try {
    document.getElementById('user-name').textContent = currentUser.real_name || currentUser.username;
    document.getElementById('user-role').textContent = currentUser.role === 'admin' ? '管理员' : '用户';

    const savedAvatar = localStorage.getItem('lift_avatar');
    const avatarEl = document.getElementById('user-avatar');
    if (savedAvatar) {
      applyAvatar(savedAvatar);
    } else {
      avatarEl.textContent = (currentUser.real_name || currentUser.username).charAt(0).toUpperCase();
    }

    await loadProductMetadata();
    connectWS();
    await fetchDevices();
    showMaintenanceReminderOnce();
    fetchUnackAlarms();
    applyLang();
    loadPage(currentPage);
  } catch (e) {
    console.error('[showApp] Error during init:', e);
  }
}

/* ===== WebSocket ===== */
let wsHeartbeat = null;

function connectWS() {
  if (ws) { try { ws.close(); } catch(e) {} }
  if (wsHeartbeat) { clearInterval(wsHeartbeat); wsHeartbeat = null; }
  try {
    const wsProto = location.protocol === 'https:' ? 'wss:' : 'ws:';
    ws = new WebSocket(`${wsProto}//${location.host}/ws?token=${encodeURIComponent(token)}`);
    ws.onopen = () => {
      document.getElementById('mqtt-dot').className = 'indicator-dot connected';
      document.getElementById('mqtt-status-text').textContent = t('common.connected');
    };
    ws.onmessage = (evt) => {
      try {
        const msg = JSON.parse(evt.data);
        if (msg.type === 'device_status') updateDeviceFromWS(msg.device_id, msg.data);
        else if (msg.type === 'command_response') handleCommandResponse(msg.device_id, msg.data);
        else if (msg.type === 'alarm') { fetchUnackAlarms(); if (['overview','devices','alarms'].includes(currentPage)) loadPage(currentPage); }
        else if (msg.type === 'device_op_log') handleDeviceOpLog(msg.device_id, msg.data);
        else if (msg.type === 'device_event') handleDeviceEvent(msg.device_id, msg.data);
      } catch (e) { /* ignore */ }
    };
    ws.onclose = () => {
      document.getElementById('mqtt-dot').className = 'indicator-dot';
      document.getElementById('mqtt-status-text').textContent = t('common.disconnected');
      if (wsHeartbeat) { clearInterval(wsHeartbeat); wsHeartbeat = null; }
      setTimeout(() => { if (token) connectWS(); }, 5000);
    };
    ws.onerror = () => {};
    wsHeartbeat = setInterval(() => { if (ws && ws.readyState === 1) ws.send(JSON.stringify({ type: 'ping' })); }, 30000);
  } catch (e) {
    console.error('[WS] Connection failed:', e);
  }
}

function updateDeviceFromWS(deviceId, data) {
  const receivedAt = Date.now();
  const idx = devices.findIndex(d => d.device_id === deviceId);
  if (idx >= 0) {
    // WebSocket status messages are patches. Merge first so omitted counters
    // are not replaced by sanitizeDevice defaults when a device goes offline.
    const merged = sanitizeDevice({ ...devices[idx], ...data, device_id: deviceId, updated_at: new Date().toISOString(), received_at_ms: receivedAt });
    if (!merged) return;
    devices[idx] = merged;
  } else {
    if (!currentUser || currentUser.role !== 'admin') return;
    const sanitized = sanitizeDevice({ device_id: deviceId, ...data, updated_at: new Date().toISOString(), received_at_ms: receivedAt });
    if (!sanitized) return;
    devices.push(sanitized);
  }
  if (currentPage === 'devices') refreshDeviceCard(deviceId);
  else if (currentPage === 'overview') renderCurrentPage();
}

function handleCommandResponse(deviceId, data) {
  const cmd = data.cmd || '';
  const result = data.result || '';
  const pending = findPendingCommand(deviceId, data.msg_id, cmd);
  if (pending) {
    const status = normalizeCommandResult(cmd, result);
    finishCommandTracking(deviceId, pending.command, status, data.reason || (status === 'succeeded' ? '设备已响应' : result));
    return;
  }
  // v1 协议:result 为 succeeded/rejected/failed/timeout
  // 旧协议:result 为 locked/unlocked/buzzer_on 等具体状态
  if (result === 'timeout') {
    showToast(cmd + ' → ' + t('command.timeout') + ' (' + deviceId + ')', 'error');
  } else if (result === 'succeeded') {
    // v1 协议成功:统一显示成功
    const cmdLabels = { lock: t('devices.lock'), unlock: t('devices.unlock'),
      buzzer_on: t('devices.buzzerOn'), buzzer_off: t('devices.buzzerOff'),
      rename: t('devices.rename'), clear_alarm: '清除报警', get_status: '查询状态' };
    showToast((cmdLabels[cmd] || cmd) + ' ✓ - ' + deviceId, 'success');
  } else if (result === 'rejected' || result === 'failed') {
    showToast(cmd + ' ✗ ' + (data.reason || result) + ' (' + deviceId + ')', 'error');
  }
  // 旧协议兼容:locked/unlocked/buzzer_on 等具体状态
  else if (cmd === 'lock' && result === 'locked') showToast(t('devices.lock') + ' ✓ - ' + deviceId, 'success');
  else if (cmd === 'unlock' && result === 'unlocked') showToast(t('devices.unlock') + ' ✓ - ' + deviceId, 'success');
  else if (cmd === 'buzzer_on' && result === 'buzzer_on') showToast(t('devices.buzzerOn') + ' ✓ - ' + deviceId, 'success');
  else if (cmd === 'buzzer_off' && result === 'buzzer_off') showToast(t('devices.buzzerOff') + ' ✓ - ' + deviceId, 'success');
  else if (cmd === 'rename' && result === 'renamed') {
    showToast(deviceId + ' ' + t('devices.rename') + ' ✓', 'success');
  }
  else showToast(t('devices.query') + ': ' + cmd + ' → ' + result, 'info');
  fetchDevices();
}

// 设备端操作日志(工人物理操作:上升/下降/锁定/补油/急停等)
const OP_TYPE_LABELS = {
  up: '上升', down: '下降', lock: '锁定', unlock: '解锁',
  refill: '补油', estop: '急停', photo_alarm: '光电报警',
  rotary_switch: '旋转开关切换', power_on: '开机', power_off: '关机'
};
function handleDeviceOpLog(deviceId, data) {
  const label = OP_TYPE_LABELS[data.op_type] || data.op_type || '操作';
  // 本地累加计数(避免每次都拉全量数据)
  const d = devices.find(x => x.device_id === deviceId);
  if (d) {
    if (data.op_type === 'up') d.up_count = (d.up_count || 0) + 1;
    else if (data.op_type === 'down') d.down_count = (d.down_count || 0) + 1;
    else if (data.op_type === 'lock') d.lock_count = (d.lock_count || 0) + 1;
    else if (data.op_type === 'refill') d.refill_count = (d.refill_count || 0) + 1;
    else if (data.op_type === 'estop') d.estop_count = (d.estop_count || 0) + 1;
    else if (data.op_type === 'photo_alarm') d.photo_alarm_count = (d.photo_alarm_count || 0) + 1;
  }
  if (currentPage === 'devices') refreshDeviceCard(deviceId);
  else if (currentPage === 'overview') renderCurrentPage();
}

// 设备事件(开机/关机/旋转开关切换等)
function handleDeviceEvent(deviceId, data) {
  const eventType = data.event_type || data.event || 'unknown';
  // 旋转开关切换:本地更新 rotary_switch
  if (eventType === 'rotary_switch' && data.position) {
    const d = devices.find(x => x.device_id === deviceId);
    if (d) d.rotary_switch = data.position;
  }
  if (currentPage === 'devices') refreshDeviceCard(deviceId);
  else if (currentPage === 'overview') renderCurrentPage();
}

/* ===== Data Fetching ===== */
async function fetchDevices() {
  try {
    const receivedAt = Date.now();
    const previous = new Map(devices.map(device => [device.device_id, device]));
    const nextDevices = (await api('/devices'))
      .map(d => sanitizeDevice({ ...d, received_at_ms: receivedAt }))
      .filter(Boolean);
    devices = nextDevices;
    if (['alarms', 'maintenance', 'statistics', 'logs'].includes(currentPage)) {
      if (currentPage === 'statistics') loadStatistics();
      return;
    }
    if (currentPage === 'devices') {
      const renderedIds = Array.from(document.querySelectorAll('.device-card[data-device-id]')).map(card => card.dataset.deviceId);
      const nextIds = nextDevices.map(device => device.device_id);
      const sameDeviceSet = renderedIds.length === nextIds.length && nextIds.every(id => renderedIds.includes(id));
      if (!sameDeviceSet) {
        renderCurrentPage();
        return;
      }
      nextDevices.forEach(device => {
        const old = previous.get(device.device_id);
        const lockChanged = old && old.locked !== device.locked;
        refreshDeviceCard(device.device_id, !!lockChanged && !hasPendingLockControl(device.device_id));
      });
      return;
    }
    renderCurrentPage();
  } catch (e) { /* handled */ }
}

async function fetchUnackAlarms() {
  try {
    const alarms = await api('/alarms/unacknowledged');
    unackAlarmCount = alarms.length;
    const badge = document.getElementById('alarm-badge');
    if (unackAlarmCount > 0) { badge.textContent = unackAlarmCount > 99 ? '99+' : unackAlarmCount; badge.style.display = ''; }
    else badge.style.display = 'none';
  } catch (e) { /* handled */ }
}

/* ===== Toast ===== */
function showToast(message, type = 'info') {
  const container = document.getElementById('toast-container');
  const toast = document.createElement('div');
  toast.className = 'toast toast-' + type;
  toast.textContent = message;
  container.appendChild(toast);
  setTimeout(() => { toast.style.animation = 'slideOut 0.3s ease forwards'; setTimeout(() => toast.remove(), 300); }, 3000);
}

/* ===== Page Navigation ===== */
function loadPage(page) {
  currentPage = page;
  document.querySelectorAll('.menu-item').forEach(i => i.classList.toggle('active', i.dataset.page === page));
  const titles = { overview: t('overview.title'), devices: t('devices.title'), alarms: t('alarms.title'), maintenance: t('maintenance.title'), statistics: t('statistics.title'), logs: t('logs.title') };
  const breadcrumbs = { overview: t('nav.overview'), devices: t('nav.devices'), alarms: t('nav.alarms'), maintenance: t('nav.maintenance'), statistics: t('nav.statistics'), logs: t('nav.logs') };
  document.getElementById('page-title').textContent = titles[page] || '';
  document.getElementById('breadcrumb').textContent = `${t('common.home')} / ${breadcrumbs[page] || ''}`;
  renderCurrentPage();
}

function renderCurrentPage() {
  const content = document.getElementById('page-content');
  switch (currentPage) {
    case 'overview': content.innerHTML = renderOverview(); break;
    case 'devices': content.innerHTML = renderDevices(); break;
    case 'alarms': content.innerHTML = renderAlarms(); break;
    case 'maintenance': content.innerHTML = renderMaintenance(); break;
    case 'statistics': content.innerHTML = renderStatistics(); break;
    case 'logs': content.innerHTML = renderLogs(); break;
  }
}

/* ===== Status Helpers ===== */
function getStatusText(s) {
  const map = { normal: t('status.normal'), offline: t('status.offline'), fault: t('status.fault'), locked: t('status.locked'), maintenance: t('status.maintenance'), idle: t('status.idle'), up: t('status.up'), down: t('status.down'), rising: '上升中', dropping: '下降中', refilling: '补油中', estop: '急停', photo_alarm: '光电报警' };
  return map[s] || s;
}

async function restoreSession() {
  if (!token || !currentUser) {
    document.getElementById('login-page').style.display = 'flex';
    return;
  }
  try {
    const response = await fetch(API_BASE + '/auth/me', {
      headers: { 'Authorization': 'Bearer ' + token }
    });
    if (response.status === 401) {
      handleLogout();
      return;
    }
    if (response.ok) {
      currentUser = await response.json();
      localStorage.setItem('lift_user', JSON.stringify(currentUser));
    } else {
      console.warn('[auth] session validation unavailable:', response.status);
    }
  } catch (error) {
    // 短暂网络异常不清除已保存的登录态，恢复后仍可自动重连。
    console.warn('[auth] session validation failed:', error.message);
  }
  showApp();
}

async function loadProductMetadata() {
  try {
    const configs = await api('/devices/meta/products');
    productMetadata = Object.fromEntries((configs || []).map(config => [config.product_type, config]));
  } catch (error) {
    // 内置能力用于旧后端或临时网络异常，不影响当前登录态。
    console.warn('[products] metadata unavailable:', error.message);
  }
}
const HIDDEN_COLLISION_ALARMS = new Set(['collision', 'collision_up', 'collision_down']);
function isAlarmHidden(alarm, productType) {
  return HIDDEN_COLLISION_ALARMS.has(alarm) || alarm === 'estop' || (alarm === 'stall' && productType !== 'small_scissor');
}
function hasVisibleAlarm(d) { return !!(d.alarm && d.alarm !== 'none' && !isAlarmHidden(d.alarm, d.product_type)); }
function getStatusClass(d) {
  if (!d.online) return 'offline';
  if (d.locked) return 'locked';
  if (hasVisibleAlarm(d)) return 'fault';
  if (isMaintenanceDue(d) || d.state === 'maintenance_due') return 'maintenance';
  return 'normal';
}
function isMaintenanceDue(d) { return window.MaintenanceReminder.isDue(d); }
function getDueMaintenanceDevices(list = devices) { return window.MaintenanceReminder.getDueMaintenanceDevices(list); }
function getCycleCount(d) { return window.MaintenanceReminder.getCycleCount(d); }
function maintenanceStatusLine(d) {
  const due = isMaintenanceDue(d);
  const cycle = getCycleCount(d);
  return `<div class="maintenance-status-line ${due ? 'due' : ''}">保养周期：${cycle}/${d.maintenance_threshold || 5000}，${due ? '已到期' : '未到期'}，累计 ${d.total_lift_count || 0}，已保养 ${d.maintenance_count || 0} 次</div>`;
}
function closeMaintenanceReminder() {
  const popup = document.getElementById('maintenance-reminder');
  if (popup) popup.style.display = 'none';
}
function showMaintenanceReminderOnce() {
  if (maintenanceReminderShown) return;
  maintenanceReminderShown = true;
  const dueDevices = getDueMaintenanceDevices();
  const popup = document.getElementById('maintenance-reminder');
  if (!popup || dueDevices.length === 0) return;
  popup.innerHTML = `<div class="maintenance-reminder-head"><strong>待保养设备</strong><button class="close-btn" onclick="closeMaintenanceReminder()" title="关闭">&times;</button></div>
    <div class="maintenance-reminder-list">${dueDevices.map(d => {
      const cycle = getCycleCount(d);
      const over = Math.max(0, cycle - (d.maintenance_threshold || 5000));
      return `<div class="maintenance-reminder-item"><div><b>${d.name || d.device_id}</b><small>${d.device_id} · ${d.model || '-'}</small><small>周期 ${cycle}/${d.maintenance_threshold || 5000} · 超出 ${over}</small></div><button class="btn btn-sm btn-outline" onclick="closeMaintenanceReminder();showDeviceDetail('${d.device_id}')">进入设备</button></div>`;
    }).join('')}</div>`;
  popup.style.display = 'block';
}
function getStateText(s) { const map = { idle: t('state.idle'), up: t('state.up'), down: t('state.down'), stop: t('state.stop'), rising: '上升中', dropping: '下降中', locked: t('status.locked'), refilling: '补油中', estop: '急停', photo_alarm: '光电报警', maintenance_due: t('status.maintenance') }; return map[s] || s; }
function getAlarmText(a, productType) {
  if (isAlarmHidden(a, productType)) return t('alarm.none');
  const map = { none: t('alarm.none'), fault: '设备故障', stall: t('alarm.stall'), balance_timeout: t('alarm.balance_timeout'), safety_bar: t('alarm.safety_bar'), overheight: t('alarm.overheight'), Emergency: t('alarm.Emergency'), estop: '急停触发', photo_alarm: '光电报警' };
  return map[a] || a;
}

const IO_INPUT_LABELS = {
  up_button: '上升按钮', btn_up: '上升按钮',
  down_button: '下降按钮', btn_down: '下降按钮',
  lock_button: '锁定按钮', btn_lock: '锁定按钮',
  refill_button: '补油按钮', btn_refill: '补油按钮',
  refill: '补油按钮',
  estop: '急停',
  rotary_switch: '当前控制', rotary: '当前控制',
  upper_limit: '上限位', limit_up: '上限位',
  lower_limit: '下限位', limit_down: '下限位',
  sub_upper_limit: '子机上限位',
  photoelectric: '光电开关'
};
const IO_OUTPUT_LABELS = {
  motor: '电机',
  electromagnet: '电磁铁', solenoid: '电磁铁',
  drop_valve: '下降阀', valve_drop: '下降阀',
  main_air_valve: '主机气阀', valve_air_main: '主机气阀',
  sub_air_valve: '子机气阀', valve_air_sub: '子机气阀',
  main_work_valve: '主机工作阀', valve_work_main: '主机工作阀',
  sub_work_valve: '子机工作阀', valve_work_sub: '子机工作阀',
  main_air: '主机气阀', sub_air: '子机气阀',
  main_work: '主机工作阀', sub_work: '子机工作阀',
  drop_valve: '下降阀', air_valve: '气阀',
  valve_air: '气阀'
};

function parseJsonObject(value) {
  if (!value) return {};
  if (typeof value === 'object' && !Array.isArray(value)) return value;
  if (typeof value !== 'string') return {};
  try {
    const parsed = JSON.parse(value);
    return parsed && typeof parsed === 'object' && !Array.isArray(parsed) ? parsed : {};
  } catch {
    return {};
  }
}

function isTruthyIo(v) {
  return v === true || v === 1 || v === '1' || v === 'on' || v === 'ON';
}

function hasHeightFeedback(d) {
  return !!d.has_encoder;
}

function getProductCaps(d) {
  const meta = productMetadata[d.product_type];
  if (meta) {
    return {
      refill: !!meta.has_refill,
      photoelectric: !!meta.has_photoelectric,
      rotary: !!meta.has_rotary,
      lowerLimit: !!meta.has_limit_down
    };
  }
  return PRODUCT_CAPABILITIES[d.product_type] || PRODUCT_CAPABILITIES.double_post;
}

function getProductName(productType, fallback) {
  return PRODUCT_TYPE_NAMES[productType] || fallback || productType || '举升机';
}

function renderAccessoryTags(d) {
  const tags = [
    d.has_encoder ? '<span class="accessory-tag" title="高度编码器">高度</span>' : '',
    d.has_buzzer ? '<span class="accessory-tag" title="蜂鸣器">蜂鸣</span>' : '',
    d.has_pressure_sensor ? '<span class="accessory-tag" title="压力传感器">压力</span>' : '',
    d.has_display ? '<span class="accessory-tag" title="显示屏">屏幕</span>' : ''
  ].filter(Boolean);
  if (!tags.length) {
    tags.push('<span class="accessory-tag muted" title="无高度编码器，页面展示运行状态">状态反馈</span>');
  }
  return tags.join('');
}

function getMotionText(d) {
  if (!d.online) return '离线';
  if (hasVisibleAlarm(d)) return getAlarmText(d.alarm, d.product_type);
  if (d.locked) return '远程锁定';
  return getStateText(d.state || 'idle');
}

function renderIoChips(jsonValue, labels, emptyText) {
  const obj = parseJsonObject(jsonValue);
  const active = Object.entries(obj)
    .filter(([, v]) => isTruthyIo(v))
    .slice(0, 8)
    .map(([k]) => `<span class="io-chip active">${labels[k] || k}</span>`);
  return active.length ? active.join('') : `<span class="io-chip muted">${emptyText}</span>`;
}

const BUTTON_INPUTS = new Set(['up_button', 'btn_up', 'down_button', 'btn_down', 'lock_button', 'btn_lock', 'refill_button', 'btn_refill', 'refill', 'estop']);
const SENSOR_INPUTS = new Set(['upper_limit', 'limit_up', 'lower_limit', 'limit_down', 'sub_upper_limit', 'photoelectric']);

function ioStateText(key, active, kind = 'input') {
  if (kind === 'output') return active ? '开启' : '关闭';
  if (BUTTON_INPUTS.has(key)) return active ? '已按下' : '未按下';
  if (SENSOR_INPUTS.has(key)) return active ? '已触发' : '未触发';
  if (key === 'rotary' || key === 'rotary_switch') return active ? '子机' : '主机';
  return active ? '开启' : '关闭';
}

function declaredKeys(d, kind) {
  const meta = productMetadata[d.product_type];
  const configured = parseJsonList(meta && meta[kind === 'input' ? 'inputs_json' : 'outputs_json']);
  if (configured.length) return configured;
  return Object.keys(parseJsonObject(d[kind === 'input' ? 'io_input_json' : 'io_output_json']));
}

const IO_ALIASES = {
  limit_up: ['upper_limit'], limit_down: ['lower_limit'], btn_refill: ['refill'],
  valve_drop: ['drop_valve'], valve_air: ['air_valve'],
  valve_air_main: ['main_air'], valve_air_sub: ['sub_air'],
  valve_work_main: ['main_work'], valve_work_sub: ['sub_work']
};

function getIoValue(values, key) {
  if (values[key] != null) return values[key];
  for (const alias of IO_ALIASES[key] || []) if (values[alias] != null) return values[alias];
  return undefined;
}

function renderStateItems(d, kind, compact = false) {
  const values = parseJsonObject(d[kind === 'input' ? 'io_input_json' : 'io_output_json']);
  return declaredKeys(d, kind).map(key => {
    let value = getIoValue(values, key);
    if (key === 'rotary' && value == null) value = d.rotary_switch === 'sub';
    const active = isTruthyIo(value);
    return `<div class="io-state-item ${active ? 'is-active' : ''} ${compact ? 'compact' : ''}">
      <span class="io-state-dot" aria-hidden="true"></span>
      <span class="io-state-name">${(kind === 'input' ? IO_INPUT_LABELS : IO_OUTPUT_LABELS)[key] || key}</span>
      <strong>${ioStateText(key, active, kind)}</strong>
    </div>`;
  }).join('');
}

function renderHeightPanel(d, compact = false) {
  const maxH = 2000;
  const leftPct = Math.min((d.height_left_mm || 0) / maxH * 100, 100);
  const rightPct = Math.min((d.height_right_mm || 0) / maxH * 100, 100);
  return `
    <div class="${compact ? 'capability-panel compact' : 'capability-panel'}">
      <div class="capability-title">高度反馈</div>
      <div class="height-values">
        <div><span>${t('devices.heightLeft')}</span><b>${d.height_left_mm || 0}<small>mm</small></b></div>
        <div><span>${t('devices.heightRight')}</span><b>${d.height_right_mm || 0}<small>mm</small></b></div>
        <div><span>${t('devices.heightDiff')}</span><b>${d.height_diff_mm || 0}<small>mm</small></b></div>
      </div>
      <div class="height-track"><span>${t('devices.heightLeft')}</span><div class="height-bar"><div class="height-bar-fill" style="width:${leftPct}%;background:var(--primary);"></div></div></div>
      <div class="height-track"><span>${t('devices.heightRight')}</span><div class="height-bar"><div class="height-bar-fill" style="width:${rightPct}%;background:var(--success);"></div></div></div>
    </div>`;
}

function renderMotionPanel(d, compact = false) {
  return `
    <div class="${compact ? 'capability-panel compact motion-only' : 'capability-panel motion-only'}">
      <div class="capability-title">运行状态</div>
      <div class="motion-status">
        <span class="motion-icon">${getDirectionIcon(d)}</span>
        <div><b>${getMotionText(d)}</b><small>${getDirectionText(d)} · ${getProductName(d.product_type, d.product_type_name)}</small></div>
      </div>
      <div class="io-chip-row">${renderIoChips(d.io_input_json, IO_INPUT_LABELS, '无输入动作')}</div>
      <div class="io-chip-row">${renderIoChips(d.io_output_json, IO_OUTPUT_LABELS, '输出关闭')}</div>
    </div>`;
}

function renderCapabilityPanel(d, compact = false) {
  return hasHeightFeedback(d) ? renderHeightPanel(d, compact) : renderMotionPanel(d, compact);
}

function parseJsonList(value) {
  if (Array.isArray(value)) return value;
  try { const parsed = JSON.parse(value || '[]'); return Array.isArray(parsed) ? parsed : []; }
  catch { return []; }
}

function renderDeclaredIo(d) {
  const inputs = renderStateItems(d, 'input');
  const outputs = renderStateItems(d, 'output');
  return `<div class="declared-io-panel">
    <div class="io-declared-group"><div class="detail-section-title">产品输入</div><div class="io-state-grid">${inputs}</div></div>
    <div class="io-declared-group"><div class="detail-section-title">产品输出</div><div class="io-state-grid">${outputs}</div></div>
  </div>`;
}

function renderSafetyState(label, active, action = '', available = true) {
  if (!available) {
    return `<div class="safety-state is-unavailable"><span class="safety-state-icon">-</span><span>${label}</span><strong>未知</strong></div>`;
  }
  return `<div class="safety-state ${active ? 'is-alert' : 'is-normal'}"><span class="safety-state-icon">${active ? '!' : '✓'}</span><span>${label}</span><strong>${active ? '已触发' : '正常'}</strong>${action ? `<div class="safety-state-action">${action}</div>` : ''}</div>`;
}

function isPhotoelectricTriggered(d) {
  if (!getProductCaps(d).photoelectric) return false;
  const inputs = parseJsonObject(d.io_input_json);
  return d.state === 'photo_alarm' || d.alarm === 'photo_alarm' || isTruthyIo(getIoValue(inputs, 'photoelectric'));
}

function renderPhotoAlarmAction(d, compact) {
  const alarmLatched = d.state === 'photo_alarm' || d.alarm === 'photo_alarm';
  if (!alarmLatched) return '';
  const pending = getPendingCommand(d.device_id, 'clear_alarm');
  const disabled = !d.online || !!pending;
  const label = pending ? '解除中...' : '解除光电报警';
  return `<button class="btn btn-sm btn-danger" ${disabled ? 'disabled' : ''} onclick="event.stopPropagation();clearPhotoAlarm('${d.device_id}')">${label}</button>`;
}

function renderSafetyStates(d, compact = false) {
  const caps = getProductCaps(d);
  const rows = [
    renderSafetyState(t('safety.upperLimit'), d.upper_limit, '', d.online),
    caps.lowerLimit ? renderSafetyState(t('safety.lowerLimit'), d.lower_limit, '', d.online) : '',
    caps.photoelectric ? renderSafetyState('光电传感器', isPhotoelectricTriggered(d), d.online ? renderPhotoAlarmAction(d, compact) : '', d.online) : ''
  ].filter(Boolean);
  return `<div class="safety-state-list ${compact ? 'compact' : ''}">${rows.join('')}</div>`;
}

/* ===== Safety / Link Helpers (完整透传字段渲染) ===== */
function getDirectionIcon(d) {
  const dir = d.direction || 'stop';
  if (dir === 'up') return '↑';
  if (dir === 'down') return '↓';
  return '■';
}
function getDirectionText(d) {
  const dir = d.direction || 'stop';
  if (dir === 'up') return t('state.up');
  if (dir === 'down') return t('state.down');
  return t('state.stop');
}

// 卡片角标：仅非正常态显示，正常态返回空字符串保持卡片简洁
function renderSafetyBadges(d) {
  if (!d.online) return '';
  const caps = getProductCaps(d);
  const badges = [];
  const push = (cond, label, cls) => { if (cond) badges.push(`<span class="safety-badge ${cls}" title="${label}">${label}</span>`); };
  push(d.upper_limit, t('safety.upperLimit'), 'badge-warn');
  push(caps.lowerLimit && d.lower_limit, t('safety.lowerLimit'), 'badge-warn');
  push(isPhotoelectricTriggered(d), '光电传感器', 'badge-danger');
  const dir = d.direction || 'stop';
  if (dir === 'up' || dir === 'down') badges.push(`<span class="safety-badge badge-dir" title="${getDirectionText(d)}">${getDirectionIcon(d)} ${getDirectionText(d)}</span>`);
  return badges.length ? `<div class="safety-badges">${badges.join('')}</div>` : '';
}
// 信号强度等级：CSQ 参考 LTE 模块惯例
function getCsqLevel(csq) {
  const v = Number(csq);
  if (v < 0 || isNaN(v)) return { text: '暂无采样', cls: 'signal-unknown', pct: 0 };
  if (v >= 20) return { text: t('link.signalGood'), cls: 'signal-good', pct: 100 };
  if (v >= 10) return { text: t('link.signalMedium'), cls: 'signal-medium', pct: 60 };
  return { text: t('link.signalWeak'), cls: 'signal-weak', pct: 25 };
}
function getYesNo(v) { return v ? t('common.yes') : t('common.no'); }
function formatTime(s) {
  if (!s) return `0${t('unit.minute')}`;
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  return h > 0 ? `${h}${t('unit.hour')}${m}${t('unit.minute')}` : `${m}${t('unit.minute')}`;
}
function getLiveUptimeSeconds(d) {
  const base = Number(d.uptime_s || 0);
  if (!d.online || !d.received_at_ms) return base;
  const elapsed = Math.max(0, Math.floor((Date.now() - d.received_at_ms) / 1000));
  return base + elapsed;
}
function formatLiveTime(s) {
  const total = Math.max(0, Math.floor(Number(s) || 0));
  const h = Math.floor(total / 3600);
  const m = Math.floor((total % 3600) / 60);
  const sec = total % 60;
  if (h > 0) return `${h}${t('unit.hour')}${m}${t('unit.minute')}${sec}${t('unit.second')}`;
  if (m > 0) return `${m}${t('unit.minute')}${sec}${t('unit.second')}`;
  return `${sec}${t('unit.second')}`;
}
function refreshLiveUptimeDisplays() {
  document.querySelectorAll('[data-uptime-device]').forEach(el => {
    const d = devices.find(x => x.device_id === el.dataset.uptimeDevice);
    if (d) el.textContent = formatLiveTime(getLiveUptimeSeconds(d));
  });
}
function formatTs(iso) { if (!iso) return '-'; try { return new Date(iso).toLocaleString(currentLang === 'zh' ? 'zh-CN' : currentLang); } catch { return iso; } }

/* ===== Overview Page ===== */
function renderOverview() {
  const validDevices = devices.filter(d => d && d.device_id);
  if (validDevices.length === 0) return `<div class="empty-state"><div class="empty-icon">📡</div><h3>${t('overview.waiting')}</h3><p>${t('overview.waitingSub')}</p></div>`;
  const online = devices.filter(d => d.online && !d.locked && !hasVisibleAlarm(d)).length;
  const offline = devices.filter(d => !d.online).length;
  const fault = devices.filter(d => d.online && hasVisibleAlarm(d)).length;
  const locked = devices.filter(d => d.locked).length;
  const filteredDevices = validDevices.filter(d => matchesOnlineFilter(d, overviewOnlineFilter));

  return `
    <div class="cards-grid">
      <div class="card online"><div class="card-title">${t('overview.online')}</div><div class="card-value">${online}</div><div class="card-subtitle">/${devices.length}${t('unit.device')}</div></div>
      <div class="card offline"><div class="card-title">${t('overview.offline')}</div><div class="card-value">${offline}</div></div>
      <div class="card fault"><div class="card-title">${t('overview.fault')}</div><div class="card-value">${fault}</div></div>
      <div class="card locked"><div class="card-title">${t('overview.locked')}</div><div class="card-value">${locked}</div></div>
    </div>
    ${renderOnlineFilterTabs('overview', overviewOnlineFilter)}
    ${filteredDevices.length
      ? `<div class="device-cards">${filteredDevices.map(d => renderDeviceCard(d)).join('')}</div>`
      : `<div class="empty-state compact"><h3>没有符合当前状态的设备</h3><p>请选择其他在线状态查看设备。</p></div>`}`;
}

function renderDeviceCard(d) {
  const sc = getStatusClass(d);
  return `
    <div class="device-card${d.online ? '' : ' is-offline'}" onclick="showDeviceDetail('${d.device_id}')">
      <div class="device-card-header">
        <div><div class="device-card-name">${d.name || d.device_id}</div><div class="device-card-id">${d.device_id}${d.group ? ' · ' + d.group : ''}${d.bound_at ? ' · 绑定 ' + formatTs(d.bound_at) : ''}</div></div>
        <span class="status-tag status-${sc}">${getStatusText(sc)}</span>
      </div>
      ${renderDeviceSummary(d)}
    </div>`;
}

function renderDeviceSummary(d) {
  const alarmActive = hasVisibleAlarm(d);
  const caps = getProductCaps(d);
  const keyInputs = declaredKeys(d, 'input').filter(key => BUTTON_INPUTS.has(key));
  const inputValues = parseJsonObject(d.io_input_json);
  return `
    <div class="device-product-line"><span>${getProductName(d.product_type, d.product_type_name)}</span>${caps.rotary ? `<b>旋转开关${d.online ? '当前控制' : '最后状态'}：${d.rotary_switch === 'sub' ? '子机' : '主机'}</b>` : ''}</div>
    <div class="device-runtime-strip">
      <div><span>运行状态</span><strong>${getMotionText(d)}</strong></div>
      <div><span>运动方向</span><strong>${d.online ? `${getDirectionIcon(d)} ${getDirectionText(d)}` : '未知'}</strong></div>
      <div><span>报警</span><strong class="${d.online && alarmActive ? 'text-danger' : ''}">${d.online ? getAlarmText(d.alarm, d.product_type) : '未知'}</strong></div>
    </div>
    <div class="device-summary-section"><div class="device-summary-title">关键输入</div><div class="compact-inputs">${keyInputs.map(key => {
      const active = isTruthyIo(getIoValue(inputValues, key));
      return `<span class="compact-input ${!d.online ? 'is-unavailable' : (active ? 'is-active' : '')}">${IO_INPUT_LABELS[key] || key}<b>${d.online ? ioStateText(key, active) : '未知'}</b></span>`;
    }).join('')}</div></div>
    <div class="device-summary-section"><div class="device-summary-title">安全状态</div>${renderSafetyStates(d, true)}</div>
    ${hasHeightFeedback(d) ? renderHeightPanel(d, true) : ''}
    <div class="maintenance-progress ${isMaintenanceDue(d) ? 'is-due' : ''}"><div><span>保养周期</span><strong>${getCycleCount(d)} / ${d.maintenance_threshold || 5000}</strong></div><div class="maintenance-progress-track"><i style="width:${Math.min(100, (getCycleCount(d) / (d.maintenance_threshold || 5000)) * 100)}%"></i></div></div>`;
}

/* ===== Devices Page ===== */
function renderDevices() {
  const validDevices = devices.filter(d => d && d.device_id);
  const isAdmin = currentUser && currentUser.role === 'admin';
  // 后端已按绑定关系过滤普通用户设备列表；这里允许已绑定用户控制自己的设备。
  const canControlBoundDevices = !!currentUser;
  const groups = [...new Set(devices.map(d => d.group || t('devices.defaultGroup')))];

  // admin 走多字段"添加设备"(可勾选配件);user 走 SN 绑定
  const addBtnHtml = isAdmin
    ? `<button class="btn btn-primary" onclick="showAddDeviceModal()">+ ${t('devices.addDevice')}</button>`
    : `<button class="btn btn-primary" onclick="showBindDeviceModal()">+ ${t('devices.addDevice')}</button>`;
  const filteredDevices = devices.filter(d => {
    const matchesGroup = activeFilterGroup === 'all' || (d.group || t('devices.defaultGroup')) === activeFilterGroup;
    return matchesGroup && matchesOnlineFilter(d, deviceOnlineFilter);
  });
  const filteredCardsHtml = filteredDevices.length === 0
    ? `<div class="empty-state"><div class="empty-icon">📡</div><h3>${t('devices.noDevices')}</h3><p>${t('devices.noDevicesSub')}</p></div>`
    : `<div class="device-cards" id="device-cards-container">${filteredDevices.map(d => renderDeviceCardWithActions(d, canControlBoundDevices)).join('')}</div>`;
  return `
    <div class="filter-bar">
      <div class="filter-set">
        <span class="filter-label">分组</span>
        <div class="tab-group">
          ${['all', ...groups].map(g => `<button class="tab-btn${activeFilterGroup === g ? ' active' : ''}" onclick="filterDeviceView('${g}')">${g === 'all' ? t('common.all') + '(' + devices.length + ')' : g + '(' + devices.filter(d => (d.group || t('devices.defaultGroup')) === g).length + ')'}</button>`).join('')}
        </div>
      </div>
      ${renderOnlineFilterTabs('devices', deviceOnlineFilter)}
      ${addBtnHtml}
    </div>
    ${filteredCardsHtml}
  `;
}

function renderDeviceCardWithActions(d, canControl) {
  const isAdmin = currentUser && currentUser.role === 'admin';
  const sc = getStatusClass(d);
  return `
    <div class="device-card${d.online ? '' : ' is-offline'}" data-device-id="${d.device_id}">
      <div class="device-card-header">
        <div><div class="device-card-name" style="cursor:pointer" onclick="showDeviceDetail('${d.device_id}')">${d.name || d.device_id}</div><div class="device-card-id">${d.device_id}${d.group ? ' · ' + d.group : ''}</div></div>
        <span class="status-tag status-${sc}">${getStatusText(sc)}</span>
      </div>
      ${renderDeviceSummary(d)}
      <div class="device-card-actions">
        <button class="btn btn-sm btn-outline" onclick="showDeviceDetail('${d.device_id}')">${t('devices.detail')}</button>
        ${canControl ? renderLockControlButton(d) : ''}
        ${canControl ? renderMaintenanceQueryButton(d.device_id, true) : ''}
        ${canControl ? `<button class="btn btn-sm btn-outline" onclick="renameDevice('${d.device_id}', '${(d.name||'').replace(/'/g,"\\'")}')">改名</button>` : ''}
        ${isAdmin ? `<button class="btn btn-sm btn-danger" onclick="deleteDevice('${d.device_id}', '${(d.name||'').replace(/'/g,"\\'")}')">${t('common.delete')}</button>` : ''}
      </div>
    </div>`;
}

function matchesOnlineFilter(device, filter) {
  if (filter === 'online') return !!device.online;
  if (filter === 'offline') return !device.online;
  return true;
}

function renderOnlineFilterTabs(scope, activeFilter) {
  const onlineCount = devices.filter(d => d.online).length;
  const offlineCount = devices.filter(d => !d.online).length;
  const labels = [
    ['all', `全部(${devices.length})`],
    ['online', `在线(${onlineCount})`],
    ['offline', `离线(${offlineCount})`]
  ];
  return `<div class="filter-set online-filter-set">
    <span class="filter-label">在线状态</span>
    <div class="tab-group status-tab-group">
      ${labels.map(([value, label]) => `<button class="tab-btn${activeFilter === value ? ' active' : ''}" onclick="setOnlineFilter('${scope}', '${value}')">${label}</button>`).join('')}
    </div>
  </div>`;
}

function setOnlineFilter(scope, filter) {
  if (scope === 'overview') overviewOnlineFilter = filter;
  else deviceOnlineFilter = filter;
  renderCurrentPage();
}

function filterDeviceView(group) {
  activeFilterGroup = group;
  renderCurrentPage();
}

function renderLockControlButton(d) {
  const pending = getPendingCommand(d.device_id, 'lock') || getPendingCommand(d.device_id, 'unlock');
  if (pending) {
    const label = pending.command === 'unlock' ? '解锁中...' : '锁机中...';
    return `<button class="btn btn-sm btn-outline" disabled aria-busy="true">${label}</button>`;
  }
  return d.locked
    ? `<button class="btn btn-sm btn-success" onclick="unlockDevice('${d.device_id}')">${t('devices.unlock')}</button>`
    : `<button class="btn btn-sm btn-danger" onclick="lockDevice('${d.device_id}')">${t('devices.lock')}</button>`;
}

function refreshDeviceCard(deviceId, syncActions = false) {
  if (currentPage !== 'devices') return;
  const device = devices.find(item => item.device_id === deviceId);
  const card = Array.from(document.querySelectorAll('.device-card'))
    .find(item => item.dataset.deviceId === deviceId);
  if (!device) return;
  const matchesGroup = activeFilterGroup === 'all' || (device.group || t('devices.defaultGroup')) === activeFilterGroup;
  if (!matchesGroup || !matchesOnlineFilter(device, deviceOnlineFilter) || !card) {
    renderCurrentPage();
    return;
  }

  const template = document.createElement('template');
  template.innerHTML = renderDeviceCardWithActions(device, !!currentUser).trim();
  const fresh = template.content.firstElementChild;
  const actions = card.querySelector(':scope > .device-card-actions');
  const freshActions = fresh.querySelector(':scope > .device-card-actions');

  Array.from(card.children).forEach(child => {
    if (child !== actions) child.remove();
  });
  Array.from(fresh.children).forEach(child => {
    if (child !== freshActions) card.insertBefore(child, actions);
  });
  if (syncActions && actions && freshActions) actions.replaceWith(freshActions);
}

function refreshDeviceCommandControls(deviceId) {
  if (currentPage === 'devices') refreshDeviceCard(deviceId, true);
  else if (currentPage === 'overview') renderCurrentPage();
}
function formatDtuState(state) {
  const labels = {
    transparent: 'MQTT 透传已连接',
    configured: 'DTU 已配置，等待连接',
    cmd_mode: 'DTU 命令模式',
    uart_ready: 'DTU 串口就绪',
    error: 'DTU 通信异常',
    off: 'DTU 未启动'
  };
  return labels[String(state || '').toLowerCase()] || (state || '暂无状态');
}

/* ===== Device Detail Modal ===== */
let deviceTrendChart = null;
let deviceTypeChart = null;

async function loadDeviceDetailCharts(deviceId, days = 30) {
  const panel = document.getElementById('device-detail-charts');
  if (!panel) return;
  const params = new URLSearchParams({ device_serial: deviceId, group_by: 'day' });
  if (Number(days) > 0) {
    const start = new Date(Date.now() - (Number(days) - 1) * 86400000);
    params.set('start_date', start.toISOString().slice(0, 10));
  }
  try {
    const stats = await api('/device-ops/stats?' + params.toString());
    const hasData = stats.by_type && stats.by_type.some(item => item.count > 0);
    panel.querySelector('.device-chart-empty').style.display = hasData ? 'none' : 'block';
    panel.querySelector('.device-chart-grid').style.display = hasData ? 'grid' : 'none';
    if (!hasData || typeof Chart === 'undefined') return;

    const daysList = [...new Set(stats.by_day.map(item => item.day))].sort();
    const series = (role, prefix) => daysList.map(day => stats.by_day
      .filter(item => item.day === day && item.role === role && item.op_type === `${prefix}_start`)
      .reduce((sum, item) => sum + Number(item.count || 0), 0));
    if (deviceTrendChart) deviceTrendChart.destroy();
    deviceTrendChart = new Chart(document.getElementById('device-trend-chart'), {
      type: 'bar',
      data: { labels: daysList, datasets: [
        { label: '主机上升', data: series('main', 'up'), backgroundColor: '#16a34a' },
        { label: '主机下降', data: series('main', 'down'), backgroundColor: '#22c55e' },
        { label: '子机上升', data: series('sub', 'up'), backgroundColor: '#2563eb' },
        { label: '子机下降', data: series('sub', 'down'), backgroundColor: '#60a5fa' }
      ]},
      options: { responsive: true, maintainAspectRatio: false, plugins: { legend: { position: 'bottom' } }, scales: { y: { beginAtZero: true, ticks: { precision: 0 } } } }
    });

    const groups = [
      ['上升', ['up', 'up_start']], ['下降', ['down', 'down_start']], ['急停', ['estop']],
      ['光电', ['photo_alarm']], ['上限位', ['upper_limit']], ['下限位', ['lower_limit']]
    ];
    const counts = groups.map(([, types]) => stats.by_type.filter(item => types.includes(item.op_type)).reduce((sum, item) => sum + Number(item.count || 0), 0));
    if (deviceTypeChart) deviceTypeChart.destroy();
    deviceTypeChart = new Chart(document.getElementById('device-type-chart'), {
      type: 'doughnut',
      data: { labels: groups.map(item => item[0]), datasets: [{ data: counts, backgroundColor: ['#16a34a','#22c55e','#b91c1c','#ef4444','#eab308','#f59e0b'] }] },
      options: { responsive: true, maintainAspectRatio: false, plugins: { legend: { position: 'bottom' } } }
    });
  } catch (error) {
    panel.querySelector('.device-chart-empty').textContent = '统计数据加载失败：' + error.message;
    panel.querySelector('.device-chart-empty').style.display = 'block';
  }
}

function showDeviceDetail(deviceId) {
  const d = devices.find(x => x.device_id === deviceId);
  if (!d) { showToast(t('common.error'), 'error'); return; }
  const sc = getStatusClass(d);
  const isAdmin = currentUser && currentUser.role === 'admin';
  const canControlBoundDevice = !!currentUser;
  const caps = getProductCaps(d);

  document.getElementById('device-modal-title').textContent = d.name || d.device_id;

  // CSQ 信号条
  const csq = getCsqLevel(d.csq);
  const csqVal = (Number(d.csq) >= 0 && !isNaN(Number(d.csq))) ? d.csq : '--';
  const renameButtonHtml = isAdmin
    ? ` <button class="btn btn-sm btn-outline detail-rename-btn" onclick="renameDevice('${d.device_id}', '${(d.name||'').replace(/'/g,"\\'")}')">✏️</button>`
    : '';
  const productCodeHtml = d.product_type ? ` <span class="product-code">(${d.product_type})</span>` : '';
  const rotaryRowHtml = caps.rotary
    ? `<div class="detail-row"><div class="detail-label">旋转开关当前控制</div><div class="detail-value">${d.rotary_switch === 'sub' ? '子机' : '主机'}</div></div>`
    : '';
  const lockStatusHtml = d.locked
    ? `<span class="status-tag status-locked">${t('devices.lockStatusYN')}</span>`
    : `<span class="status-tag status-normal">${t('devices.lockStatusNormal')}</span>`;
  const alarmDetailClass = hasVisibleAlarm(d) ? ' detail-value-danger' : '';
  const counterTile = (label, value, cls = '') => `
    <div class="device-counter-tile ${cls}">
      <span>${label}</span>
      <b>${value || 0}</b>
    </div>`;
  const counterTiles = [
    counterTile('上升', d.up_count, 'primary'),
    counterTile('下降', d.down_count, 'success'),
    counterTile('锁定', d.lock_count, 'warning'),
    caps.refill ? counterTile('补油', d.refill_count) : '',
    counterTile('急停', d.estop_count, 'danger'),
    caps.photoelectric ? counterTile('光电报警', d.photo_alarm_count, 'danger') : ''
  ].join('');
  const primaryControlButton = d.locked
    ? `<button class="btn btn-success" onclick="unlockDevice('${d.device_id}');closeModal('device-modal')">${t('devices.unlock')}</button>`
    : `<button class="btn btn-danger" onclick="lockDevice('${d.device_id}');closeModal('device-modal')">${t('devices.lock')}</button>`;
  const buzzerControlButton = d.has_buzzer && canControlBoundDevice
    ? (d.buzzer_on
      ? `<button class="btn btn-warning" onclick="sendBuzzerCmd('${d.device_id}', 'buzzer_off');closeModal('device-modal')">${t('devices.buzzerOff')}</button>`
      : `<button class="btn btn-success" onclick="sendBuzzerCmd('${d.device_id}', 'buzzer_on');closeModal('device-modal')">${t('devices.buzzerOn')}</button>`)
    : '';
  const controlButtonsHtml = canControlBoundDevice
    ? `${primaryControlButton}${renderMaintenanceQueryButton(d.device_id)}`
    : '';

  document.getElementById('device-modal-body').innerHTML = `
    <div class="device-detail-grid">
      <section class="device-detail-panel overview-panel">
        <div class="detail-section-title">设备概况</div>
        <div class="detail-row"><div class="detail-label">${t('devices.deviceId')}</div><div class="detail-value">${d.device_id}</div></div>
        <div class="detail-row"><div class="detail-label">${t('devices.deviceName')}</div><div class="detail-value">${d.name || '-'}${renameButtonHtml}</div></div>
        <div class="detail-row"><div class="detail-label">${t('devices.model')}</div><div class="detail-value">${d.model || 'TL-5000'}</div></div>
        <div class="detail-row"><div class="detail-label">DTU 模块</div><div class="detail-value">Tas_Dtu_892G-D</div></div>
        <div class="detail-row"><div class="detail-label">芯片 UID</div><div class="detail-value">${d.uid || '-'}</div></div>
        <div class="detail-row"><div class="detail-label">产品型号</div><div class="detail-value">${getProductName(d.product_type, d.product_type_name)}${productCodeHtml}</div></div>
        <div class="detail-row"><div class="detail-label">${t('devices.group')}</div><div class="detail-value">${d.group || t('devices.defaultGroup')}</div></div>
        <div class="detail-row"><div class="detail-label">${t('devices.status')}</div><div class="detail-value"><span class="status-tag status-${sc}">${getStatusText(sc)}</span></div></div>
        <div class="detail-row"><div class="detail-label">绑定时间</div><div class="detail-value">${formatTs(d.bound_at) || '-'}</div></div>
      </section>
      <section class="device-detail-panel runtime-panel">
        <div class="detail-section-title">实时运行</div>
        <div class="motion-status detail-motion"><span class="motion-icon">${getDirectionIcon(d)}</span><div><b>${getMotionText(d)}</b><small>${getProductName(d.product_type, d.product_type_name)}</small></div></div>
        ${rotaryRowHtml}
        <div class="detail-row"><div class="detail-label">${t('devices.direction')}</div><div class="detail-value">${getDirectionIcon(d)} ${getDirectionText(d)}</div></div>
        <div class="detail-row"><div class="detail-label">${t('common.lockInfo')}</div><div class="detail-value">${lockStatusHtml}</div></div>
        <div class="detail-row"><div class="detail-label">${t('devices.alarm')}</div><div class="detail-value${alarmDetailClass}">${getAlarmText(d.alarm, d.product_type)}</div></div>
        ${hasHeightFeedback(d) ? renderHeightPanel(d, true) : ''}
      </section>
      <section class="device-detail-panel io-panel">
        <div class="detail-section-title">输入状态</div>
        <div class="io-state-grid">${renderStateItems(d, 'input')}</div>
      </section>
      <section class="device-detail-panel safety-panel">
        <div class="detail-section-title">安全与输出</div>
        ${renderSafetyStates(d)}
        <div class="detail-subsection-title">设备输出</div>
        <div class="io-state-grid">${renderStateItems(d, 'output')}</div>
        <div class="detail-row alarm-code-row"><div class="detail-label">${t('safety.alarmCode')}</div><div class="detail-value">${hasVisibleAlarm(d) ? (d.alarm_code || 0) : 0}</div></div>
      </section>
      <section class="device-detail-panel usage-panel">
        <div class="detail-section-title">使用统计</div>
        <div class="detail-row"><div class="detail-label">${t('devices.runTime')}</div><div class="detail-value">${formatTime(d.run_time_s)}</div></div>
        <div class="detail-row"><div class="detail-label">${t('devices.onlineDuration')}</div><div class="detail-value"><span data-uptime-device="${d.device_id}">${formatLiveTime(getLiveUptimeSeconds(d))}</span></div></div>
         <div class="detail-row"><div class="detail-label">${t('devices.runCount')}</div><div class="detail-value">${d.run_count || 0}</div></div>
         <div class="detail-row"><div class="detail-label">保养状态</div><div class="detail-value">周期 ${getCycleCount(d)}/${d.maintenance_threshold || 5000} · ${isMaintenanceDue(d) ? '已到期' : '未到期'} · 累计 ${d.total_lift_count || 0} · 保养 ${d.maintenance_count || 0} 次</div></div>
        <div class="detail-row"><div class="detail-label">最后运行</div><div class="detail-value">${formatTs(d.last_run_at)}</div></div>
        <div class="detail-row"><div class="detail-label">${t('common.lastUpdate')}</div><div class="detail-value">${formatTs(d.updated_at)}</div></div>
        <div class="detail-section-title">操作计数(设备端)</div>
        <div class="device-counter-grid">${counterTiles}</div>
      </section>
      <section class="device-detail-panel link-panel">
        <div class="detail-section-title">${t('common.linkDiag')}</div>
        <div class="detail-row"><div class="detail-label">${t('link.dtuState')}</div><div class="detail-value">${formatDtuState(d.dtu_state)}</div></div>
        <div class="detail-row"><div class="detail-label">${t('link.csq')}</div><div class="detail-value">${csqVal} <span class="csq-tag ${csq.cls}">${csq.text}</span></div></div>
        <div class="signal-meter height-bar"><div class="height-bar-fill" style="width:${csq.pct}%;background:var(--primary);"></div></div>
      </section>
    </div>
    <section class="device-detail-panel device-charts-panel" id="device-detail-charts">
      <div class="device-chart-head"><div class="detail-section-title">操作趋势与类型分布</div><select onchange="loadDeviceDetailCharts('${d.device_id}', this.value)"><option value="7">近7天</option><option value="30" selected>近30天</option><option value="0">全部</option></select></div>
      <div class="device-chart-empty">暂无可统计的操作日志</div>
      <div class="device-chart-grid" style="display:none"><div class="device-chart-box"><canvas id="device-trend-chart"></canvas></div><div class="device-chart-box"><canvas id="device-type-chart"></canvas></div></div>
    </section>
    <div class="device-detail-actions">
       ${controlButtonsHtml}
       ${buzzerControlButton}
      <button class="btn btn-outline" onclick="closeModal('device-modal')">${t('common.close')}</button>
    </div>`;
  const deviceModalBody = document.getElementById('device-modal-body');
  deviceModalBody.scrollTop = 0;
  document.getElementById('device-modal').classList.add('active');
  setTimeout(() => loadDeviceDetailCharts(deviceId, 30), 0);
}

async function showAddDeviceModal() {
  const modelOptions = LIFT_MODELS.map(m => `<option value="${m}">${m}</option>`).join('');
  document.getElementById('maintenance-modal-title').textContent = t('devices.addDevice');
  document.getElementById('maintenance-modal-body').innerHTML = `
    <form onsubmit="return addDevice(event)">
      <div class="form-group"><label>${t('devices.deviceId')}</label><input type="text" id="add-device-id" placeholder="${t('devices.idPlaceholder')}" required></div>
      <div class="form-group"><label>${t('devices.deviceName')}</label><input type="text" id="add-device-name" placeholder="${t('devices.namePlaceholder')}" required></div>
      <div class="form-group"><label>产品型号</label><select id="add-product-type"><option value="screw_lift">丝杆举升机</option><option value="double_post">两柱举升机</option><option value="small_scissor">小剪举升机</option><option value="thin_scissor">超薄小剪举升机</option><option value="large_scissor">大剪举升机</option></select></div>
      <div class="form-group"><label>${t('devices.model')}</label><select id="add-device-model">${modelOptions}</select></div>
      <div class="form-group"><label>${t('devices.group')}</label><input type="text" id="add-device-group" value="${t('devices.defaultGroup')}"></div>
      <div class="form-group">
        <label>${t('devices.accessories')}</label>
        <div class="accessory-checkboxes">
          <label class="checkbox-label"><input type="checkbox" id="add-has-encoder"> ${t('devices.encoder')}</label>
          <label class="checkbox-label"><input type="checkbox" id="add-has-buzzer"> ${t('devices.buzzer')}</label>
          <label class="checkbox-label"><input type="checkbox" id="add-has-pressure"> ${t('devices.pressureSensor')}</label>
          <label class="checkbox-label"><input type="checkbox" id="add-has-display"> ${t('devices.display')}</label>
        </div>
      </div>
      <div style="display:flex;gap:8px;margin-top:16px;"><button type="submit" class="btn btn-primary">${t('common.add')}</button><button type="button" class="btn btn-outline" onclick="closeModal('maintenance-modal')">${t('common.cancel')}</button></div>
    </form>`;
  document.getElementById('maintenance-modal').classList.add('active');
}

async function addDevice(e) {
  e.preventDefault();
  try {
    await api('/devices', {
      method: 'POST',
      body: JSON.stringify({
        device_id: document.getElementById('add-device-id').value.trim(),
        name: document.getElementById('add-device-name').value.trim(),
        model: document.getElementById('add-device-model').value,
        product_type: document.getElementById('add-product-type').value,
        group_name: document.getElementById('add-device-group').value.trim(),
        has_encoder: document.getElementById('add-has-encoder').checked ? 1 : 0,
        has_buzzer: document.getElementById('add-has-buzzer').checked ? 1 : 0,
        has_pressure_sensor: document.getElementById('add-has-pressure').checked ? 1 : 0,
        has_display: document.getElementById('add-has-display').checked ? 1 : 0
      })
    });
    showToast(t('devices.addSuccess'), 'success');
    closeModal('maintenance-modal');
    fetchDevices();
  } catch (err) { showToast(err.message, 'error'); }
  return false;
}

// 用户端绑定设备:输入 SN 码 + 绑定码,后端匹配注册表完成绑定
async function showBindDeviceModal() {
  document.getElementById('maintenance-modal-title').textContent = t('devices.addDevice');
  document.getElementById('maintenance-modal-body').innerHTML = `
    <form onsubmit="return bindDeviceBySn(event)">
      <div class="form-group">
        <label>设备编号 (SN)</label>
        <input type="text" id="bind-sn" placeholder="输入设备上粘贴的SN码，如GC-2026-00001" required autocomplete="off" onblur="previewBindDevice()">
        <div style="font-size:12px;color:var(--text-muted);margin-top:6px;">请输入设备铭牌上的出厂编号,系统会自动匹配并绑定到您的账号</div>
      </div>
      <div class="form-group">
        <label>绑定码 (Bind Code)</label>
        <input type="text" id="bind-code" placeholder="输入设备铭牌上的绑定码" required autocomplete="off">
        <div style="font-size:12px;color:var(--text-muted);margin-top:6px;">绑定码印在设备铭牌上,用于验证绑定权限</div>
      </div>
      <div id="bind-preview" class="capability-panel compact" style="display:none;"></div>
      <div style="display:flex;gap:8px;margin-top:16px;">
        <button type="button" class="btn btn-outline" onclick="previewBindDevice()">查询设备</button>
        <button type="submit" class="btn btn-primary">绑定设备</button>
        <button type="button" class="btn btn-outline" onclick="closeModal('maintenance-modal')">${t('common.cancel')}</button>
      </div>
    </form>`;
  document.getElementById('maintenance-modal').classList.add('active');
  setTimeout(() => { const inp = document.getElementById('bind-sn'); if (inp) inp.focus(); }, 100);
}

async function bindDeviceBySn(e) {
  e.preventDefault();
  const sn = document.getElementById('bind-sn').value.trim();
  const bindCode = document.getElementById('bind-code').value.trim();
  if (!sn) { showToast('请输入设备编号', 'error'); return false; }
  if (!bindCode) { showToast('请输入绑定码', 'error'); return false; }
  try {
    const result = await api('/binding/bind', {
      method: 'POST',
      body: JSON.stringify({ serial: sn, bind_code: bindCode })
    });
    // 区分直接绑定(201 active)和待审批(202 pending)
    if (result.status === 'pending') {
      showToast(`设备 ${sn} 绑定申请已提交,等待管理员审批(当前绑定人数已达上限)`, 'info');
    } else {
      showToast(`设备 ${sn} 绑定成功`, 'success');
    }
    closeModal('maintenance-modal');
    fetchDevices();
  } catch (err) { showToast(err.message, 'error'); }
  return false;
}

/* ===== Commands ===== */
function commandKey(deviceId, command) { return `${deviceId}:${command}`; }
function getPendingCommand(deviceId, command) { return commandStateByKey.get(commandKey(deviceId, command)); }
function hasPendingLockControl(deviceId) { return !!getPendingCommand(deviceId, 'lock') || !!getPendingCommand(deviceId, 'unlock'); }
function beginCommandTracking(deviceId, command, response, action) {
  commandStateByKey.set(commandKey(deviceId, command), { deviceId, command, msgId: response?.msg_id || '', startedAt: Date.now(), action });
}
function attachCommandMessageId(deviceId, command, msgId) {
  const pending = getPendingCommand(deviceId, command);
  if (pending) pending.msgId = msgId || '';
}
function cancelCommandTracking(deviceId, command) {
  commandStateByKey.delete(commandKey(deviceId, command));
  refreshDeviceCommandControls(deviceId);
}
function findPendingCommand(deviceId, msgId, command) {
  const pending = getPendingCommand(deviceId, command);
  return msgId && pending?.msgId === msgId ? pending : null;
}
function normalizeCommandResult(command, result) {
  if (result === 'succeeded' ||
      (command === 'lock' && result === 'locked') ||
      (command === 'unlock' && result === 'unlocked') ||
      (command === 'maintenance_done' && result === 'maintenance_done') ||
      (command === 'reset_usage' && result === 'usage_reset')) return 'succeeded';
  return ['rejected', 'failed', 'timeout'].includes(result) ? result : 'failed';
}

async function lockDevice(id) {
  if (hasPendingLockControl(id)) return;
  console.info(`[CMD UI] click device=${id} cmd=lock at_ms=${Date.now()}`);
  beginCommandTracking(id, 'lock', null, '锁机');
  refreshDeviceCommandControls(id);
  try {
    const response = await api('/commands/lock/' + id, { method: 'POST' });
    console.info(`[CMD UI] created device=${id} cmd=lock msg_id=${response.msg_id || '-'} at_ms=${Date.now()}`);
    attachCommandMessageId(id, 'lock', response.msg_id);
    showToast(t('devices.lockSent'), 'info');
    pollCommandStatus(id, 'lock', response.msg_id);
  } catch (err) { cancelCommandTracking(id, 'lock'); showToast(err.message, 'error'); }
}

async function unlockDevice(id) {
  if (hasPendingLockControl(id)) return;
  console.info(`[CMD UI] click device=${id} cmd=unlock at_ms=${Date.now()}`);
  beginCommandTracking(id, 'unlock', null, '解锁');
  refreshDeviceCommandControls(id);
  try {
    const response = await api('/commands/unlock/' + id, { method: 'POST' });
    console.info(`[CMD UI] created device=${id} cmd=unlock msg_id=${response.msg_id || '-'} at_ms=${Date.now()}`);
    attachCommandMessageId(id, 'unlock', response.msg_id);
    showToast(t('devices.unlockSent'), 'info');
    pollCommandStatus(id, 'unlock', response.msg_id);
  } catch (err) { cancelCommandTracking(id, 'unlock'); showToast(err.message, 'error'); }
}

async function queryDevice(id) {
  if (getPendingCommand(id, 'get_status')) return;
  try {
    const response = await api('/commands/query/' + id, { method: 'POST' });
    beginCommandTracking(id, 'get_status', response, '保养状态查询');
    refreshDeviceCommandControls(id);
    showToast('查询已下发，等待设备响应', 'info');
    pollCommandStatus(id, 'get_status', response.msg_id);
  } catch (err) { showToast(err.message, 'error'); }
}

async function sendMaintenanceCommand(deviceId, command, body = {}) {
  if (getPendingCommand(deviceId, command)) return;
  try {
    const response = await api(`/commands/${command}/${encodeURIComponent(deviceId)}`, {
      method: 'POST',
      body: JSON.stringify(body)
    });
    beginCommandTracking(deviceId, command, response, command === 'maintenance_done' ? '保养确认' : '保养命令');
    refreshDeviceCommandControls(deviceId);
    showToast('命令已发送，等待设备成功回执', 'info');
    pollCommandStatus(deviceId, command, response.msg_id);
  } catch (err) {
    showToast(err.message, 'error');
  }
}

async function registerMaintenanceDone(deviceId) {
  if (getPendingCommand(deviceId, 'maintenance_done')) return;
  if (!confirm(`确认 ${deviceId} 已完成保养？将由设备保存新的保养基准。`)) return;
  await sendMaintenanceCommand(deviceId, 'maintenance_done');
}

async function clearPhotoAlarm(deviceId) {
  if (getPendingCommand(deviceId, 'clear_alarm')) return;
  const device = devices.find(d => d.device_id === deviceId);
  if (!device || !device.online) { showToast('设备离线，无法解除光电报警', 'error'); return; }
  try {
    const response = await api(`/commands/clear_alarm/${encodeURIComponent(deviceId)}`, { method: 'POST' });
    beginCommandTracking(deviceId, 'clear_alarm', response, '解除光电报警');
    refreshDeviceCard(deviceId);
    showToast('解除命令已发送，等待设备确认', 'info');
    pollCommandStatus(deviceId, 'clear_alarm', response.msg_id);
  } catch (err) {
    showToast(err.message, 'error');
  }
}

function renderMaintenanceQueryButton(deviceId, compact = false) {
  const pending = getPendingCommand(deviceId, 'get_status');
  const cls = compact ? 'btn btn-sm btn-outline' : 'btn btn-outline';
  const label = pending ? '查询中...' : '查询保养状态';
  return `<button class="${cls}" ${pending ? 'disabled' : ''} onclick="showMaintenanceStatus('${deviceId}')">${label}</button>`;
}

async function showMaintenanceStatus(deviceId) {
  const d = devices.find(item => item.device_id === deviceId);
  if (!d) return;
  document.getElementById('maintenance-modal-title').textContent = `${d.name || deviceId} · 保养状态`;
  document.getElementById('maintenance-modal-body').innerHTML = '<div class="maintenance-dialog-loading">正在查询保养记录...</div>';
  document.getElementById('maintenance-modal').classList.add('active');
  try {
    const records = await api('/maintenance?device_id=' + encodeURIComponent(deviceId));
    renderMaintenanceDialog(deviceId, records);
  } catch (err) {
    document.getElementById('maintenance-modal-body').innerHTML = `<div class="empty-state"><p>${err.message}</p></div>`;
  }
}

function renderMaintenanceDialog(deviceId, records = []) {
  const d = devices.find(item => item.device_id === deviceId);
  if (!d) return;
  const due = isMaintenanceDue(d);
  const cycle = getCycleCount(d);
  const pct = Math.min(100, (cycle / (d.maintenance_threshold || 5000)) * 100);
  const threshold = d.maintenance_threshold || 5000;
  const pending = getPendingCommand(deviceId, 'maintenance_done');
  const actionLabel = pending ? '等待设备回执...' : '发送保养完成';
  const history = records.length ? records.slice(0, 10).map(record => `<div class="maintenance-history-row"><div><strong>${record.type || '保养'}</strong><span>${record.handler || '系统记录'}</span></div><div><b>累计 ${record.total_lift_count || 0} 次</b><span>${formatTs(record.created_at)}</span></div></div>`).join('') : '<div class="maintenance-history-empty">暂无保养记录</div>';
  document.getElementById('maintenance-modal-body').innerHTML = `
    <div class="maintenance-dialog">
      <div class="maintenance-dialog-status ${due ? 'is-due' : ''}"><span>${due ? '!' : '✓'}</span><div><strong>${due ? '已到保养周期' : '保养状态正常'}</strong><small>平台记录，下次保底 ${threshold} 次</small></div></div>
      <div class="maintenance-dialog-progress"><div><span>本周期举升</span><strong>${cycle} / ${threshold}</strong></div><div class="maintenance-progress-track"><i style="width:${pct}%"></i></div></div>
      <div class="maintenance-stat-grid"><div><span>累计举升</span><strong>${d.total_lift_count || 0}</strong></div><div><span>已保养</span><strong>${d.maintenance_count || 0} 次</strong></div><div><span>上次保养时累计</span><strong>${d.last_maintenance_total || 0}</strong></div></div>
      <div class="maintenance-history"><div class="detail-section-title">最近保养记录</div>${history}</div>
      <div class="maintenance-dialog-actions"><button class="btn btn-success" ${pending ? 'disabled' : ''} onclick="registerMaintenanceDone('${deviceId}')">${actionLabel}</button><button class="btn btn-outline" onclick="closeModal('maintenance-modal')">关闭</button></div>
    </div>`;
}

async function pollCommandStatus(deviceId, commandName, msgId) {
  // Covers two 15-second device response windows, retry delay and scheduler jitter.
  const deadline = Date.now() + 40000;
  while (getPendingCommand(deviceId, commandName)?.msgId === msgId && Date.now() < deadline) {
    await new Promise(resolve => setTimeout(resolve, 1000));
    try {
      const command = await api('/commands/status/' + encodeURIComponent(msgId));
      if (['succeeded', 'rejected', 'failed', 'timeout'].includes(command.status)) {
        const text = command.status === 'succeeded' ? '设备已响应' : (command.result || command.status);
        finishCommandTracking(deviceId, commandName, command.status, text);
        return;
      }
    } catch (error) {
      if (error.code === 'AUTH_EXPIRED') return;
      console.warn('[command] status poll failed:', error.message);
    }
  }
  if (getPendingCommand(deviceId, commandName)?.msgId === msgId) {
    finishCommandTracking(deviceId, commandName, 'timeout', '设备响应超时');
  }
}

function finishCommandTracking(deviceId, commandName, status, detail) {
  const key = commandKey(deviceId, commandName);
  const pending = commandStateByKey.get(key);
  if (!pending) return;
  commandStateByKey.delete(key);
  console.info(`[CMD UI] finished device=${deviceId} cmd=${commandName} msg_id=${pending.msgId || '-'} status=${status} elapsed_ms=${Date.now() - pending.startedAt} at_ms=${Date.now()}`);
  if (status === 'succeeded' && (commandName === 'lock' || commandName === 'unlock')) {
    const device = devices.find(item => item.device_id === deviceId);
    if (device) {
      device.locked = commandName === 'lock';
      device.state = commandName === 'lock' ? 'locked' : 'idle';
    }
  }
  refreshDeviceCommandControls(deviceId);
  const action = pending.action || '查询';
  showToast(status === 'succeeded' ? `${action}成功：${detail}` : `${action}失败：${detail}`, status === 'succeeded' ? 'success' : 'error');
  if (status === 'succeeded' && commandName === 'clear_alarm') {
    fetchUnackAlarms();
    if (currentPage === 'alarms') loadAlarms();
  }
  fetchDevices().then(() => {
    if (['get_status', 'maintenance_done'].includes(commandName) && document.getElementById('maintenance-modal').classList.contains('active')) {
      api('/maintenance?device_id=' + encodeURIComponent(deviceId)).then(records => renderMaintenanceDialog(deviceId, records)).catch(() => {});
    }
  });
}

async function renameDevice(deviceId, currentName) {
  const newName = prompt('请输入新名称', currentName);
  if (!newName || newName.trim() === '' || newName.trim() === currentName) return;
  try {
    await api('/commands/rename/' + deviceId, {
      method: 'POST',
      body: JSON.stringify({ name: newName.trim() })
    });
    showToast('设备名称已更新', 'success');
    const d = devices.find(x => x.device_id === deviceId);
    if (d) d.name = newName.trim();
    renderCurrentPage();
  } catch (err) { showToast(err.message, 'error'); }
}

async function deleteDevice(deviceId, deviceName) {
  if (!confirm(t('devices.deleteConfirm').replace('{name}', deviceName))) return;
  try {
    await api(`/devices/${deviceId}`, { method: 'DELETE' });
    showToast(t('devices.deleteSuccess'), 'success');
    fetchDevices();
  } catch (err) { showToast(err.message, 'error'); }
}

async function sendBuzzerCmd(deviceId, cmd) {
  try {
    await api(`/commands/${cmd}/${deviceId}`, { method: 'POST' });
    showToast(t(cmd === 'buzzer_on' ? 'devices.buzzerOnSent' : 'devices.buzzerOffSent'), 'success');
  } catch (err) { showToast(err.message, 'error'); }
}

/* ===== Alarms Page ===== */
const ALARM_TYPE_LABELS = {
  collision: '碰撞报警', collision_up: '上行碰撞', collision_down: '下行碰撞',
  stall: '运行超时', fault: '设备故障', balance_timeout: '平衡超时',
  safety_bar: '安全杆触发', overheight: '超高报警', Emergency: '急停触发',
  estop: '急停触发', photo_alarm: '光电报警', unknown: '未知报警'
};
const ALARM_FILTER_TYPES = ['fault', 'stall', 'photo_alarm', 'safety_bar', 'overheight', 'balance_timeout', 'estop', 'unknown'];
let alarmRecords = [];
let alarmSummary = null;
let alarmRequestSerial = 0;
let alarmFilters = {
  status: '', level: '', deviceId: '', type: '', query: '', range: 'all', startDate: '', endDate: ''
};

function alarmTypeLabel(type) { return ALARM_TYPE_LABELS[type] || type || '未知报警'; }
function alarmLevelLabel(level) { return level === 'danger' ? '严重' : '警告'; }
function alarmStatusMeta(alarm) {
  if (alarm.resolved_at) return { label: '已解除', cls: 'resolved' };
  if (!alarm.acknowledged) return { label: '待确认', cls: 'unack' };
  return { label: '已确认', cls: 'ack' };
}
function alarmDateInput(date) {
  const pad = value => String(value).padStart(2, '0');
  return `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())}`;
}
function alarmRangeDates() {
  if (alarmFilters.range === 'all') return { start: '', end: '' };
  if (alarmFilters.range === 'custom') return { start: alarmFilters.startDate, end: alarmFilters.endDate };
  const days = alarmFilters.range === '24h' ? 1 : alarmFilters.range === '30d' ? 30 : 7;
  const start = new Date();
  start.setDate(start.getDate() - days);
  return { start: alarmDateInput(start), end: alarmDateInput(new Date()) };
}
function alarmQueryParams() {
  const params = new URLSearchParams();
  if (alarmFilters.status) params.set('status', alarmFilters.status);
  if (alarmFilters.level) params.set('level', alarmFilters.level);
  if (alarmFilters.deviceId) params.set('device_id', alarmFilters.deviceId);
  if (alarmFilters.type) params.set('type', alarmFilters.type);
  if (alarmFilters.query.trim()) params.set('q', alarmFilters.query.trim());
  const dates = alarmRangeDates();
  if (dates.start) params.set('start_date', dates.start);
  if (dates.end) params.set('end_date', dates.end);
  params.set('limit', '1000');
  return params;
}
function alarmTypeOptions(selected) {
  return `<option value="">全部类型</option>${ALARM_FILTER_TYPES.map(type => `<option value="${type}" ${selected === type ? 'selected' : ''}>${alarmTypeLabel(type)}</option>`).join('')}`;
}
function alarmDeviceOptions(selected) {
  return `<option value="">全部设备</option>${devices.map(device => `<option value="${escapeHTML(device.device_id)}" ${selected === device.device_id ? 'selected' : ''}>${escapeHTML(device.name || device.device_id)}</option>`).join('')}`;
}

function renderAlarms() {
  const summary = alarmSummary || {};
  return `
    <section class="alarm-console">
      <div class="alarm-page-head">
        <div>
          <div class="alarm-eyebrow">SAFETY / EVENTS</div>
          <h3>报警处置中心</h3>
          <p>集中查看设备异常，先确认责任人，再跟踪解除结果。</p>
        </div>
        <div class="alarm-live-actions">
          <span class="alarm-live-state"><span class="alarm-live-dot"></span>实时同步</span>
          <button class="btn btn-outline btn-sm" onclick="loadAlarms({announce:true})" title="刷新报警列表">↻ 刷新</button>
        </div>
      </div>

      <div class="alarm-summary-grid" id="alarm-summary-grid">
        <div class="alarm-summary-card alarm-summary-total"><span class="alarm-summary-label">全部记录</span><strong id="alarm-summary-total">${summary.total ?? '—'}</strong><small>当前保留的报警事件</small></div>
        <div class="alarm-summary-card alarm-summary-active"><span class="alarm-summary-label">待处置</span><strong id="alarm-summary-active">${summary.active ?? '—'}</strong><small>尚未解除的事件</small></div>
        <div class="alarm-summary-card alarm-summary-unack"><span class="alarm-summary-label">未确认</span><strong id="alarm-summary-unack">${summary.unacknowledged ?? '—'}</strong><small>需要值班人员跟进</small></div>
        <div class="alarm-summary-card alarm-summary-critical"><span class="alarm-summary-label">严重未解除</span><strong id="alarm-summary-critical">${summary.critical ?? '—'}</strong><small>高优先级安全事件</small></div>
      </div>

      <div class="alarm-filter-panel">
        <div class="alarm-filter-heading"><div><strong>筛选报警</strong><span>组合条件后点击查询，列表和数量会同步更新</span></div><button class="alarm-clear-btn" onclick="resetAlarmFilters()">清空条件</button></div>
        <div class="alarm-status-tabs" role="tablist" aria-label="报警状态">
          ${[['active','待处置'],['unack','未确认'],['resolved','已解除'],['','全部']].map(([value,label]) => `<button class="alarm-status-tab ${alarmFilters.status === value ? 'active' : ''}" data-alarm-status="${value}" onclick="setAlarmStatusFilter('${value}')">${label}</button>`).join('')}
        </div>
        <div class="alarm-filter-grid">
          <label class="alarm-filter-field alarm-filter-search"><span>关键词</span><input id="alarm-filter-query" value="${escapeHTML(alarmFilters.query)}" placeholder="设备名、设备ID、报警内容" onkeydown="if(event.key==='Enter')applyAlarmFilters()"></label>
          <label class="alarm-filter-field"><span>设备</span><select id="alarm-filter-device">${alarmDeviceOptions(alarmFilters.deviceId)}</select></label>
          <label class="alarm-filter-field"><span>报警类型</span><select id="alarm-filter-type">${alarmTypeOptions(alarmFilters.type)}</select></label>
          <label class="alarm-filter-field"><span>级别</span><select id="alarm-filter-level"><option value="">全部级别</option><option value="danger" ${alarmFilters.level === 'danger' ? 'selected' : ''}>严重</option><option value="warning" ${alarmFilters.level === 'warning' ? 'selected' : ''}>警告</option></select></label>
          <label class="alarm-filter-field"><span>时间范围</span><select id="alarm-filter-range" onchange="toggleAlarmCustomDates()"><option value="24h" ${alarmFilters.range === '24h' ? 'selected' : ''}>近 24 小时</option><option value="7d" ${alarmFilters.range === '7d' ? 'selected' : ''}>近 7 天</option><option value="30d" ${alarmFilters.range === '30d' ? 'selected' : ''}>近 30 天</option><option value="custom" ${alarmFilters.range === 'custom' ? 'selected' : ''}>自定义</option><option value="all" ${alarmFilters.range === 'all' ? 'selected' : ''}>全部时间</option></select></label>
          <div class="alarm-custom-dates ${alarmFilters.range === 'custom' ? 'visible' : ''}" id="alarm-custom-dates"><input type="date" id="alarm-filter-start" value="${alarmFilters.startDate}"><span>至</span><input type="date" id="alarm-filter-end" value="${alarmFilters.endDate}"></div>
          <button class="btn btn-primary alarm-filter-submit" onclick="applyAlarmFilters()">查询报警</button>
        </div>
      </div>

      <div class="alarm-results-bar"><div><strong id="alarm-result-count">加载中...</strong><span id="alarm-result-meta">—</span></div><div class="alarm-results-actions"><span id="alarm-last-refresh">—</span><button class="btn btn-outline btn-sm" onclick="exportAlarmsCSV()">↓ 导出 CSV</button><button class="btn btn-danger btn-sm" onclick="deleteFilteredAlarms()">清空当前记录</button></div></div>
      <div class="alarm-table-shell" id="alarms-list"><div class="loading">${t('common.loading')}</div></div>
    </section>`;
}

function toggleAlarmCustomDates() {
  const range = document.getElementById('alarm-filter-range')?.value || '7d';
  document.getElementById('alarm-custom-dates')?.classList.toggle('visible', range === 'custom');
}

function setAlarmStatusFilter(status) {
  alarmFilters.status = status;
  document.querySelectorAll('[data-alarm-status]').forEach(button => button.classList.toggle('active', button.dataset.alarmStatus === status));
  loadAlarms();
}

function applyAlarmFilters() {
  alarmFilters.query = document.getElementById('alarm-filter-query')?.value || '';
  alarmFilters.deviceId = document.getElementById('alarm-filter-device')?.value || '';
  alarmFilters.type = document.getElementById('alarm-filter-type')?.value || '';
  alarmFilters.level = document.getElementById('alarm-filter-level')?.value || '';
  alarmFilters.range = document.getElementById('alarm-filter-range')?.value || '7d';
  alarmFilters.startDate = document.getElementById('alarm-filter-start')?.value || '';
  alarmFilters.endDate = document.getElementById('alarm-filter-end')?.value || '';
  loadAlarms({ announce: true });
}

function resetAlarmFilters() {
  alarmFilters = { status: '', level: '', deviceId: '', type: '', query: '', range: 'all', startDate: '', endDate: '' };
  renderCurrentPage();
  loadAlarms();
}

function renderAlarmSummary(summary = {}) {
  const summaryIds = { total: 'total', active: 'active', unacknowledged: 'unack', critical: 'critical' };
  Object.entries(summaryIds).forEach(([key, id]) => {
    const node = document.getElementById(`alarm-summary-${id}`);
    if (node) node.textContent = Number(summary[key] || 0).toLocaleString();
  });
}

function renderAlarmResults() {
  const list = document.getElementById('alarms-list');
  if (!list) return;
  const count = document.getElementById('alarm-result-count');
  const meta = document.getElementById('alarm-result-meta');
  if (count) count.textContent = `${alarmRecords.length.toLocaleString()} 条报警`;
  if (meta) meta.textContent = alarmFilters.status === 'active' ? '待处置视图' : '按当前条件筛选';

  if (!alarmRecords.length) {
    list.innerHTML = `<div class="alarm-empty"><div class="alarm-empty-mark">✓</div><h4>没有符合条件的报警</h4><p>可以清空筛选，或切换到“全部”查看历史记录。</p><button class="btn btn-outline btn-sm" onclick="resetAlarmFilters()">清空筛选</button></div>`;
    return;
  }

  const rows = alarmRecords.map(alarm => {
    const status = alarmStatusMeta(alarm);
    const device = devices.find(item => item.device_id === alarm.device_id);
    const deviceOnline = !!device?.online;
    const deviceName = escapeHTML(alarm.device_name || alarm.device_id || '未知设备');
    const message = escapeHTML(alarm.message || '设备上报了异常状态');
    const type = escapeHTML(alarmTypeLabel(alarm.alarm_type));
    const level = alarm.level === 'danger' ? 'danger' : 'warning';
    const actionButtons = [
      !alarm.acknowledged ? `<button class="alarm-action alarm-action-ack" onclick="event.stopPropagation();ackAlarm(${alarm.id})">确认</button>` : '',
      !alarm.resolved_at && deviceOnline ? `<button class="alarm-action alarm-action-resolve" onclick="event.stopPropagation();resolveAlarm(${alarm.id}, ${JSON.stringify(alarm.device_id || '')})">清除报警</button>` : '',
      `<button class="alarm-action alarm-action-delete" onclick="event.stopPropagation();deleteAlarmRecord(${alarm.id})">删除记录</button>`
    ].filter(Boolean).join('');
    return `<tr class="alarm-row" data-alarm-id="${alarm.id}" onclick="showAlarmDetail(${alarm.id})">
      <td><span class="alarm-severity ${level}"><i></i>${alarmLevelLabel(level)}</span></td>
      <td><div class="alarm-event-title">${type}</div><div class="alarm-event-id">#${alarm.id}</div></td>
      <td><div class="alarm-device-cell"><strong>${deviceName}</strong><small>${escapeHTML(alarm.device_id || '-')}</small><span class="alarm-device-live ${deviceOnline ? 'online' : 'offline'}">${deviceOnline ? '在线' : '离线'}</span></div></td>
      <td><span class="alarm-message-cell">${message}</span></td>
      <td><div class="alarm-time-cell"><strong>${formatTs(alarm.created_at)}</strong><small>${formatAlarmAge(alarm.created_at)}</small></div></td>
      <td><span class="alarm-status ${status.cls}">${status.label}</span>${alarm.resolved_at ? `<small class="alarm-resolved-time">${formatTs(alarm.resolved_at)}</small>` : ''}</td>
      <td><div class="alarm-row-actions">${actionButtons || '<button class="alarm-action alarm-action-view" onclick="event.stopPropagation();showAlarmDetail(' + alarm.id + ')">查看</button>'}</div></td>
    </tr>`;
  }).join('');

  list.innerHTML = `<div class="alarm-table-scroll"><table class="alarm-table"><thead><tr><th>级别</th><th>事件</th><th>设备</th><th>现场信息</th><th>发生时间</th><th>处置状态</th><th></th></tr></thead><tbody>${rows}</tbody></table></div>`;
}

function formatAlarmAge(iso) {
  if (!iso) return '-';
  const delta = Math.max(0, Date.now() - new Date(iso).getTime());
  const minutes = Math.floor(delta / 60000);
  if (minutes < 1) return '刚刚';
  if (minutes < 60) return `${minutes} 分钟前`;
  const hours = Math.floor(minutes / 60);
  if (hours < 24) return `${hours} 小时前`;
  return `${Math.floor(hours / 24)} 天前`;
}

async function loadAlarms(options = {}) {
  const requestId = ++alarmRequestSerial;
  const list = document.getElementById('alarms-list');
  if (list && !alarmRecords.length) list.innerHTML = `<div class="loading">正在读取报警...</div>`;
  try {
    const params = alarmQueryParams();
    const [alarms, summary] = await Promise.all([api(`/alarms?${params.toString()}`), api(`/alarms/summary?${params.toString()}`)]);
    if (requestId !== alarmRequestSerial) return;
    alarmRecords = Array.isArray(alarms) ? alarms : [];
    alarmSummary = summary || {};
    renderAlarmSummary(alarmSummary);
    renderAlarmResults();
    const refreshed = document.getElementById('alarm-last-refresh');
    if (refreshed) refreshed.textContent = `更新于 ${new Date().toLocaleTimeString('zh-CN', { hour: '2-digit', minute: '2-digit', second: '2-digit' })}`;
    if (options.announce) showToast(`已更新，找到 ${alarmRecords.length} 条报警`, 'success');
  } catch (err) {
    if (list) list.innerHTML = `<div class="alarm-empty alarm-empty-error"><div class="alarm-empty-mark">!</div><h4>报警读取失败</h4><p>${escapeHTML(err.message)}</p><button class="btn btn-outline btn-sm" onclick="loadAlarms({announce:true})">重试</button></div>`;
    if (options.announce) showToast(err.message, 'error');
  }
}

function showAlarmDetail(id) {
  const alarm = alarmRecords.find(item => Number(item.id) === Number(id));
  if (!alarm) return;
  const status = alarmStatusMeta(alarm);
  const device = devices.find(item => item.device_id === alarm.device_id);
  const deviceId = JSON.stringify(alarm.device_id || '');
  document.getElementById('alarm-modal-title').textContent = `${alarmTypeLabel(alarm.alarm_type)} · #${alarm.id}`;
  document.getElementById('alarm-modal-body').innerHTML = `
    <div class="alarm-detail-head"><span class="alarm-severity ${alarm.level === 'danger' ? 'danger' : 'warning'}"><i></i>${alarmLevelLabel(alarm.level)}</span><span class="alarm-status ${status.cls}">${status.label}</span></div>
    <div class="alarm-detail-grid">
      <div><span>设备</span><strong>${escapeHTML(alarm.device_name || alarm.device_id || '-')}</strong><small>${escapeHTML(alarm.device_id || '-')}</small></div>
      <div><span>发生时间</span><strong>${formatTs(alarm.created_at)}</strong><small>${formatAlarmAge(alarm.created_at)}</small></div>
      <div><span>确认状态</span><strong>${alarm.acknowledged ? '已确认' : '待确认'}</strong></div>
      <div><span>解除时间</span><strong>${alarm.resolved_at ? formatTs(alarm.resolved_at) : '尚未解除'}</strong></div>
    </div>
    <div class="alarm-detail-message"><span>现场信息</span><p>${escapeHTML(alarm.message || '设备未提供附加描述')}</p></div>
    <div class="alarm-detail-actions">
      ${!alarm.acknowledged ? `<button class="btn btn-outline" onclick="ackAlarm(${alarm.id})">确认报警</button>` : ''}
      ${!alarm.resolved_at && device?.online ? `<button class="btn btn-danger" onclick="resolveAlarm(${alarm.id}, ${deviceId})">发送清除命令</button>` : ''}
      <button class="btn btn-outline" onclick="deleteAlarmRecord(${alarm.id})">删除记录</button>
      ${device ? `<button class="btn btn-outline" onclick="closeModal('alarm-modal');loadPage('devices');setTimeout(() => showDeviceDetail(${deviceId}), 0)">打开设备</button>` : ''}
    </div>`;
  document.getElementById('alarm-modal').classList.add('active');
}

async function ackAlarm(id) {
  try { await api(`/alarms/${id}/acknowledge`, { method: 'PUT' }); showToast(t('alarms.ackSuccess'), 'success'); closeModal('alarm-modal'); fetchUnackAlarms(); loadAlarms(); } catch (err) { showToast(err.message, 'error'); }
}

async function resolveAlarm(id, deviceId) {
  if (getPendingCommand(deviceId, 'clear_alarm')) return;
  try {
    if (!deviceId) throw new Error('缺少设备ID，无法下发清警命令');
    const device = devices.find(item => item.device_id === deviceId);
    if (device && !device.online) throw new Error('设备当前离线，无法发送清除命令');
    const response = await api(`/commands/clear_alarm/${encodeURIComponent(deviceId)}`, { method: 'POST' });
    beginCommandTracking(deviceId, 'clear_alarm', response, '清除报警');
    showToast('清除报警命令已发送，等待设备确认', 'info');
    pollCommandStatus(deviceId, 'clear_alarm', response.msg_id);
  } catch (err) {
    showToast(err.message, 'error');
  }
}

async function deleteAlarmRecord(id) {
  if (!window.confirm(`确定删除报警记录 #${id} 吗？此操作只删除网站历史，不能恢复。`)) return;
  try {
    await api(`/alarms/${id}`, { method: 'DELETE' });
    closeModal('alarm-modal');
    fetchUnackAlarms();
    loadAlarms({ announce: true });
  } catch (err) {
    showToast(err.message, 'error');
  }
}

async function deleteFilteredAlarms() {
  if (!alarmRecords.length) {
    showToast('当前筛选没有可删除的报警记录', 'error');
    return;
  }
  const count = alarmRecords.length;
  if (!window.confirm(`确定删除当前筛选出的 ${count} 条报警记录吗？此操作只删除网站历史，不能恢复。`)) return;
  try {
    const result = await api(`/alarms?${alarmQueryParams().toString()}`, { method: 'DELETE' });
    fetchUnackAlarms();
    await loadAlarms();
    showToast(result.message || `已删除 ${count} 条报警记录`, 'success');
  } catch (err) {
    showToast(err.message, 'error');
  }
}

function exportAlarmsCSV() {
  if (!alarmRecords.length) { showToast('当前没有可导出的报警', 'error'); return; }
  const rows = [['报警ID', '设备', '设备ID', '类型', '级别', '状态', '发生时间', '解除时间', '现场信息']];
  alarmRecords.forEach(alarm => rows.push([
    alarm.id, alarm.device_name || '', alarm.device_id || '', alarmTypeLabel(alarm.alarm_type), alarmLevelLabel(alarm.level), alarmStatusMeta(alarm).label,
    alarm.created_at || '', alarm.resolved_at || '', String(alarm.message || '').replace(/[\r\n,]+/g, ' ')
  ]));
  const csv = '\uFEFF' + rows.map(row => row.map(value => `"${String(value).replace(/"/g, '""')}"`).join(',')).join('\n');
  const url = URL.createObjectURL(new Blob([csv], { type: 'text/csv;charset=utf-8;' }));
  const link = document.createElement('a'); link.href = url; link.download = `报警记录_${new Date().toISOString().slice(0, 10)}.csv`; link.click(); URL.revokeObjectURL(url);
}

async function previewBindDevice() {
  const sn = document.getElementById('bind-sn')?.value.trim();
  const box = document.getElementById('bind-preview');
  if (!sn || !box) return;
  try {
    const info = await api('/binding/lookup?serial=' + encodeURIComponent(sn));
    const accessories = [
      info.has_encoder ? '高度编码器' : '',
      info.has_buzzer ? '蜂鸣器' : '',
      info.has_pressure_sensor ? '压力传感器' : '',
      info.has_display ? '显示屏' : ''
    ].filter(Boolean);
    box.style.display = '';
    box.innerHTML = `
      <div class="capability-title">设备预览</div>
      <div style="font-size:13px;font-weight:600;">${info.display_name || info.model || info.serial}</div>
      <div style="font-size:12px;color:var(--text-muted);margin-top:4px;">${info.product_type || 'double_post'} · 当前 ${info.current_bindings || 0}/${info.auto_approve_limit || 3} 绑定${info.my_binding_status === 'active' ? ' · 您已绑定' : info.my_binding_status === 'pending' ? ' · 您的申请待审批' : ''}</div>
      <div class="io-chip-row">${accessories.length ? accessories.map(a => `<span class="io-chip active">${a}</span>`).join('') : '<span class="io-chip muted">无高度编码器，页面显示运行状态</span>'}</div>
    `;
  } catch (err) {
    box.style.display = '';
    box.innerHTML = `<div style="color:var(--danger);font-size:12px;">${err.message}</div>`;
  }
}

/* ===== Maintenance Page ===== */
function renderMaintenance() {
  return `
    <div class="filter-bar">
      <div class="form-group" style="margin-bottom:0"><select id="mt-device-filter"><option value="">${t('common.allDevices')}</option>${devices.map(d => `<option value="${d.device_id}">${d.name || d.device_id}</option>`).join('')}</select></div>
      <div class="form-group" style="margin-bottom:0"><select id="mt-type-filter"><option value="">${t('common.allTypes')}</option><option value="保养">${t('maintenance.serviceType')}</option><option value="维修">${t('maintenance.repairType')}</option></select></div>
      <button class="btn btn-primary" onclick="loadMaintenance()">${t('common.query')}</button>
      <button class="btn btn-outline" onclick="showAddMaintenanceModal()">+ ${t('maintenance.add')}</button>
      <button class="btn btn-outline" onclick="exportMaintenance()">${t('common.export')} CSV</button>
    </div>
    <div id="maintenance-list"><div class="loading">${t('common.loading')}</div></div>`;
}

async function loadMaintenance() {
  try {
    const params = [];
    const dev = document.getElementById('mt-device-filter')?.value;
    const type = document.getElementById('mt-type-filter')?.value;
    if (dev) params.push('device_id=' + dev);
    if (type) params.push('type=' + type);
    const records = await api('/maintenance?' + params.join('&'));
    const list = document.getElementById('maintenance-list');
    if (!list) return;
    if (records.length === 0) { list.innerHTML = `<div class="empty-state"><div class="empty-icon">🔧</div><h3>${t('maintenance.noRecords')}</h3></div>`; return; }
    list.innerHTML = `<div class="device-table"><div class="table-header"><span>${t('maintenance.records')} (${records.length}${t('unit.record')})</span></div><table><thead><tr><th>${t('logs.device')}</th><th>${t('maintenance.type')}</th><th>${t('maintenance.description')}</th><th>${t('maintenance.handler')}</th><th>${t('maintenance.result')}</th><th>${t('maintenance.nextDate')}</th><th>${t('maintenance.cost')}</th><th>${t('logs.time')}</th><th>${t('logs.action')}</th></tr></thead><tbody>${records.map(r => `<tr>
      <td>${r.device_name || r.device_id}</td>
      <td><span class="status-tag ${r.type === '保养' ? 'status-maintenance' : 'status-fault'}">${r.type === '保养' ? t('maintenance.serviceType') : t('maintenance.repairType')}</span></td>
      <td>${r.description || '-'}</td>
      <td>${r.handler || '-'}</td>
      <td>${r.result || '-'}</td>
      <td>${r.next_date || '-'}</td>
      <td>${r.cost || '-'}</td>
      <td>${formatTs(r.created_at)}</td>
      <td>${currentUser && currentUser.role === 'admin' ? `<button class="btn btn-sm btn-danger" onclick="deleteMaintenance(${r.id})">${t('common.delete')}</button>` : ''}</td>
    </tr>`).join('')}</tbody></table></div>`;
  } catch (err) { showToast(err.message, 'error'); }
}

function showAddMaintenanceModal() {
  if (devices.length === 0) { showToast(t('devices.noDevices'), 'warning'); return; }
  document.getElementById('maintenance-modal-title').textContent = t('maintenance.add');
  document.getElementById('maintenance-modal-body').innerHTML = `
    <form onsubmit="return submitMaintenance(event)">
      <div class="form-group"><label>${t('logs.device')}</label><select id="mt-device" required>${devices.map(d => `<option value="${d.device_id}">${d.name || d.device_id}</option>`).join('')}</select></div>
      <div class="form-group"><label>${t('maintenance.type')}</label><select id="mt-type"><option value="保养">${t('maintenance.serviceType')}</option><option value="维修">${t('maintenance.repairType')}</option></select></div>
      <div class="form-group"><label>${t('maintenance.description')}</label><textarea id="mt-desc" rows="2" placeholder="${t('maintenance.descPh')}"></textarea></div>
      <div class="form-group"><label>${t('maintenance.handler')}</label><input type="text" id="mt-handler" placeholder="${t('maintenance.handlerPh')}"></div>
      <div class="form-group"><label>${t('maintenance.result')}</label><input type="text" id="mt-result" value="${t('maintenance.inProgress')}"></div>
      <div class="form-group"><label>${t('maintenance.nextDate')}</label><input type="date" id="mt-next-date"></div>
      <div class="form-group"><label>${t('maintenance.cost')}(${t('unit.currency')})</label><input type="number" id="mt-cost" step="0.01" value="0"></div>
      <div style="display:flex;gap:8px;margin-top:16px;"><button type="submit" class="btn btn-primary">${t('common.submit')}</button><button type="button" class="btn btn-outline" onclick="closeModal('maintenance-modal')">${t('common.cancel')}</button></div>
    </form>`;
  document.getElementById('maintenance-modal').classList.add('active');
}

async function submitMaintenance(e) {
  e.preventDefault();
  try {
    await api('/maintenance', {
      method: 'POST',
      body: JSON.stringify({
        device_id: document.getElementById('mt-device').value,
        type: document.getElementById('mt-type').value,
        description: document.getElementById('mt-desc').value,
        handler: document.getElementById('mt-handler').value,
        result: document.getElementById('mt-result').value,
        next_date: document.getElementById('mt-next-date').value,
        cost: parseFloat(document.getElementById('mt-cost').value) || 0
      })
    });
    showToast(t('common.recordAdded'), 'success');
    closeModal('maintenance-modal');
    loadMaintenance();
  } catch (err) { showToast(err.message, 'error'); }
  return false;
}

async function deleteMaintenance(id) {
  if (!confirm(t('common.confirmDelete'))) return;
  try { await api('/maintenance/' + id, { method: 'DELETE' }); showToast(t('common.deleted'), 'success'); loadMaintenance(); } catch (err) { showToast(err.message, 'error'); }
}

async function exportMaintenance() {
  try {
    const res = await fetch(API_BASE + '/maintenance/export', { headers: { 'Authorization': 'Bearer ' + token } });
    const blob = await res.blob();
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a'); a.href = url;
    a.download = `${t('maintenance.records')}_${new Date().toLocaleDateString('zh-CN').replace(/\//g, '-')}.csv`;
    a.click(); URL.revokeObjectURL(url);
  } catch (err) { showToast(err.message, 'error'); }
}

/* ===== Statistics Page ===== */
// 操作类型列定义(用于统计表和 CSV 导出)
const OP_STAT_COLUMNS = [
  { key: 'up', label: '上升' },
  { key: 'down', label: '下降' },
  { key: 'lock', label: '锁定' },
  { key: 'refill', label: '补油' },
  { key: 'estop', label: '急停' },
  { key: 'photo_alarm', label: '光电报警' },
  { key: 'upper_limit', label: '上限位' },
  { key: 'lower_limit', label: '下限位' },
  { key: 'rotary_switch', label: '旋转开关' },
  { key: 'power_on', label: '开机' },
  { key: 'power_off', label: '关机' }
];

function renderStatistics() {
  return `
    <div class="filter-bar">
      <button class="btn btn-primary" onclick="loadStatistics()">刷新</button>
      <button class="btn btn-outline" onclick="exportStatsCSV()">导出CSV</button>
    </div>
    <div id="statistics-list"><div class="loading">加载中...</div></div>`;
}

async function loadStatistics() {
  const container = document.getElementById('statistics-list');
  if (!container) return;
  try {
    const deviceList = devices.filter(d => d && d.device_id);
    if (deviceList.length === 0) {
      container.innerHTML = '<div class="empty-state"><div class="empty-icon">📊</div><h3>暂无设备数据</h3><p>等待设备加载...</p></div>';
      return;
    }
    const stats = await api('/device-ops/stats?group_by=type');
    const opMap = {};
    if (stats.by_device) {
      stats.by_device.forEach(d => { opMap[d.device_serial] = d; });
    }
    // 合并表: 设备运行明细 + 操作统计列
    let count = deviceList.length;
    let html = `<div class="device-table"><div class="table-header"><span>设备运行明细与操作统计 (${count}台)</span></div><div class="table-wrap" style="overflow-x:auto;"><table class="data-table"><thead><tr>`;
    const cols = [
      { key: 'name', label: '设备名称' },
      { key: 'device_id', label: '设备编号' },
      { key: 'model', label: '型号' },
      { key: 'status', label: '状态' },
      { key: 'run_time', label: '运行时间' },
      { key: 'run_count', label: '运行次数' },
      { key: 'avg_single', label: '平均单次(min)' },
      { key: 'feedback', label: '反馈/状态' },
      ...OP_STAT_COLUMNS.map(c => ({ key: c.key, label: c.label }))
    ];
    for (const col of cols) {
      html += `<th style="text-align:center;white-space:nowrap;">${col.label}</th>`;
    }
    html += '</thead><tbody>';
    for (const d of devices) {
      const avgSingle = d.run_count > 0 ? ((d.run_time_s || 0) / 60 / d.run_count).toFixed(1) : 0;
      const feedback = hasHeightFeedback(d)
        ? `${d.height_left_mm || 0}/${d.height_right_mm || 0}mm · 偏差 ${d.height_diff_mm || 0}mm`
        : `${getStateText(d.state)} · ${getDirectionText(d)}`;
      const sc = getStatusClass(d);
      const od = opMap[d.device_id];
      html += `<tr>
        <td style="font-weight:500;"><span class="device-name" onclick="showDeviceDetail('${d.device_id}')">${d.name || d.device_id}</span></td>
        <td style="color:var(--text-light);font-size:12px;">${d.device_id}</td>
        <td>${d.model || 'TL-5000'}</td>
        <td><span class="status-tag status-${sc}">${getStatusText(sc)}</span></td>
        <td style="text-align:center;">${formatTime(d.run_time_s)}</td>
        <td style="text-align:center;">${d.run_count || 0}</td>
        <td style="text-align:center;">${avgSingle}</td>
        <td style="font-size:12px;">${feedback}</td>`;
      for (const col of OP_STAT_COLUMNS) {
        const val = od ? (Number(od[col.key]) || 0) : 0;
        const cls = val > 0 ? (['estop','photo_alarm','upper_limit','lower_limit'].includes(col.key) ? ' style="color:var(--danger);font-weight:700;text-align:center;"' : ' style="text-align:center;"') : ' style="text-align:center;color:var(--text-light);"';
        html += `<td${cls}>${val.toLocaleString()}</td>`;
      }
      html += '</tr>';
    }
    html += '</tbody></table></div></div>';
    container.innerHTML = html;
  } catch (err) {
    container.innerHTML = '<div class="empty-state">加载失败: ' + err.message + '</div>';
  }
}

async function exportStatsCSV() {
  try {
    const stats = await api('/device-ops/stats?group_by=type');
    const opMap = {};
    if (stats.by_device) {
      stats.by_device.forEach(d => { opMap[d.device_serial] = d; });
    }
    showToast('正在导出统计...', 'info');
    const headers = ['设备名称','设备编号','型号','状态','运行时间(min)','运行次数','平均单次(min)','反馈/状态', ...OP_STAT_COLUMNS.map(c => c.label)];
    const BOM = '\uFEFF';
    const csvRows = [headers.join(',')];
    for (const d of devices) {
      const avgSingle = d.run_count > 0 ? ((d.run_time_s || 0) / 60 / d.run_count).toFixed(1) : 0;
      const feedback = hasHeightFeedback(d)
        ? `${d.height_left_mm || 0}/${d.height_right_mm || 0}mm`
        : `${getStateText(d.state)}·${getDirectionText(d)}`;
      const od = opMap[d.device_id];
      const row = [
        String(d.name || d.device_id),
        String(d.device_id),
        d.model || 'TL-5000',
        getStatusText(getStatusClass(d)),
        formatTime(d.run_time_s).replace(/,/g, ''),
        d.run_count || 0,
        avgSingle,
        feedback.replace(/,/g, '，'),
        ...OP_STAT_COLUMNS.map(c => od ? (Number(od[c.key]) || 0) : 0)
      ];
      csvRows.push(row.join(','));
    }
    const csvStr = BOM + csvRows.join('\n');
    const blob = new Blob([csvStr], { type: 'text/csv;charset=utf-8;' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `运行统计_${new Date().toISOString().slice(0,10)}.csv`;
    a.click();
    URL.revokeObjectURL(url);
    showToast(`已导出 ${devices.length} 台设备的统计数据`, 'success');
  } catch (err) {
    showToast('导出失败: ' + err.message, 'error');
  }
}

/* ===== Logs Page ===== */
function renderLogs() {
  return `
    <div class="filter-bar">
      <div class="filter-group">
        <label>${t('logs.filterDevice')}</label>
        <select id="log-filter-device">
          <option value="">${t('logs.allDevices')}</option>
        </select>
      </div>
      <div class="filter-group">
        <label>设备型号</label>
        <select id="log-filter-product">
          <option value="">全部型号</option>
          <option value="screw_lift">丝杆举升机</option>
          <option value="double_post">两柱举升机</option>
          <option value="small_scissor">小剪举升机</option>
          <option value="thin_scissor">超薄小剪举升机</option>
          <option value="large_scissor">大剪举升机</option>
        </select>
      </div>
      <div class="filter-group">
        <label>操作类型</label>
        <select id="log-filter-optype">
          <option value="">全部</option>
          <option value="up">上升</option>
          <option value="down">下降</option>
          <option value="lock">锁定</option>
          <option value="refill">补油</option>
          <option value="estop">急停</option>
          <option value="photo_alarm">光电报警</option>
          <option value="rotary_switch">旋转开关切换</option>
          <option value="power_on">开机</option>
          <option value="power_off">关机</option>
        </select>
      </div>
      <div class="filter-group">
        <label>${t('logs.filterStart')}</label>
        <input type="datetime-local" id="log-filter-start">
      </div>
      <div class="filter-group">
        <label>${t('logs.filterEnd')}</label>
        <input type="datetime-local" id="log-filter-end">
      </div>
      <button class="btn btn-primary" onclick="loadLogs()">筛选</button>
      <button class="btn btn-outline" onclick="exportLogsCSV()" style="margin-left:8px;">导出CSV</button>
      <span id="log-count" style="margin-left:12px;font-size:13px;color:var(--text-muted);align-self:flex-end;"></span>
    </div>
    <div id="logs-list"></div>`;
}

// 设备端操作日志(工人物理操作:上升/下降/锁定等)
// 调用 /api/device-ops,后端已按 device_bindings 多对多绑定隔离,只返回当前用户绑定设备的日志
function operationLogClass(type) {
  if (['estop', 'photo_alarm'].includes(type)) return 'op-log-danger';
  if (['upper_limit', 'lower_limit', 'sub_upper_limit'].includes(type)) return 'op-log-warning';
  if (/^(up|down)(_|$)/.test(type || '')) return 'op-log-success';
  return 'op-log-neutral';
}

function operationRoleLabel(role) {
  return role === 'sub' ? '子机' : (role === 'main' ? '主机' : '-');
}

async function loadLogs() {
  try {
    const deviceId = document.getElementById('log-filter-device')?.value || '';
    const productType = document.getElementById('log-filter-product')?.value || '';
    const opType = document.getElementById('log-filter-optype')?.value || '';
    const startDate = document.getElementById('log-filter-start')?.value || '';
    const endDate = document.getElementById('log-filter-end')?.value || '';
    const params = new URLSearchParams();
    if (deviceId) params.set('device_serial', deviceId);
    if (productType) params.set('product_type', productType);
    if (opType) params.set('op_type', opType);
    if (startDate) params.set('start_date', startDate);
    if (endDate) params.set('end_date', endDate);

    const data = await api(`/device-ops?${params.toString()}&pageSize=100`);
    const countEl = document.getElementById('log-count');
    if (countEl) countEl.textContent = data.total ? `共 ${data.total} 条记录` : '';
    const list = document.getElementById('logs-list');
    if (!list) return;
    if (!data.list || data.list.length === 0) {
      list.innerHTML = `<div class="empty-state">${t('logs.noData')}</div>`;
      return;
    }
    list.innerHTML = `
      <table class="data-table">
        <thead><tr>
          <th>时间</th>
          <th>设备编号</th>
          <th>操作类型</th>
          <th>主/子机</th>
          <th>结果</th>
          <th>持续(ms)</th>
          <th>详情</th>
        </tr></thead>
        <tbody>${data.list.map(l => `
          <tr class="${operationLogClass(l.op_type)}">
            <td>${formatTs(l.occurred_at)}</td>
            <td>${l.device_serial || '-'}</td>
            <td><span class="op-log-tag">${l.op_type_label || l.op_type}</span></td>
            <td>${operationRoleLabel(l.role)}</td>
            <td>${l.op_result || '-'}</td>
            <td>${l.duration_ms || 0}</td>
            <td>${l.detail || '-'}</td>
          </tr>`).join('')}
        </tbody>
      </table>`;
  } catch (err) { showToast(err.message, 'error'); }
}

async function exportLogsCSV() {
  try {
    const deviceId = document.getElementById('log-filter-device')?.value || '';
    const productType = document.getElementById('log-filter-product')?.value || '';
    const opType = document.getElementById('log-filter-optype')?.value || '';
    const startDate = document.getElementById('log-filter-start')?.value || '';
    const endDate = document.getElementById('log-filter-end')?.value || '';
    const params = new URLSearchParams();
    if (deviceId) params.set('device_serial', deviceId);
    if (productType) params.set('product_type', productType);
    if (opType) params.set('op_type', opType);
    if (startDate) params.set('start_date', startDate);
    if (endDate) params.set('end_date', endDate);
    params.set('pageSize', '10000');

    showToast('正在导出...', 'info');
    const data = await api(`/device-ops?${params.toString()}`);
    if (!data.list || data.list.length === 0) {
      showToast('没有数据可导出', 'error');
      return;
    }

    const headers = ['时间','设备编号','操作类型','主/子机','结果','持续(ms)','详情'];
    const BOM = '\uFEFF';
    const csvRows = [headers.join(',')];
    for (const l of data.list) {
      csvRows.push([
        String(l.occurred_at || ''),
        String(l.device_serial || ''),
        l.op_type_label || l.op_type || '',
        operationRoleLabel(l.role),
        l.op_result || '',
        l.duration_ms || 0,
        (l.detail || '').replace(/,/g,'，').replace(/[\r\n]+/g,' ')
      ].join(','));
    }
    const csvStr = BOM + csvRows.join('\n');

    const blob = new Blob([csvStr], { type: 'text/csv;charset=utf-8;' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `操作日志_${new Date().toISOString().slice(0,10)}.csv`;
    a.click();
    URL.revokeObjectURL(url);
    showToast(`已导出 ${data.list.length} 条记录`, 'success');
  } catch (err) {
    showToast('导出失败: ' + err.message, 'error');
  }
}

async function loadLogFilterOptions() {
  try {
    const deviceList = await api('/devices');
    const deviceSelect = document.getElementById('log-filter-device');
    if (deviceSelect && Array.isArray(deviceList)) {
      deviceList.forEach(d => {
        const opt = document.createElement('option');
        opt.value = d.device_id;
        opt.textContent = d.name || d.device_id;
        deviceSelect.appendChild(opt);
      });
    }
  } catch (e) { /* ignore */ }
}

/* ===== Settings ===== */
function showUserSettings() {
  document.getElementById('settings-modal-body').innerHTML = `
    <h4 style="margin-bottom:14px;">${t('settings.changePassword')}</h4>
    <form onsubmit="return changePassword(event)">
      <div class="form-group"><label>${t('settings.oldPassword')}</label><input type="password" id="old-password" required></div>
      <div class="form-group"><label>${t('settings.newPassword')}</label><input type="password" id="new-password" required minlength="6"></div>
      <div class="form-group"><label>${t('settings.confirmPassword')}</label><input type="password" id="confirm-password" required minlength="6"></div>
      <button type="submit" class="btn btn-primary">${t('settings.changePassword')}</button>
    </form>
    ${currentUser && currentUser.role === 'admin' ? `
      <hr style="margin:20px 0;border:none;border-top:1px solid var(--border);">
      <h4 style="margin-bottom:14px;">${t('settings.userManagement')}</h4>
      <div id="user-list"><div class="loading">${t('common.loading')}</div></div>
      <button class="btn btn-primary" style="margin-top:14px;" onclick="showAddUserForm()">${t('settings.addUser')}</button>
      <div id="add-user-form" style="display:none;margin-top:14px;">
        <div class="form-group"><label>${t('settings.username')}</label><input type="text" id="new-username" required></div>
        <div class="form-group"><label>${t('settings.password')}</label><input type="password" id="new-user-pass" required minlength="6"></div>
        <div class="form-group"><label>${t('settings.role')}</label><select id="new-user-role"><option value="user">用户</option><option value="admin">管理员</option></select></div>
        <div class="form-group"><label>${t('settings.realname')}</label><input type="text" id="new-user-realname"></div>
        <div style="display:flex;gap:8px;"><button class="btn btn-primary" onclick="addUser()">${t('settings.createUser')}</button><button class="btn btn-outline" onclick="document.getElementById('add-user-form').style.display='none'">${t('common.cancel')}</button></div>
      </div>` : ''}`;
  document.getElementById('settings-modal').classList.add('active');
  if (currentUser && currentUser.role === 'admin') loadUserList();
}

async function changePassword(e) {
  e.preventDefault();
  const oldP = document.getElementById('old-password').value;
  const newP = document.getElementById('new-password').value;
  const confP = document.getElementById('confirm-password').value;
  if (newP !== confP) { showToast(t('settings.pwdMismatch'), 'error'); return false; }
  try {
    await api('/auth/password', { method: 'PUT', body: JSON.stringify({ old_password: oldP, new_password: newP }) });
    showToast(t('settings.pwdSuccess'), 'success');
    closeModal('settings-modal');
  } catch (err) { showToast(err.message, 'error'); }
  return false;
}

async function loadUserList() {
  try {
    const users = await api('/auth/users');
    const list = document.getElementById('user-list');
    if (!list) return;
    list.innerHTML = `<table style="width:100%"><thead><tr><th>${t('settings.username')}</th><th>${t('settings.role')}</th><th>${t('settings.realname')}</th><th>${t('devices.status')}</th><th>${t('settings.lastLogin')}</th><th>${t('logs.action')}</th></tr></thead><tbody>${users.map(u => `<tr>
      <td>${u.username}</td>
      <td>${u.role === 'admin' ? '管理员' : '用户'}</td>
      <td>${u.real_name || '-'}</td>
      <td>${u.enabled ? `<span class="status-tag status-normal">${t('settings.enabled')}</span>` : `<span class="status-tag status-offline">${t('settings.disabled')}</span>`}</td>
      <td>${formatTs(u.last_login)}</td>
      <td>${u.id !== currentUser.id ? `
        <button class="btn btn-sm btn-outline" onclick="toggleUser(${u.id})">${u.enabled ? t('settings.disable') : t('settings.enable')}</button>
        <button class="btn btn-sm btn-danger" onclick="deleteUser(${u.id})">${t('settings.deleteUser')}</button>` : `<span style="font-size:12px;color:var(--text-muted)">${t('settings.currentUser')}</span>`}</td>
    </tr>`).join('')}</tbody></table>`;
  } catch (err) { showToast(err.message, 'error'); }
}

function showAddUserForm() { document.getElementById('add-user-form').style.display = 'block'; }

async function addUser() {
  try {
    await api('/auth/register', {
      method: 'POST',
      body: JSON.stringify({
        username: document.getElementById('new-username').value.trim(),
        password: document.getElementById('new-user-pass').value,
        role: document.getElementById('new-user-role').value,
        real_name: document.getElementById('new-user-realname').value.trim()
      })
    });
    showToast(t('settings.userCreated'), 'success');
    document.getElementById('add-user-form').style.display = 'none';
    loadUserList();
  } catch (err) { showToast(err.message, 'error'); }
}

async function toggleUser(id) {
  try { await api('/auth/users/' + id + '/toggle', { method: 'PUT' }); loadUserList(); } catch (err) { showToast(err.message, 'error'); }
}

async function deleteUser(id) {
  if (!confirm(t('common.confirmDeleteUser'))) return;
  try { await api('/auth/users/' + id, { method: 'DELETE' }); showToast(t('common.deleted'), 'success'); loadUserList(); } catch (err) { showToast(err.message, 'error'); }
}

/* ===== Utils ===== */
function closeModal(id) { document.getElementById(id).classList.remove('active'); }

/* ===== Init ===== */
function init() {
  applyLang();
  restoreAvatar();

  setInterval(() => { const el = document.getElementById('current-time'); if (el) el.textContent = new Date().toLocaleString('zh-CN'); }, 1000);

  document.querySelectorAll('.menu-item').forEach(item => {
    item.addEventListener('click', () => {
      if (!item.dataset.page) return;
      loadPage(item.dataset.page);
    });
  });

  document.querySelectorAll('.modal').forEach(modal => {
    modal.addEventListener('click', (e) => { if (e.target === modal) modal.classList.remove('active'); });
  });

  restoreSession().catch(e => console.error('[Init] session restore failed:', e));
}

// Override loadPage for pages that need async data
const _originalLoadPage = loadPage;
loadPage = function(page) {
  currentPage = page;
  localStorage.setItem('lift_current_page', page);
  document.querySelectorAll('.menu-item').forEach(i => i.classList.toggle('active', i.dataset.page === page));

  const titles = { overview: t('overview.title'), devices: t('devices.title'), alarms: t('alarms.title'), maintenance: t('maintenance.title'), statistics: t('statistics.title'), logs: t('logs.title') };
  const breadcrumbs = { overview: t('nav.overview'), devices: t('nav.devices'), alarms: t('nav.alarms'), maintenance: t('nav.maintenance'), statistics: t('nav.statistics'), logs: t('nav.logs') };
  document.getElementById('page-title').textContent = titles[page] || '';
  document.getElementById('breadcrumb').textContent = `${t('common.home')} / ${breadcrumbs[page] || ''}`;

  if (page === 'alarms') { renderCurrentPage(); loadAlarms(); }
  else if (page === 'maintenance') { renderCurrentPage(); loadMaintenance(); }
  else if (page === 'statistics') { renderCurrentPage(); loadStatistics(); }
  else if (page === 'logs') { renderCurrentPage(); loadLogs(); loadLogFilterOptions(); }
  else { renderCurrentPage(); }
};

// Auto-refresh
setInterval(() => { if (token) fetchDevices(); }, 10000);
setInterval(() => { if (token) fetchUnackAlarms(); }, 30000);
setInterval(() => { if (token) refreshLiveUptimeDisplays(); }, 1000);

/* ===== Avatar Upload ===== */
function showAvatarUpload() {
  document.getElementById('avatar-input').click();
}

function handleAvatarUpload(event) {
  const file = event.target.files[0];
  if (!file) return;
  const reader = new FileReader();
  reader.onload = (e) => {
    const dataUrl = e.target.result;
    localStorage.setItem('lift_avatar', dataUrl);
    applyAvatar(dataUrl);
  };
  reader.readAsDataURL(file);
}

function applyAvatar(dataUrl) {
  const avatarEl = document.getElementById('user-avatar');
  if (!avatarEl) return;
  avatarEl.innerHTML = '';
  avatarEl.style.backgroundImage = `url(${dataUrl})`;
  avatarEl.style.backgroundSize = 'cover';
  avatarEl.style.backgroundPosition = 'center';
}

function restoreAvatar() {
  const saved = localStorage.getItem('lift_avatar');
  if (saved) applyAvatar(saved);
}

/* ===== AI Chat (MiMo v2.5) ===== */
let aiChatOpen = false;
let aiChatLoading = false;

function toggleAIChat() {
  aiChatOpen = !aiChatOpen;
  const panel = document.getElementById('ai-chat-panel');
  const fab = document.getElementById('ai-fab');
  panel.style.display = aiChatOpen ? 'flex' : 'none';
  if (aiChatOpen) {
    document.getElementById('ai-chat-input').focus();
  }
}

async function sendAIMessage() {
  const input = document.getElementById('ai-chat-input');
  const text = input.value.trim();
  if (!text || aiChatLoading) return;

  input.value = '';
  input.style.height = 'auto';
  const requestMessages = [...getAIChatHistory(), { role: 'user', content: text }];
  addAIChatMessage('user', text);

  aiChatLoading = true;
  const sendBtn = document.querySelector('.ai-send-btn');
  if (sendBtn) sendBtn.disabled = true;

  // Show typing indicator
  const typingId = addAITyping();

  try {
    const response = await fetch(API_BASE + '/ai/chat', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'Authorization': 'Bearer ' + token
      },
      body: JSON.stringify({
        messages: requestMessages,
        lang: currentLang
      })
    });

    removeAITyping(typingId);

    if (!response.ok) {
      const errData = await response.json().catch(() => ({}));
      throw new Error(errData.error || `${t('common.apiError')} ${response.status}`);
    }

    const data = await response.json();
    const aiText = data.choices?.[0]?.message?.content || t('common.error');
    addAIChatMessage('bot', aiText);
  } catch (err) {
    removeAITyping(typingId);
    addAIChatMessage('bot', `❌ ${err.message}`);
  } finally {
    aiChatLoading = false;
    if (sendBtn) sendBtn.disabled = false;
  }
}

const aiChatHistory = [];

function addAIChatMessage(role, text) {
  const container = document.getElementById('ai-chat-messages');
  if (!container) return;

  const div = document.createElement('div');
  div.className = `ai-msg ai-${role === 'user' ? 'user' : 'bot'}`;
  div.innerHTML = `
    <div class="ai-msg-avatar">${role === 'user' ? '👤' : '🤖'}</div>
    <div class="ai-msg-text">${role === 'bot' ? formatAIResponse(text) : escapeHTML(text)}</div>
  `;
  container.appendChild(div);
  container.scrollTop = container.scrollHeight;

  if (role === 'user' || role === 'bot') {
    aiChatHistory.push({ role, content: text });
    // Keep only last 20 messages for context
    if (aiChatHistory.length > 20) aiChatHistory.splice(0, aiChatHistory.length - 20);
  }
}

function getAIChatHistory() {
  return aiChatHistory.slice(-10).map(m => ({
    role: m.role === 'bot' ? 'assistant' : m.role,
    content: m.content
  }));
}

let aiTypingCounter = 0;
function addAITyping() {
  const container = document.getElementById('ai-chat-messages');
  if (!container) return -1;
  const id = ++aiTypingCounter;
  const div = document.createElement('div');
  div.className = 'ai-msg ai-bot';
  div.id = `ai-typing-${id}`;
  div.innerHTML = `
    <div class="ai-msg-avatar">🤖</div>
    <div class="ai-msg-text"><div class="ai-typing"><span></span><span></span><span></span></div></div>
  `;
  container.appendChild(div);
  container.scrollTop = container.scrollHeight;
  return id;
}

function removeAITyping(id) {
  if (id < 0) return;
  const el = document.getElementById(`ai-typing-${id}`);
  if (el) el.remove();
}

function formatAIResponse(text) {
  // Simple markdown-like formatting
  return escapeHTML(text)
    .replace(/\n/g, '<br>')
    .replace(/\*\*(.*?)\*\*/g, '<strong>$1</strong>')
    .replace(/`(.*?)`/g, '<code style="background:#e8f0fe;padding:1px 4px;border-radius:3px;font-size:12px;">$1</code>');
}

function escapeHTML(str) {
  const div = document.createElement('div');
  div.textContent = str;
  return div.innerHTML;
}

// Auto-resize textarea
document.addEventListener('input', (e) => {
  if (e.target.id === 'ai-chat-input') {
    e.target.style.height = 'auto';
    e.target.style.height = Math.min(e.target.scrollHeight, 80) + 'px';
  }
});

/* ===== 用户注册（Canvas图形验证码） ===== */
let captchaText = '';

function generateCaptchaText(len) {
  const chars = 'ABCDEFGHJKLMNPQRSTUVWXYZabcdefghjkmnpqrstuvwxyz23456789';
  let s = '';
  for (let i = 0; i < (len || 4); i++) s += chars[Math.floor(Math.random() * chars.length)];
  return s;
}

function drawCaptcha() {
  const canvas = document.getElementById('captcha-canvas');
  if (!canvas) return;
  const ctx = canvas.getContext('2d');
  const w = canvas.width, h = canvas.height;
  captchaText = generateCaptchaText(4);

  // 背景
  ctx.fillStyle = '#f0f4ff';
  ctx.fillRect(0, 0, w, h);

  // 干扰线
  for (let i = 0; i < 4; i++) {
    ctx.strokeStyle = `rgba(${Math.random()*100+100},${Math.random()*100+100},${Math.random()*200+55},0.4)`;
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(Math.random() * w, Math.random() * h);
    ctx.lineTo(Math.random() * w, Math.random() * h);
    ctx.stroke();
  }

  // 干扰点
  for (let i = 0; i < 30; i++) {
    ctx.fillStyle = `rgba(${Math.random()*255},${Math.random()*255},${Math.random()*255},0.3)`;
    ctx.beginPath();
    ctx.arc(Math.random() * w, Math.random() * h, 1.5, 0, Math.PI * 2);
    ctx.fill();
  }

  // 文字
  const colors = ['#2563eb', '#dc2626', '#16a34a', '#7c3aed', '#d97706'];
  for (let i = 0; i < 4; i++) {
    ctx.save();
    ctx.font = `bold ${20 + Math.random() * 6}px 'Inter', sans-serif`;
    ctx.fillStyle = colors[Math.floor(Math.random() * colors.length)];
    ctx.translate(18 + i * 26, 26 + Math.random() * 6);
    ctx.rotate((Math.random() - 0.5) * 0.4);
    ctx.fillText(captchaText[i], 0, 0);
    ctx.restore();
  }
}

function refreshCaptcha() {
  drawCaptcha();
  const input = document.getElementById('reg-captcha');
  if (input) input.value = '';
}

function showRegisterPage() {
  document.getElementById('login-page').style.display = 'none';
  document.getElementById('register-page').style.display = 'flex';
  drawCaptcha();
}

function showLoginPage() {
  document.getElementById('register-page').style.display = 'none';
  document.getElementById('login-page').style.display = 'flex';
}

function showLangDropdownReg() {
  const dd = document.getElementById('reg-lang-dropdown');
  dd.style.display = dd.style.display === 'none' ? 'block' : 'none';
}

async function handleRegister(e) {
  e.preventDefault();
  const errEl = document.getElementById('register-error');
  const btn = document.getElementById('reg-btn');
  errEl.style.display = 'none';

  const password = document.getElementById('reg-password').value;
  const password2 = document.getElementById('reg-password2').value;
  if (password !== password2) {
    errEl.textContent = t('register.passwordMismatch');
    errEl.style.display = 'block';
    refreshCaptcha();
    return false;
  }

  const captchaInput = document.getElementById('reg-captcha').value.trim();
  if (captchaInput.toLowerCase() !== captchaText.toLowerCase()) {
    errEl.textContent = t('register.captchaError');
    errEl.style.display = 'block';
    refreshCaptcha();
    return false;
  }

  btn.disabled = true;
  btn.textContent = t('register.submitting');

  try {
    const res = await fetch(API_BASE + '/auth/register-public', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        username: document.getElementById('reg-username').value.trim(),
        password: password,
        real_name: document.getElementById('reg-realname').value.trim(),
        phone: document.getElementById('reg-phone').value.trim()
      })
    });
    const data = await res.json();
    if (!res.ok) throw new Error(data.error || t('register.failed'));

    showLoginPage();
    showToast(t('register.success'), 'success');
  } catch (err) {
    errEl.textContent = err.message;
    errEl.style.display = 'block';
    refreshCaptcha();
  } finally {
    btn.disabled = false;
    btn.textContent = t('register.submit');
  }
  return false;
}

window.onerror = function(msg, src, line, col, err) {
  console.error('[GlobalError]', msg, src, line);
  return false;
};
window.addEventListener('unhandledrejection', function(e) {
  console.error('[UnhandledPromise]', e.reason);
});

window.onload = init;
