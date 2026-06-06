/* ===== 举升机物联网管理平台 - 前端核心 ===== */

const API_BASE = window.location.origin + '/api';
let token = localStorage.getItem('lift_token') || '';
let currentUser = JSON.parse(localStorage.getItem('lift_user') || 'null');
let currentPage = 'overview';
let ws = null;
let devices = [];
let unackAlarmCount = 0;

/* ===== API Helper ===== */
async function api(path, options = {}) {
  const headers = { 'Content-Type': 'application/json', ...(options.headers || {}) };
  if (token) headers['Authorization'] = 'Bearer ' + token;
  try {
    const res = await fetch(API_BASE + path, { ...options, headers });
    if (res.status === 401) { handleLogout(); throw new Error(t('auth.expired')); }
    const data = await res.json();
    if (!res.ok) throw new Error(data.error || t('common.requestFailed'));
    return data;
  } catch (e) {
    if (e.message === t('auth.expired')) handleLogout();
    throw e;
  }
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
    showApp();
  } catch (err) {
    errEl.textContent = err.message;
    errEl.style.display = 'block';
  } finally {
    btn.disabled = false;
    btn.textContent = t('login.submit');
  }
}

function handleLogout() {
  token = '';
  currentUser = null;
  localStorage.removeItem('lift_token');
  localStorage.removeItem('lift_user');
  if (ws) { ws.close(); ws = null; }
  document.getElementById('app').style.display = 'none';
  document.getElementById('login-page').style.display = 'flex';
}

function showApp() {
  document.getElementById('login-page').style.display = 'none';
  document.getElementById('app').style.display = 'flex';
  try {
    document.getElementById('user-name').textContent = currentUser.real_name || currentUser.username;
    document.getElementById('user-role').textContent = currentUser.role === 'admin' ? t('settings.roleAdmin') : currentUser.role === 'operator' ? t('settings.roleOperator') : t('settings.roleViewer');

    // Avatar: restore saved image or show initial
    const savedAvatar = localStorage.getItem('lift_avatar');
    const avatarEl = document.getElementById('user-avatar');
    if (savedAvatar) {
      applyAvatar(savedAvatar);
    } else {
      avatarEl.textContent = (currentUser.real_name || currentUser.username).charAt(0).toUpperCase();
    }

    connectWS();
    fetchDevices();
    fetchUnackAlarms();
    loadPage(currentPage);
    applyLang();
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
    ws = new WebSocket(`${wsProto}//${location.host}/ws`);
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
  const idx = devices.findIndex(d => d.device_id === deviceId);
  if (idx >= 0) {
    devices[idx] = { ...devices[idx], ...data, updated_at: new Date().toISOString() };
  } else {
    devices.push({ device_id: deviceId, ...data, updated_at: new Date().toISOString() });
  }
  if (['overview', 'devices'].includes(currentPage)) renderCurrentPage();
}

function handleCommandResponse(deviceId, data) {
  if (data.result === 'timeout') showToast((data.cmd || '') + ' → ' + t('command.timeout') + ' (' + deviceId + ')', 'error');
  else if (data.cmd === 'lock' && data.result === 'locked') showToast(t('devices.lock') + ' ✓ - ' + deviceId, 'success');
  else if (data.cmd === 'unlock' && data.result === 'unlocked') showToast(t('devices.unlock') + ' ✓ - ' + deviceId, 'success');
  else if (data.cmd === 'rename' && data.result === 'renamed') {
    showToast(deviceId + ' ' + t('devices.rename') + ' ✓', 'success');
  }
  else showToast(t('devices.query') + ': ' + (data.cmd || '') + ' → ' + (data.result || ''), 'info');
  fetchDevices();
}

/* ===== Data Fetching ===== */
async function fetchDevices() {
  try {
    devices = await api('/devices');
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
  document.getElementById('breadcrumb').textContent = breadcrumbs[page] || '';
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
  const map = { normal: t('status.normal'), offline: t('status.offline'), fault: t('status.fault'), locked: t('status.locked'), maintenance: t('status.maintenance'), idle: t('status.idle'), up: t('status.up'), down: t('status.down') };
  return map[s] || s;
}
function getStatusClass(d) { if (!d.online) return 'offline'; if (d.locked) return 'locked'; if (d.alarm && d.alarm !== 'none') return 'fault'; return 'normal'; }
function getStateText(s) { const map = { idle: t('state.idle'), up: t('state.up'), down: t('state.down'), stop: t('state.stop') }; return map[s] || s; }
function getAlarmText(a) { const map = { none: t('alarm.none'), collision: t('alarm.collision'), stall: t('alarm.stall'), balance_timeout: t('alarm.balance_timeout'), safety_bar: t('alarm.safety_bar'), overheight: t('alarm.overheight'), Emergency: t('alarm.Emergency') }; return map[a] || a; }
function formatTime(s) { if (!s) return '0h'; const h = Math.floor(s / 3600); const m = Math.floor((s % 3600) / 60); return h > 0 ? `${h}h${m}m` : `${m}m`; }
function formatTs(iso) { if (!iso) return '-'; try { return new Date(iso).toLocaleString('zh-CN'); } catch { return iso; } }

/* ===== Overview Page ===== */
function renderOverview() {
  if (devices.length === 0) return `<div class="empty-state"><div class="empty-icon">📡</div><h3>${t('overview.waiting')}}</h3><p>${t('overview.waitingSub')}}</p></div>`;
  const online = devices.filter(d => d.online && !d.locked && (d.alarm === 'none' || !d.alarm)).length;
  const offline = devices.filter(d => !d.online).length;
  const fault = devices.filter(d => d.alarm && d.alarm !== 'none').length;
  const locked = devices.filter(d => d.locked).length;
  const totalRuns = devices.reduce((s, d) => s + (d.run_count || 0), 0);
  const totalRunTime = devices.reduce((s, d) => s + (d.run_time_s || 0), 0);

  return `
    <div class="cards-grid">
      <div class="card online"><div class="card-title">${t('overview.online')}</div><div class="card-value">${online}</div><div class="card-subtitle">/${devices.length}台</div></div>
      <div class="card offline"><div class="card-title">${t('overview.offline')}</div><div class="card-value">${offline}</div></div>
      <div class="card fault"><div class="card-title">${t('overview.fault')}</div><div class="card-value">${fault}</div></div>
      <div class="card locked"><div class="card-title">已锁机</div><div class="card-value">${locked}</div></div>
      <div class="card"><div class="card-title">${t('overview.totalRuns')}</div><div class="card-value">${totalRuns.toLocaleString()}</div></div>
      <div class="card"><div class="card-title">${t('overview.totalTime')}</div><div class="card-value">${formatTime(totalRunTime)}</div></div>
    </div>
    <div class="device-cards">${devices.slice(0, 8).map(d => renderDeviceCard(d)).join('')}</div>`;
}

function renderDeviceCard(d) {
  const sc = getStatusClass(d);
  const maxH = 2000;
  const leftPct = Math.min((d.height_left_mm || 0) / maxH * 100, 100);
  const rightPct = Math.min((d.height_right_mm || 0) / maxH * 100, 100);
  return `
    <div class="device-card" onclick="showDeviceDetail('${d.device_id}')">
      <div class="device-card-header">
        <div><div class="device-card-name">${d.name || d.device_id}</div><div class="device-card-id">${d.device_id}${d.group ? ' · ' + d.group : ''}</div></div>
        <span class="status-tag status-${sc}">${getStatusText(sc)}</span>
      </div>
      <div class="device-card-grid">
        <div class="device-card-metric"><div class="device-card-metric-label">左高度</div><div class="device-card-metric-value">${d.height_left_mm || 0}<small>mm</small></div></div>
        <div class="device-card-metric"><div class="device-card-metric-label">右高度</div><div class="device-card-metric-value">${d.height_right_mm || 0}<small>mm</small></div></div>
        <div class="device-card-metric"><div class="device-card-metric-label">偏差</div><div class="device-card-metric-value">${d.height_diff_mm || 0}<small>mm</small></div></div>
        <div class="device-card-metric"><div class="device-card-metric-label">运行次数</div><div class="device-card-metric-value">${d.run_count || 0}</div></div>
      </div>
      <div style="margin-top:8px;">
        <div style="font-size:11px;color:var(--text-muted);margin-bottom:4px;">左高度</div>
        <div class="height-bar"><div class="height-bar-fill" style="width:${leftPct}%;background:var(--primary);"></div></div>
        <div style="font-size:11px;color:var(--text-muted);margin:6px 0 4px;">右高度</div>
        <div class="height-bar"><div class="height-bar-fill" style="width:${rightPct}%;background:var(--success);"></div></div>
      </div>
      ${d.alarm && d.alarm !== 'none' ? `<div style="margin-top:8px;color:var(--danger);font-size:12px;font-weight:500;">⚠ ${getAlarmText(d.alarm)}</div>` : ''}
    </div>`;
}

/* ===== Devices Page ===== */
function renderDevices() {
  const isAdmin = currentUser && currentUser.role === 'admin';
  const isOperator = currentUser && (currentUser.role === 'admin' || currentUser.role === 'operator');
  const groups = [...new Set(devices.map(d => d.group || t('devices.defaultGroup')))];

  return `
    <div class="filter-bar">
      <div class="tab-group">
        <button class="tab-btn active" onclick="filterDeviceView('all', this)">全部(${devices.length})</button>
        ${groups.map(g => `<button class="tab-btn" onclick="filterDeviceView('${g}', this)">${g}</button>`).join('')}
      </div>
      ${isAdmin ? `<button class="btn btn-primary" onclick="showAddDeviceModal()">+ 添加设备</button>` : ''}
    </div>
    ${devices.length === 0 ? `<div class="empty-state"><div class="empty-icon">📡</div><h3>${t('devices.noDevices')}</h3><p>${t('devices.noDevicesSub')}</p></div>` :
    `<div class="device-cards" id="device-cards-container">${devices.map(d => renderDeviceCardWithActions(d, isOperator)).join('')}</div>`}`;
}

function renderDeviceCardWithActions(d, canControl) {
  const sc = getStatusClass(d);
  const maxH = 2000;
  const leftPct = Math.min((d.height_left_mm || 0) / maxH * 100, 100);
  const rightPct = Math.min((d.height_right_mm || 0) / maxH * 100, 100);
  return `
    <div class="device-card">
      <div class="device-card-header">
        <div><div class="device-card-name" style="cursor:pointer" onclick="showDeviceDetail('${d.device_id}')">${d.name || d.device_id}</div><div class="device-card-id">${d.device_id}${d.group ? ' · ' + d.group : ''}</div></div>
        <span class="status-tag status-${sc}">${getStatusText(sc)}</span>
      </div>
      <div class="device-card-grid">
        <div class="device-card-metric"><div class="device-card-metric-label">状态</div><div class="device-card-metric-value">${getStateText(d.state)}</div></div>
        <div class="device-card-metric"><div class="device-card-metric-label">报警</div><div class="device-card-metric-value" style="${d.alarm && d.alarm !== 'none' ? 'color:var(--danger)' : ''}">${getAlarmText(d.alarm)}</div></div>
        <div class="device-card-metric"><div class="device-card-metric-label">运行时长</div><div class="device-card-metric-value">${formatTime(d.run_time_s)}</div></div>
        <div class="device-card-metric"><div class="device-card-metric-label">运行次数</div><div class="device-card-metric-value">${d.run_count || 0}</div></div>
      </div>
      <div style="margin-top:8px;">
        <div style="font-size:11px;color:var(--text-muted);margin-bottom:4px;">左高度: ${d.height_left_mm || 0}mm</div>
        <div class="height-bar"><div class="height-bar-fill" style="width:${leftPct}%;background:var(--primary);"></div></div>
        <div style="font-size:11px;color:var(--text-muted);margin:6px 0 4px;">右高度: ${d.height_right_mm || 0}mm</div>
        <div class="height-bar"><div class="height-bar-fill" style="width:${rightPct}%;background:var(--success);"></div></div>
      </div>
      <div class="device-card-actions">
        <button class="btn btn-sm btn-outline" onclick="showDeviceDetail('${d.device_id}')">详情</button>
        ${canControl ? `${d.locked
          ? `<button class="btn btn-sm btn-success" onclick="unlockDevice('${d.device_id}')">解锁</button>`
          : `<button class="btn btn-sm btn-danger" onclick="lockDevice('${d.device_id}')">锁机</button>`
        }` : ''}
        ${canControl ? `<button class="btn btn-sm btn-outline" onclick="queryDevice('${d.device_id}')">查询</button>` : ''}
      </div>
    </div>`;
}

function filterDeviceView(group, btn) {
  document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
  btn.classList.add('active');
  const filtered = group === 'all' ? devices : devices.filter(d => (d.group || t('devices.defaultGroup')) === group);
  const isOperator = currentUser && (currentUser.role === 'admin' || currentUser.role === 'operator');
  const container = document.getElementById('device-cards-container');
  if (container) container.innerHTML = filtered.map(d => renderDeviceCardWithActions(d, isOperator)).join('');
}

/* ===== Device Detail Modal ===== */
function showDeviceDetail(deviceId) {
  const d = devices.find(x => x.device_id === deviceId);
  if (!d) { showToast(t('common.error'), 'error'); return; }
  const sc = getStatusClass(d);
  const isAdmin = currentUser && currentUser.role === 'admin';
  const isOperator = currentUser && (currentUser.role === 'admin' || currentUser.role === 'operator');
  const maxH = 2000;
  const leftPct = Math.min((d.height_left_mm || 0) / maxH * 100, 100);
  const rightPct = Math.min((d.height_right_mm || 0) / maxH * 100, 100);

  document.getElementById('device-modal-title').textContent = d.name || d.device_id;
  document.getElementById('device-modal-body').innerHTML = `
    <div style="display:grid;grid-template-columns:1fr 1fr;gap:20px;">
      <div>
        <div class="detail-row"><div class="detail-label">${t('devices.deviceId')}</div><div class="detail-value">${d.device_id}</div></div>
        <div class="detail-row"><div class="detail-label">${t('devices.deviceName')}}</div><div class="detail-value">${d.name || '-'}${isAdmin ? ` <button class="btn btn-sm btn-outline" style="margin-left:6px;font-size:11px;padding:2px 8px;" onclick="renameDevice('${d.device_id}', '${(d.name||'').replace(/'/g,"\\'")}')">✏️</button>` : ''}</div></div>
        <div class="detail-row"><div class="detail-label">${t('devices.model')}</div><div class="detail-value">${d.model || 'TL-5000'}</div></div>
        <div class="detail-row"><div class="detail-label">${t('devices.group')}</div><div class="detail-value">${d.group || t('devices.defaultGroup')}</div></div>
        <div class="detail-row"><div class="detail-label">${t('devices.status')}</div><div class="detail-value"><span class="status-tag status-${sc}">${getStatusText(sc)}</span></div></div>
        <div class="detail-row"><div class="detail-label">${t('common.runInfo')}</div><div class="detail-value">${getStateText(d.state)}</div></div>
        <div class="detail-row"><div class="detail-label">${t('common.lockInfo')}}</div><div class="detail-value">${d.locked ? `<span class="status-tag status-locked">${t('devices.lockStatusYN')}}</span>` : `<span class="status-tag status-normal">${t('devices.lockStatusNormal')}}</span>`}</div></div>
        <div class="detail-row"><div class="detail-label">${t('devices.alarm')}</div><div class="detail-value" style="${d.alarm && d.alarm !== 'none' ? 'color:var(--danger);font-weight:600' : ''}">${getAlarmText(d.alarm)}</div></div>
      </div>
      <div>
        <div style="margin-bottom:16px;">
          <div style="font-size:13px;font-weight:600;margin-bottom:8px;">${t('common.heightData')}</div>
          <div style="font-size:12px;color:var(--text-muted);margin-bottom:4px;">${t('devices.leftHeight')}: ${d.height_left_mm || 0}mm</div>
          <div class="height-bar" style="height:10px;"><div class="height-bar-fill" style="width:${leftPct}%;background:var(--primary);"></div></div>
          <div style="font-size:12px;color:var(--text-muted);margin:8px 0 4px;">${t('devices.rightHeight')}: ${d.height_right_mm || 0}mm</div>
          <div class="height-bar" style="height:10px;"><div class="height-bar-fill" style="width:${rightPct}%;background:var(--success);"></div></div>
          <div style="font-size:12px;color:var(--text-muted);margin-top:8px;">${t('devices.diff')}: <b>${d.height_diff_mm || 0}mm</b></div>
        </div>
        <div class="detail-row"><div class="detail-label">${t('devices.runTime')}</div><div class="detail-value">${formatTime(d.run_time_s)}</div></div>
        <div class="detail-row"><div class="detail-label">${t('devices.runCount')}</div><div class="detail-value">${d.run_count || 0}</div></div>
        <div class="detail-row"><div class="detail-label">${t('common.lastUpdate')}</div><div class="detail-value">${formatTs(d.updated_at)}</div></div>
      </div>
    </div>
    <div style="margin-top:20px;display:flex;gap:8px;">
      ${isOperator ? `
        ${d.locked ? `<button class="btn btn-success" onclick="unlockDevice('${d.device_id}');closeModal('device-modal')">${t('devices.unlock')}}</button>` : `<button class="btn btn-danger" onclick="lockDevice('${d.device_id}');closeModal('device-modal')">${t('devices.lock')}}</button>`}
        <button class="btn btn-outline" onclick="queryDevice('${d.device_id}')">${t('devices.query')}</button>
      ` : ''}
      <button class="btn btn-outline" onclick="closeModal('device-modal')">${t('common.close')}</button>
    </div>`;
  document.getElementById('device-modal').classList.add('active');
}

async function showAddDeviceModal() {
  document.getElementById('maintenance-modal-title').textContent = t('devices.addDevice');
  document.getElementById('maintenance-modal-body').innerHTML = `
    <form onsubmit="return addDevice(event)">
      <div class="form-group"><label>设备ID</label><input type="text" id="add-device-id" placeholder="如: lift_004" required></div>
      <div class="form-group"><label>设备名称</label><input type="text" id="add-device-name" placeholder="如: 举升机4号" required></div>
      <div class="form-group"><label>设备型号</label><input type="text" id="add-device-model" value="TL-5000"></div>
      <div class="form-group"><label>所属区域</label><input type="text" id="add-device-group" value="默认分组"></div>
      <div style="display:flex;gap:8px;margin-top:16px;"><button type="submit" class="btn btn-primary">添加</button><button type="button" class="btn btn-outline" onclick="closeModal('maintenance-modal')">取消</button></div>
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
        model: document.getElementById('add-device-model').value.trim(),
        group_name: document.getElementById('add-device-group').value.trim()
      })
    });
    showToast(t('devices.addSuccess'), 'success');
    closeModal('maintenance-modal');
    fetchDevices();
  } catch (err) { showToast(err.message, 'error'); }
  return false;
}

/* ===== Commands ===== */
async function lockDevice(id) {
  try {
    await api('/commands/lock/' + id, { method: 'POST' });
    showToast(t('devices.lockSent'), 'success');
    fetchDevices();
  } catch (err) { showToast(err.message, 'error'); }
}

async function unlockDevice(id) {
  try {
    await api('/commands/unlock/' + id, { method: 'POST' });
    showToast(t('devices.unlockSent'), 'success');
    fetchDevices();
  } catch (err) { showToast(err.message, 'error'); }
}

async function queryDevice(id) {
  try {
    await api('/commands/query/' + id, { method: 'POST' });
    showToast(t('devices.query') + ' ✓', 'info');
  } catch (err) { showToast(err.message, 'error'); }
}

async function renameDevice(deviceId, currentName) {
  const newName = prompt(t('devices.renameTitle'), currentName);
  if (!newName || newName.trim() === '' || newName.trim() === currentName) return;
  try {
    await api('/commands/rename/' + deviceId, {
      method: 'POST',
      body: JSON.stringify({ name: newName.trim() })
    });
    showToast(t('devices.renameSuccess'), 'success');
    const d = devices.find(x => x.device_id === deviceId);
    if (d) d.name = newName.trim();
    renderCurrentPage();
  } catch (err) { showToast(err.message, 'error'); }
}

/* ===== Alarms Page ===== */
function renderAlarms() {
  return `
    <div class="filter-bar">
      <div class="form-group" style="margin-bottom:0">
        <select id="alarm-filter-ack" onchange="loadAlarms()">
          <option value="">${t('alarms.all')}</option>
          <option value="unack">未确认报警</option>
          <option value="ack">${t('alarms.acked')}</option>
        </select>
      </div>
      <button class="btn btn-outline" onclick="loadAlarms()">刷新</button>
    </div>
    <div id="alarms-list"><div class="loading">加载中...</div></div>`;
}

async function loadAlarms() {
  try {
    const alarms = await api('/alarms');
    const filter = document.getElementById('alarm-filter-ack')?.value || '';
    let filtered = alarms;
    if (filter === 'unack') filtered = alarms.filter(a => !a.acknowledged);
    else if (filter === 'ack') filtered = alarms.filter(a => a.acknowledged);

    const list = document.getElementById('alarms-list');
    if (!list) return;

    if (filtered.length === 0) {
      list.innerHTML = `<div class="empty-state"><div class="empty-icon">🔔</div><h3>${t('alarms.noAlarms')}}</h3><p>${t('alarms.allNormal')}}</p></div>`;
      return;
    }

    list.innerHTML = filtered.map(a => `
      <div class="alarm-card ${a.level || 'warning'}">
        <div class="alarm-icon">${a.level === 'danger' || a.alarm_type === 'collision' ? '🔴' : '🟡'}</div>
        <div class="alarm-info">
          <div class="alarm-device">${a.device_name || a.device_id}</div>
          <div class="alarm-message">${getAlarmText(a.alarm_type)}: ${a.message || ''}</div>
          <div class="alarm-time">${formatTs(a.created_at)}</div>
        </div>
        <div class="alarm-actions">
          ${!a.acknowledged ? `<button class="btn btn-sm btn-outline" onclick="ackAlarm(${a.id})">确认</button>` : ''}
          ${!a.resolved_at ? `<button class="btn btn-sm btn-success" onclick="resolveAlarm(${a.id})">解除</button>` : '<span style="font-size:12px;color:var(--success);">已解除</span>'}
        </div>
      </div>`).join('');
  } catch (err) { showToast(err.message, 'error'); }
}

async function ackAlarm(id) {
  try { await api(`/alarms/${id}/acknowledge`, { method: 'PUT' }); showToast(t('alarms.ackSuccess'), 'success'); fetchUnackAlarms(); loadAlarms(); } catch (err) { showToast(err.message, 'error'); }
}

async function resolveAlarm(id) {
  try { await api(`/alarms/${id}/resolve`, { method: 'PUT' }); showToast(t('alarms.resolveSuccess'), 'success'); fetchUnackAlarms(); loadAlarms(); fetchDevices(); } catch (err) { showToast(err.message, 'error'); }
}

/* ===== Maintenance Page ===== */
function renderMaintenance() {
  return `
    <div class="filter-bar">
      <div class="form-group" style="margin-bottom:0"><select id="mt-device-filter"><option value="">${t('common.allDevices')}</option>${devices.map(d => `<option value="${d.device_id}">${d.name || d.device_id}</option>`).join('')}</select></div>
      <div class="form-group" style="margin-bottom:0"><select id="mt-type-filter"><option value="">${t('common.allTypes')}</option><option value="保养">保养</option><option value="维修">维修</option></select></div>
      <button class="btn btn-primary" onclick="loadMaintenance()">查询</button>
      <button class="btn btn-outline" onclick="showAddMaintenanceModal()">+ 添加记录</button>
      <button class="btn btn-outline" onclick="exportMaintenance()">导出CSV</button>
    </div>
    <div id="maintenance-list"><div class="loading">加载中...</div></div>`;
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
    if (records.length === 0) { list.innerHTML = `<div class="empty-state"><div class="empty-icon">🔧</div><h3>${t('maintenance.noRecords')}}</h3></div>`; return; }
    list.innerHTML = `<div class="device-table"><div class="table-header"><span>${t('maintenance.records')} (${records.length}条)</span></div><table><thead><tr><th>设备</th><th>类型</th><th>描述</th><th>处理人</th><th>结果</th><th>下次保养</th><th>费用</th><th>记录时间</th><th>操作</th></tr></thead><tbody>${records.map(r => `<tr>
      <td>${r.device_name || r.device_id}</td>
      <td><span class="status-tag ${r.type === '保养' ? 'status-maintenance' : 'status-fault'}">${r.type === '保养' ? t('maintenance.serviceType') : t('maintenance.repairType')}</span></td>
      <td>${r.description || '-'}</td>
      <td>${r.handler || '-'}</td>
      <td>${r.result || '-'}</td>
      <td>${r.next_date || '-'}</td>
      <td>${r.cost || '-'}</td>
      <td>${formatTs(r.created_at)}</td>
      <td>${currentUser && currentUser.role === 'admin' ? `<button class="btn btn-sm btn-danger" onclick="deleteMaintenance(${r.id})">删除</button>` : ''}</td>
    </tr>`).join('')}</tbody></table></div>`;
  } catch (err) { showToast(err.message, 'error'); }
}

function showAddMaintenanceModal() {
  document.getElementById('maintenance-modal-title').textContent = t('maintenance.add');
  document.getElementById('maintenance-modal-body').innerHTML = `
    <form onsubmit="return submitMaintenance(event)">
      <div class="form-group"><label>设备</label><select id="mt-device" required>${devices.map(d => `<option value="${d.device_id}">${d.name || d.device_id}</option>`).join('')}</select></div>
      <div class="form-group"><label>类型</label><select id="mt-type"><option value="保养">保养</option><option value="维修">维修</option></select></div>
      <div class="form-group"><label>描述</label><textarea id="mt-desc" rows="2" placeholder="${t('maintenance.descPh')}"></textarea></div>
      <div class="form-group"><label>处理人</label><input type="text" id="mt-handler" placeholder="${t('maintenance.handlerPh')}"></div>
      <div class="form-group"><label>处理结果</label><input type="text" id="mt-result" value="${t('maintenance.inProgress')}"></div>
      <div class="form-group"><label>${t('maintenance.nextDate')}</label><input type="date" id="mt-next-date"></div>
      <div class="form-group"><label>费用(元)</label><input type="number" id="mt-cost" step="0.01" value="0"></div>
      <div style="display:flex;gap:8px;margin-top:16px;"><button type="submit" class="btn btn-primary">提交</button><button type="button" class="btn btn-outline" onclick="closeModal('maintenance-modal')">取消</button></div>
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
function renderStatistics() {
  if (devices.length === 0) return `<div class="empty-state"><div class="empty-icon">📊</div><h3>${t('statistics.noData')}</h3><p>${t('statistics.waitingData')}</p></div>`;
  const totalRunTime = devices.reduce((s, d) => s + (d.run_time_s || 0), 0);
  const totalRunCount = devices.reduce((s, d) => s + (d.run_count || 0), 0);
  const avgTime = devices.length > 0 ? (totalRunTime / devices.length) : 0;
  const avgCount = devices.length > 0 ? Math.round(totalRunCount / devices.length) : 0;

  return `
    <div class="stats-grid">
      <div class="stat-card"><h4>累计运行时长</h4><div><span class="value">${formatTime(totalRunTime)}</span></div></div>
      <div class="stat-card"><h4>累计运行次数</h4><div><span class="value">${totalRunCount.toLocaleString()}</span><span class="unit">次</span></div></div>
      <div class="stat-card"><h4>平均运行时长</h4><div><span class="value">${formatTime(avgTime)}</span><span class="unit">/台</span></div></div>
      <div class="stat-card"><h4>平均运行次数</h4><div><span class="value">${avgCount}</span><span class="unit">次/台</span></div></div>
    </div>
    <div class="device-table" style="margin-top:20px;">
      <div class="table-header"><span>${t('statistics.deviceDetail')}</span></div>
      <table><thead><tr><th>设备名称</th><th>型号</th><th>状态</th><th>运行时长</th><th>运行次数</th><th>左高度(mm)</th><th>右高度(mm)</th><th>偏差(mm)</th><th>平均单次(min)</th></tr></thead>
      <tbody>${devices.map(d => {
        const avgSingle = d.run_count > 0 ? ((d.run_time_s || 0) / 60 / d.run_count).toFixed(1) : 0;
        return `<tr>
          <td><span class="device-name" onclick="showDeviceDetail('${d.device_id}')">${d.name || d.device_id}</span></td>
          <td>${d.model || 'TL-5000'}</td>
          <td><span class="status-tag status-${getStatusClass(d)}">${getStatusText(getStatusClass(d))}</span></td>
          <td>${formatTime(d.run_time_s)}</td>
          <td>${d.run_count || 0}</td>
          <td>${d.height_left_mm || 0}</td>
          <td>${d.height_right_mm || 0}</td>
          <td>${d.height_diff_mm || 0}</td>
          <td>${avgSingle}</td></tr>`;
      }).join('')}</tbody></table>
    </div>`;
}

/* ===== Logs Page ===== */
function renderLogs() {
  return `
    <div class="filter-bar">
      <div class="form-group" style="margin-bottom:0"><select id="log-device-filter"><option value="">${t('common.allDevices')}</option>${devices.map(d => `<option value="${d.device_id}">${d.name || d.device_id}</option>`).join('')}</select></div>
      <div class="form-group" style="margin-bottom:0"><input type="date" id="log-start-date"></div>
      <div class="form-group" style="margin-bottom:0"><input type="date" id="log-end-date"></div>
      <button class="btn btn-primary" onclick="loadLogs()">查询</button>
    </div>
    <div id="logs-list"><div class="loading">加载中...</div></div>`;
}

async function loadLogs() {
  try {
    const params = [];
    const dev = document.getElementById('log-device-filter')?.value;
    const start = document.getElementById('log-start-date')?.value;
    const end = document.getElementById('log-end-date')?.value;
    if (dev) params.push('device_id=' + dev);
    if (start) params.push('start_date=' + start);
    if (end) params.push('end_date=' + end);
    const data = await api('/logs?' + params.join('&'));
    const list = document.getElementById('logs-list');
    if (!list) return;
    if (data.logs.length === 0) { list.innerHTML = `<div class="empty-state"><div class="empty-icon">📋</div><h3>${t('logs.noLogs')}}</h3></div>`; return; }
    list.innerHTML = `<div class="device-table"><div class="table-header"><span>操作日志 (${data.total}条)</span></div><table><thead><tr><th>操作人</th><th>操作</th><th>设备</th><th>详情</th><th>结果</th><th>时间</th></tr></thead><tbody>${data.logs.map(l => `<tr>
      <td>${l.real_name || l.username || '-'}</td>
      <td>${l.action}</td>
      <td>${l.device_id || '-'}</td>
      <td>${l.detail || '-'}</td>
      <td>${l.result || '-'}</td>
      <td>${formatTs(l.created_at)}</td></tr>`).join('')}</tbody></table></div>`;
  } catch (err) { showToast(err.message, 'error'); }
}

/* ===== Settings ===== */
function showUserSettings() {
  document.getElementById('settings-modal-body').innerHTML = `
    <h4 style="margin-bottom:14px;">修改密码</h4>
    <form onsubmit="return changePassword(event)">
      <div class="form-group"><label>旧密码</label><input type="password" id="old-password" required></div>
      <div class="form-group"><label>新密码</label><input type="password" id="new-password" required minlength="6"></div>
      <div class="form-group"><label>确认新密码</label><input type="password" id="confirm-password" required minlength="6"></div>
      <button type="submit" class="btn btn-primary">修改密码</button>
    </form>
    ${currentUser && currentUser.role === 'admin' ? `
      <hr style="margin:20px 0;border:none;border-top:1px solid var(--border);">
      <h4 style="margin-bottom:14px;">用户管理</h4>
      <div id="user-list"><div class="loading">加载中...</div></div>
      <button class="btn btn-primary" style="margin-top:14px;" onclick="showAddUserForm()">添加用户</button>
      <div id="add-user-form" style="display:none;margin-top:14px;">
        <div class="form-group"><label>用户名</label><input type="text" id="new-username" required></div>
        <div class="form-group"><label>密码</label><input type="password" id="new-user-pass" required minlength="6"></div>
        <div class="form-group"><label>角色</label><select id="new-user-role"><option value="operator">操作员</option><option value="viewer">观察员</option><option value="admin">管理员</option></select></div>
        <div class="form-group"><label>姓名</label><input type="text" id="new-user-realname"></div>
        <div style="display:flex;gap:8px;"><button class="btn btn-primary" onclick="addUser()">创建</button><button class="btn btn-outline" onclick="document.getElementById('add-user-form').style.display='none'">取消</button></div>
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
    list.innerHTML = `<table style="width:100%"><thead><tr><th>用户名</th><th>角色</th><th>姓名</th><th>状态</th><th>最后登录</th><th>操作</th></tr></thead><tbody>${users.map(u => `<tr>
      <td>${u.username}</td>
      <td>${u.role === 'admin' ? t('settings.roleAdmin') : u.role === 'operator' ? t('settings.roleOperator') : t('settings.roleViewer')}</td>
      <td>${u.real_name || '-'}</td>
      <td>${u.enabled ? '<span class="status-tag status-normal">启用</span>' : '<span class="status-tag status-offline">禁用</span>'}</td>
      <td>${formatTs(u.last_login)}</td>
      <td>${u.id !== currentUser.id ? `
        <button class="btn btn-sm btn-outline" onclick="toggleUser(${u.id})">${u.enabled ? t('settings.disable') : t('settings.enable')}</button>
        <button class="btn btn-sm btn-danger" onclick="deleteUser(${u.id})">删除</button>` : '<span style="font-size:12px;color:var(--text-muted)">当前用户</span>'}</td>
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

  if (token && currentUser) {
    try {
      showApp();
    } catch (e) {
      console.error('[Init] showApp failed:', e);
      document.getElementById('login-page').style.display = 'flex';
    }
  } else {
    document.getElementById('login-page').style.display = 'flex';
  }
}

// Override loadPage for pages that need async data
const _originalLoadPage = loadPage;
loadPage = function(page) {
  currentPage = page;
  document.querySelectorAll('.menu-item').forEach(i => i.classList.toggle('active', i.dataset.page === page));

  const titles = { overview: t('overview.title'), devices: t('devices.title'), alarms: t('alarms.title'), maintenance: t('maintenance.title'), statistics: t('statistics.title'), logs: t('logs.title') };
  const breadcrumbs = { overview: t('nav.overview'), devices: t('nav.devices'), alarms: t('nav.alarms'), maintenance: t('nav.maintenance'), statistics: t('nav.statistics'), logs: t('nav.logs') };
  document.getElementById('page-title').textContent = titles[page] || '';
  document.getElementById('breadcrumb').textContent = breadcrumbs[page] || '';

  if (page === 'alarms') { renderCurrentPage(); loadAlarms(); }
  else if (page === 'maintenance') { renderCurrentPage(); loadMaintenance(); }
  else if (page === 'logs') { renderCurrentPage(); loadLogs(); }
  else { renderCurrentPage(); }
};

// Auto-refresh
setInterval(() => { if (token) fetchDevices(); }, 10000);
setInterval(() => { if (token) fetchUnackAlarms(); }, 30000);

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
        messages: [...getAIChatHistory(), { role: 'user', content: text }]
      })
    });

    removeAITyping(typingId);

    if (!response.ok) {
      const errData = await response.json().catch(() => ({}));
      throw new Error(errData.error || `API错误 ${response.status}`);
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
  return aiChatHistory.slice(-10).map(m => ({ role: m.role, content: m.content }));
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

window.onerror = function(msg, src, line, col, err) {
  console.error('[GlobalError]', msg, src, line);
  return false;
};
window.addEventListener('unhandledrejection', function(e) {
  console.error('[UnhandledPromise]', e.reason);
});

window.onload = init;
