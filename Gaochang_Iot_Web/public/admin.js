/* ============================================================
 * 高昌举升机 IoT 管理后台 - 前端逻辑
 * ============================================================ */

// 当前登录用户信息
let currentUser = null;
// 各模块分页状态
let registryPage = 1;
let registryTotal = 0;
let registryPageSize = 20;
let bindingsPage = 1;
let bindingsTotal = 0;
let bindingsPageSize = 20;
let logsPage = 1;
let logsTotal = 0;
let logsPageSize = 20;
// 设备端操作日志分页
let deviceOpsPage = 1;
let deviceOpsTotal = 0;
let deviceOpsPageSize = 20;
const shippingResetSelected = new Set();
const shippingResetState = new Map();
// 各模块是否已首次加载
const loaded = { dashboard: false, registry: false, bindings: false, shipping_reset: false, users: false, logs: false, device_ops: false };

/* ============ 通用工具 ============ */

function getToken() {
    // 与前台 app.js 共用同一个 token key,实现登录态互通
    return localStorage.getItem('lift_token') || localStorage.getItem('admin_token') || localStorage.getItem('token') || '';
}

function setToken(token) {
    localStorage.setItem('lift_token', token);
    localStorage.setItem('admin_token', token);
}

function clearToken() {
    localStorage.removeItem('lift_token');
    localStorage.removeItem('admin_token');
    localStorage.removeItem('admin_user');
    localStorage.removeItem('lift_user');
}

/**
 * 封装的 API 请求方法
 * - 自动带 Authorization: Bearer token
 * - 401 自动跳转登录
 */
async function api(url, options = {}) {
    const token = getToken();
    const headers = { 'Content-Type': 'application/json', ...(options.headers || {}) };
    if (token) {
        headers['Authorization'] = 'Bearer ' + token;
    }
    try {
        const resp = await fetch(url, { ...options, headers });
        // 如果返回 CSV 等非 JSON
        const contentType = resp.headers.get('Content-Type') || '';
        if (!contentType.includes('application/json')) {
            if (!resp.ok) {
                throw new Error('请求失败: ' + resp.status);
            }
            return resp;
        }
        const data = await resp.json();
        if (resp.status === 401) {
            toast('登录已过期，请重新登录', 'error');
            clearToken();
            showLoginOverlay();
            throw new Error('未登录');
        }
        if (!resp.ok) {
            const msg = data.error || data.message || ('请求失败: ' + resp.status);
            throw new Error(msg);
        }
        return data;
    } catch (err) {
        if (err.message === '未登录' || err.message === 'Failed to fetch') {
            // 网络错误或未登录，不重复处理
        }
        throw err;
    }
}

function toast(message, type = 'info', duration = 2500) {
    const container = document.getElementById('toast-container');
    const div = document.createElement('div');
    div.className = 'toast ' + type;
    div.textContent = message;
    container.appendChild(div);
    setTimeout(() => {
        div.style.opacity = '0';
        div.style.transition = 'opacity 0.3s';
        setTimeout(() => div.remove(), 300);
    }, duration);
}

function openModal(id) {
    document.getElementById(id).classList.add('show');
}
function closeModal(id) {
    document.getElementById(id).classList.remove('show');
}

function escapeHtml(s) {
    if (s === null || s === undefined) return '';
    return String(s).replace(/[&<>"']/g, (c) => ({
        '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'
    }[c]));
}

function showConfirm(message, onOk, title) {
    document.getElementById('confirm-title').textContent = title || '确认';
    document.getElementById('confirm-message').textContent = message;
    const btn = document.getElementById('confirm-ok-btn');
    // 替换克隆以清除之前的事件监听
    const newBtn = btn.cloneNode(true);
    btn.parentNode.replaceChild(newBtn, btn);
    newBtn.addEventListener('click', () => {
        closeModal('confirm-modal');
        try { onOk && onOk(); } catch (e) { console.error(e); }
    });
    openModal('confirm-modal');
}

/* ============ 登录/退出 ============ */

function showLoginOverlay() {
    document.getElementById('login-overlay').style.display = 'flex';
    const inp = document.getElementById('admin-login-username');
    if (inp) setTimeout(() => inp.focus(), 100);
}

function hideLoginOverlay() {
    document.getElementById('login-overlay').style.display = 'none';
}

async function handleAdminLogin(e) {
    e.preventDefault();
    const username = document.getElementById('admin-login-username').value.trim();
    const password = document.getElementById('admin-login-password').value;
    const errBox = document.getElementById('admin-login-error');
    const btn = document.getElementById('admin-login-btn');
    if (!username || !password) {
        errBox.textContent = '请输入用户名和密码';
        errBox.style.display = 'block';
        return false;
    }
    btn.disabled = true;
    btn.textContent = '登录中...';
    errBox.style.display = 'none';
    try {
        const resp = await fetch('/api/auth/login', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ username, password })
        });
        const data = await resp.json();
        if (!resp.ok) {
            errBox.textContent = data.error || ('登录失败: ' + resp.status);
            errBox.style.display = 'block';
            btn.disabled = false;
            btn.textContent = '登录';
            return false;
        }
        // 角色校验:仅 admin 可进入管理后台
        if (data.user.role !== 'admin') {
            errBox.textContent = '该账号无管理员权限,请使用管理员账号登录';
            errBox.style.display = 'block';
            btn.disabled = false;
            btn.textContent = '登录';
            return false;
        }
        setToken(data.token);
        currentUser = data.user;
        localStorage.setItem('admin_user', JSON.stringify(data.user));
        hideLoginOverlay();
        updateTopbar();
        loadSection('dashboard');
        loaded.dashboard = true;
    } catch (err) {
        errBox.textContent = '网络错误: ' + err.message;
        errBox.style.display = 'block';
        btn.disabled = false;
        btn.textContent = '登录';
    }
    return false;
}

async function checkLogin() {
    const token = getToken();
    if (!token) {
        // 未登录,显示登录层
        showLoginOverlay();
        return false;
    }
    try {
        const user = await api('/api/auth/me');
        currentUser = user;
        // 角色校验:仅 admin 可进入
        if (user.role !== 'admin') {
            toast('该账号无管理员权限', 'error');
            clearToken();
            showLoginOverlay();
            return false;
        }
        // 缓存用户信息
        localStorage.setItem('admin_user', JSON.stringify(user));
        updateTopbar();
        return true;
    } catch (err) {
        // token 无效,显示登录层
        clearToken();
        showLoginOverlay();
        return false;
    }
}

function updateTopbar() {
    if (!currentUser) return;
    document.getElementById('current-username').textContent = currentUser.real_name || currentUser.username;
    // 两层角色模型
    const roleMap = { admin: '管理员', user: '用户' };
    document.getElementById('current-role').textContent = roleMap[currentUser.role] || currentUser.role;
}

function logout() {
    showConfirm('确定要退出登录吗？', () => {
        clearToken();
        currentUser = null;
        // 显示登录层而不是跳转
        showLoginOverlay();
    }, '退出登录');
}

/* ============ 菜单切换 ============ */

function switchSection(section) {
    // 切换菜单高亮
    document.querySelectorAll('.sidebar .menu-item').forEach(el => {
        el.classList.toggle('active', el.dataset.section === section);
    });
    // 切换内容区
    document.querySelectorAll('.page-section').forEach(el => {
        el.classList.toggle('active', el.id === 'section-' + section);
    });
    // 首次进入时加载
    if (!loaded[section]) {
        loadSection(section);
        loaded[section] = true;
    }
}

function loadSection(section) {
    switch (section) {
        case 'dashboard': loadStats(); break;
        case 'registry': loadRegistry(); break;
        case 'bindings': loadBindings(); break;
        case 'shipping_reset': loadShippingResetDevices(); break;
        case 'approval': loadBindingRequests(); break;
        case 'users': loadUsers(); break;
        case 'logs': loadLogs(); break;
        case 'device_ops': loadDeviceOps(); loadDeviceOpStats(); break;
    }
}

/* ============ 发货前数据清理 ============ */

async function loadShippingResetDevices() {
    const tbody = document.getElementById('shipping-reset-tbody');
    if (!tbody) return;
    tbody.innerHTML = '<tr class="empty-row"><td colspan="7">加载中...</td></tr>';
    try {
        const params = new URLSearchParams();
        const account = document.getElementById('shipping-account-filter').value.trim();
        const search = document.getElementById('shipping-device-filter').value.trim();
        const productType = document.getElementById('shipping-product-filter').value;
        const online = document.getElementById('shipping-online-filter').value;
        if (account) params.set('account', account);
        if (search) params.set('search', search);
        if (productType) params.set('product_type', productType);
        if (online) params.set('online', online);
        const data = await api('/api/admin/shipping-reset/devices?' + params.toString());
        window.shippingResetRows = data.list || [];
        const visibleIds = new Set(window.shippingResetRows.map(row => row.device_id));
        [...shippingResetSelected].forEach(id => { if (!visibleIds.has(id)) shippingResetSelected.delete(id); });
        renderShippingResetDevices(window.shippingResetRows);
    } catch (err) {
        tbody.innerHTML = '<tr class="empty-row"><td colspan="7" style="color:var(--danger);">加载失败: ' + escapeHtml(err.message) + '</td></tr>';
    }
}

function shippingResetRowState(deviceId, fallback = '') {
    return shippingResetState.get(deviceId) || fallback;
}

function renderShippingResetDevices(list) {
    const tbody = document.getElementById('shipping-reset-tbody');
    if (!list.length) {
        tbody.innerHTML = '<tr class="empty-row"><td colspan="7">没有符合筛选条件的设备</td></tr>';
        updateShippingSelectionSummary();
        return;
    }
    tbody.innerHTML = list.map(row => {
        const state = shippingResetRowState(row.device_id, row.reset_pending ? '等待设备响应' : '');
        const terminal = /^(清理成功|失败|设备离线|超时|拒绝)/.test(state);
        const canSelect = row.online && !row.reset_pending && !terminal;
        const checked = shippingResetSelected.has(row.device_id);
        const statusTag = row.online ? '<span class="tag tag-success">在线</span>' : '<span class="tag tag-default">离线</span>';
        const stateClass = state === '清理成功' ? 'tag-success' : (state.startsWith('失败') || state === '超时' || state === '拒绝' ? 'tag-danger' : 'tag-warning');
        const stateHtml = state ? `<span class="tag ${stateClass}" title="${escapeHtml(state)}">${escapeHtml(state)}</span>` : '<span class="text-muted">未执行</span>';
        return `<tr>
            <td><input type="checkbox" ${checked ? 'checked' : ''} ${canSelect ? '' : 'disabled'} onchange="toggleShippingResetDevice('${escapeAttr(row.device_id)}', this.checked)"></td>
            <td><div class="shipping-device-primary">${escapeHtml(row.name || row.device_id)}</div><div class="shipping-device-secondary">${escapeHtml(row.device_id)}<br>UID ${escapeHtml(row.uid || '-')} · 出厂编号 ${escapeHtml(row.serial || '-')}</div></td>
            <td><span class="tag tag-info">${escapeHtml(row.product_type_name)}</span></td>
            <td>${escapeHtml(row.account_names)}</td>
            <td class="shipping-status-cell">${statusTag}</td>
            <td><div class="shipping-data-summary">运行 ${row.run_count || 0} 次 / ${row.run_time_s || 0} 秒<br>举升 ${row.total_lift_count || 0} 次 · 保养 ${row.maintenance_lift_count || 0} 次<br>报警 ${row.alarm_count || 0} · 保养记录 ${row.maintenance_record_count || 0} · 命令 ${row.command_count || 0} · 操作日志 ${row.device_log_count || 0}</div></td>
            <td class="shipping-status-cell">${stateHtml}</td>
        </tr>`;
    }).join('');
    updateShippingSelectionSummary();
}

function toggleShippingResetDevice(deviceId, checked) {
    if (checked) shippingResetSelected.add(deviceId);
    else shippingResetSelected.delete(deviceId);
    updateShippingSelectionSummary();
}

function toggleAllShippingReset(checked) {
    (window.shippingResetRows || []).forEach(row => {
        if (checked && row.online && !row.reset_pending && !shippingResetRowState(row.device_id)) shippingResetSelected.add(row.device_id);
        if (!checked) shippingResetSelected.delete(row.device_id);
    });
    renderShippingResetDevices(window.shippingResetRows || []);
}

function updateShippingSelectionSummary() {
    const count = shippingResetSelected.size;
    const summary = document.getElementById('shipping-selection-summary');
    const submit = document.getElementById('shipping-reset-submit');
    if (summary) summary.textContent = `已选择 ${count} 台`;
    if (submit) submit.disabled = count === 0;
    const selectAll = document.getElementById('shipping-select-all');
    const selectable = (window.shippingResetRows || []).filter(row => row.online && !row.reset_pending && !shippingResetRowState(row.device_id));
    if (selectAll) {
        selectAll.checked = selectable.length > 0 && selectable.every(row => shippingResetSelected.has(row.device_id));
        selectAll.indeterminate = count > 0 && !selectAll.checked;
    }
}

function startShippingReset() {
    const ids = [...shippingResetSelected];
    if (!ids.length) return;
    const names = (window.shippingResetRows || []).filter(row => shippingResetSelected.has(row.device_id)).map(row => row.name || row.device_id);
    showConfirm(`将向 ${ids.length} 台在线设备下发清除命令：${names.join('、')}。设备成功回执后才会删除网站业务历史，资产和绑定不会改变。继续吗？`, async () => {
        try {
            const data = await api('/api/admin/shipping-reset/start', { method: 'POST', body: JSON.stringify({ device_ids: ids }) });
            shippingResetSelected.clear();
            (data.results || []).forEach(item => {
                const state = item.status === 'sent' || item.status === 'pending' ? '等待设备响应' : (item.error || '失败');
                shippingResetState.set(item.device_id, state);
                if (item.msg_id && (item.status === 'sent' || item.status === 'pending')) pollShippingReset(item.device_id, item.msg_id);
            });
            renderShippingResetDevices(window.shippingResetRows || []);
            toast('清除命令已提交，等待设备回执', 'info');
        } catch (err) { toast(err.message, 'error'); }
    }, '确认发货前清除');
}

async function pollShippingReset(deviceId, msgId, attempt = 0) {
    if (attempt > 24) { shippingResetState.set(deviceId, '超时'); renderShippingResetDevices(window.shippingResetRows || []); return; }
    try {
        const status = await api('/api/commands/status/' + encodeURIComponent(msgId));
        if (status.status === 'succeeded') {
            shippingResetState.set(deviceId, '清理成功');
            shippingResetSelected.delete(deviceId);
            renderShippingResetDevices(window.shippingResetRows || []);
            toast(`${deviceId} 发货前清除成功`, 'success');
            return;
        }
        if (['failed', 'rejected', 'timeout'].includes(status.status)) {
            shippingResetState.set(deviceId, `失败：${status.result || status.status}`);
            renderShippingResetDevices(window.shippingResetRows || []);
            toast(`${deviceId} 清除失败`, 'error');
            return;
        }
        setTimeout(() => pollShippingReset(deviceId, msgId, attempt + 1), 1000);
    } catch (err) {
        setTimeout(() => pollShippingReset(deviceId, msgId, attempt + 1), 1000);
    }
}

/* ============ 仪表盘 ============ */

async function loadStats() {
    const grid = document.getElementById('stats-grid');
    grid.innerHTML = '<div class="loading">加载中...</div>';
    try {
        const data = await api('/api/admin/stats');
        renderStats(data);
    } catch (err) {
        grid.innerHTML = '<div class="loading" style="color:var(--danger);">加载失败: ' + escapeHtml(err.message) + '</div>';
    }
}

function renderStats(data) {
    const cards = [
        { label: '运行设备总数', value: data.devices.total, sub: `在线 ${data.devices.online} / 离线 ${data.devices.offline}`, cls: '' },
        { label: '已绑定设备', value: data.devices.bound, sub: `未绑定 ${data.devices.unbound}`, cls: 'success' },
        { label: '在线设备', value: data.devices.online, sub: `共 ${data.devices.total} 台`, cls: 'success' },
        { label: '注册表总数', value: data.registry.total, sub: `已绑定 ${data.registry.bound} / 未绑定 ${data.registry.unbound}`, cls: 'warning' },
        { label: '用户总数', value: data.users.total, sub: `启用 ${data.users.enabled} / 禁用 ${data.users.disabled}`, cls: '' },
        { label: '今日绑定', value: data.today.bindings, sub: `解绑 ${data.today.unbindings}`, cls: 'success' },
        { label: '今日解绑', value: data.today.unbindings, sub: '', cls: 'warning' },
        { label: '今日操作', value: data.today.operations, sub: '操作日志条数', cls: '' }
    ];
    const grid = document.getElementById('stats-grid');
    grid.innerHTML = cards.map(c => `
        <div class="stat-card ${c.cls}">
            <div class="stat-label">${c.label}</div>
            <div class="stat-value">${c.value}</div>
            ${c.sub ? `<div class="stat-sub">${c.sub}</div>` : ''}
        </div>
    `).join('');

    // 今日活动
    const todayActivity = document.getElementById('today-activity');
    todayActivity.innerHTML = `
        <div style="display:flex;gap:24px;flex-wrap:wrap;">
            <div><span class="text-muted">绑定操作：</span><b class="text-success">${data.today.bindings}</b> 次</div>
            <div><span class="text-muted">解绑操作：</span><b class="text-warning">${data.today.unbindings}</b> 次</div>
            <div><span class="text-muted">管理操作：</span><b class="text-primary">${data.today.operations}</b> 次</div>
        </div>
    `;

    // 趋势图
    renderTrend(data.trend || []);
}

function renderTrend(trend) {
    const container = document.getElementById('trend-chart');
    if (!trend || trend.length === 0) {
        container.innerHTML = '<span class="text-muted">最近 7 天无绑定数据</span>';
        return;
    }
    // 按 day 分组
    const days = {};
    trend.forEach(item => {
        if (!days[item.day]) days[item.day] = { bind: 0, unbind: 0 };
        days[item.day][item.action] = (days[item.day][item.action] || 0) + item.c;
    });
    const dayList = Object.keys(days).sort();
    const maxVal = Math.max(1, ...dayList.map(d => Math.max(days[d].bind, days[d].unbind)));
    container.innerHTML = `
        <div style="display:flex;gap:4px;align-items:flex-end;height:160px;padding:8px 0;">
            ${dayList.map(d => {
                const b = days[d].bind;
                const u = days[d].unbind;
                const bH = Math.round((b / maxVal) * 120);
                const uH = Math.round((u / maxVal) * 120);
                return `
                    <div style="flex:1;text-align:center;">
                        <div style="display:flex;gap:2px;align-items:flex-end;justify-content:center;height:130px;">
                            <div title="绑定 ${b}" style="width:14px;height:${bH}px;background:var(--success);border-radius:2px 2px 0 0;min-height:2px;"></div>
                            <div title="解绑 ${u}" style="width:14px;height:${uH}px;background:var(--warning);border-radius:2px 2px 0 0;min-height:2px;"></div>
                        </div>
                        <div style="font-size:11px;color:var(--text-light);margin-top:4px;">${d.substring(5)}</div>
                    </div>
                `;
            }).join('')}
        </div>
        <div style="margin-top:8px;font-size:12px;color:var(--text-light);">
            <span style="display:inline-block;width:10px;height:10px;background:var(--success);border-radius:2px;margin-right:4px;"></span>绑定
            <span style="display:inline-block;width:10px;height:10px;background:var(--warning);border-radius:2px;margin:0 4px 0 12px;"></span>解绑
        </div>
    `;
}

/* ============ 设备注册管理 ============ */

async function loadRegistry() {
    const tbody = document.getElementById('registry-tbody');
    tbody.innerHTML = '<tr class="empty-row"><td colspan="10">加载中...</td></tr>';
    try {
        const search = document.getElementById('registry-search').value.trim();
        const status = document.getElementById('registry-status-filter').value;
        const productType = document.getElementById('registry-product-filter').value;
        const params = new URLSearchParams({
            page: registryPage, pageSize: registryPageSize
        });
        if (search) params.set('search', search);
        if (status) params.set('status', status);
        if (productType) params.set('product_type', productType);
        const data = await api('/api/admin/registry?' + params.toString());
        registryTotal = data.total;
        renderRegistry(data.list);
        renderPagination('registry-pagination', registryPage, registryPageSize, registryTotal, (p) => {
            registryPage = p; loadRegistry();
        });
    } catch (err) {
        tbody.innerHTML = '<tr class="empty-row"><td colspan="10" style="color:var(--danger);">加载失败: ' + escapeHtml(err.message) + '</td></tr>';
    }
}

// 产品型号中文映射(与后端 PRODUCT_CONFIGS 保持一致)
const PRODUCT_TYPE_NAMES = {
    double_post: '两柱举升机',
    small_scissor: '小剪举升机',
    thin_scissor: '超薄小剪举升机',
    large_scissor: '大剪举升机'
};
const FIRMWARE_DISPLAY_NAMES = {
    double_post: 'GC-Two_Pillars',
    small_scissor: 'GC_Small_Scissor',
    thin_scissor: 'GC_Thin_Scissor',
    large_scissor: 'GC_Big_Scissor'
};

function normalizeRegistryUid(value) {
    return String(value || '').trim().toLowerCase().replace(/^0x/, '').replace(/[\s:\-]/g, '');
}

function syncRegistryDisplayName() {
    const productType = document.getElementById('registry-product-type').value;
    document.getElementById('registry-display-name').value = FIRMWARE_DISPLAY_NAMES[productType] || '';
}

function renderRegistry(list) {
    const tbody = document.getElementById('registry-tbody');
    if (!list || list.length === 0) {
        tbody.innerHTML = '<tr class="empty-row"><td colspan="10">暂无数据</td></tr>';
        return;
    }
    tbody.innerHTML = list.map(r => {
        const productTypeName = PRODUCT_TYPE_NAMES[r.product_type] || '两柱举升机';
        const displayName = r.display_name || FIRMWARE_DISPLAY_NAMES[r.product_type] || '';
        const accessories = [
            r.has_encoder ? '高度' : '',
            r.has_buzzer ? '蜂鸣' : '',
            r.has_pressure_sensor ? '压力' : '',
            r.has_display ? '屏幕' : ''
        ].filter(Boolean);
        return `
        <tr>
            <td>${r.id}</td>
            <td class="nowrap">${escapeHtml(r.serial)}</td>
            <td class="break-all" style="max-width:200px;">${escapeHtml(r.uid)}</td>
            <td><span class="tag tag-info">${escapeHtml(productTypeName)}</span>${displayName ? `<div class="text-muted" style="font-size:11px;margin-top:2px;">${escapeHtml(displayName)}</div>` : ''}${accessories.length ? `<div style="margin-top:4px;">${accessories.map(a => `<span class="tag tag-default" style="margin-right:3px;">${a}</span>`).join('')}</div>` : `<div class="text-muted" style="font-size:11px;margin-top:4px;">无高度编码器</div>`}</td>
            <td>${escapeHtml(r.batch)}</td>
            <td>${escapeHtml(r.produced_at)}</td>
            <td>${r.status === 'bound'
                ? '<span class="tag tag-success">已绑定</span>'
                : '<span class="tag tag-default">未绑定</span>'}</td>
            <td class="break-all" style="max-width:160px;">${escapeHtml(r.bound_device_id || '-')}</td>
            <td class="nowrap">${escapeHtml(r.created_at)}</td>
            <td class="nowrap">
                <span class="action-link" onclick="editRegistry(${r.id}, ${escapeAttr(JSON.stringify(r))})">编辑</span>
                ${r.status === 'unbound'
                    ? `<span class="action-link danger" onclick="deleteRegistry(${r.id}, '${escapeAttr(r.serial)}')">删除</span>`
                    : ''}
            </td>
        </tr>
        `;
    }).join('');
}

function escapeAttr(s) {
    return String(s).replace(/'/g, "\\'").replace(/"/g, '&quot;');
}

function showRegistryModal() {
    document.getElementById('registry-modal-title').textContent = '新增设备注册';
    document.getElementById('registry-id').value = '';
    document.getElementById('registry-serial').value = '';
    document.getElementById('registry-uid').value = '';
    document.getElementById('registry-bind-code').value = '';
    document.getElementById('registry-bind-code-required').style.display = '';
    document.getElementById('registry-product-type').value = 'double_post';
    syncRegistryDisplayName();
    document.getElementById('registry-model').value = '';
    document.getElementById('registry-batch').value = '';
    document.getElementById('registry-produced').value = '';
    document.getElementById('registry-has-encoder').checked = false;
    document.getElementById('registry-has-buzzer').checked = false;
    document.getElementById('registry-has-pressure').checked = false;
    document.getElementById('registry-has-display').checked = false;
    openModal('registry-modal');
}

function editRegistry(id, data) {
    document.getElementById('registry-modal-title').textContent = '编辑设备注册';
    document.getElementById('registry-id').value = id;
    document.getElementById('registry-serial').value = data.serial || '';
    document.getElementById('registry-uid').value = data.uid || '';
    document.getElementById('registry-bind-code').value = '';
    document.getElementById('registry-bind-code-required').style.display = 'none';
    document.getElementById('registry-product-type').value = data.product_type || 'double_post';
    document.getElementById('registry-display-name').value = data.display_name || FIRMWARE_DISPLAY_NAMES[data.product_type] || '';
    document.getElementById('registry-model').value = data.model || '';
    document.getElementById('registry-batch').value = data.batch || '';
    document.getElementById('registry-produced').value = data.produced_at || '';
    document.getElementById('registry-has-encoder').checked = !!data.has_encoder;
    document.getElementById('registry-has-buzzer').checked = !!data.has_buzzer;
    document.getElementById('registry-has-pressure').checked = !!data.has_pressure_sensor;
    document.getElementById('registry-has-display').checked = !!data.has_display;
    openModal('registry-modal');
}

async function submitRegistry() {
    const id = document.getElementById('registry-id').value;
    const payload = {
        serial: document.getElementById('registry-serial').value.trim(),
        uid: normalizeRegistryUid(document.getElementById('registry-uid').value),
        bind_code: document.getElementById('registry-bind-code').value.trim(),
        product_type: document.getElementById('registry-product-type').value,
        model: document.getElementById('registry-model').value.trim(),
        batch: document.getElementById('registry-batch').value.trim(),
        produced_at: document.getElementById('registry-produced').value,
        has_encoder: document.getElementById('registry-has-encoder').checked ? 1 : 0,
        has_buzzer: document.getElementById('registry-has-buzzer').checked ? 1 : 0,
        has_pressure_sensor: document.getElementById('registry-has-pressure').checked ? 1 : 0,
        has_display: document.getElementById('registry-has-display').checked ? 1 : 0
    };
    if (!payload.serial || !payload.uid) {
        toast('出厂编号和芯片UID不能为空', 'error');
        return;
    }
    if (!/^[0-9a-f]{24}$/.test(payload.uid)) {
        toast('芯片UID格式错误，应为24位十六进制字符', 'error');
        return;
    }
    if (!id && !payload.bind_code) {
        toast('新增设备必须填写绑定码', 'error');
        return;
    }
    if (id && !payload.bind_code) {
        delete payload.bind_code;
    }
    try {
        if (id) {
            await api('/api/admin/registry/' + id, {
                method: 'PUT',
                body: JSON.stringify(payload)
            });
            toast('修改成功', 'success');
        } else {
            await api('/api/admin/registry', {
                method: 'POST',
                body: JSON.stringify(payload)
            });
            toast('新增成功', 'success');
        }
        closeModal('registry-modal');
        loadRegistry();
    } catch (err) {
        toast(err.message, 'error');
    }
}

function deleteRegistry(id, serial) {
    showConfirm(`确定要删除出厂编号 "${serial}" 的注册记录吗？`, async () => {
        try {
            await api('/api/admin/registry/' + id, { method: 'DELETE' });
            toast('删除成功', 'success');
            loadRegistry();
        } catch (err) {
            toast(err.message, 'error');
        }
    }, '删除注册记录');
}

function showBatchImportModal() {
    document.getElementById('batch-json').value = '';
    document.getElementById('batch-result').innerHTML = '';
    openModal('batch-modal');
}

async function submitBatchImport() {
    const text = document.getElementById('batch-json').value.trim();
    if (!text) {
        toast('请输入 JSON 数据', 'error');
        return;
    }
    let arr;
    try {
        arr = JSON.parse(text);
    } catch (e) {
        toast('JSON 格式错误: ' + e.message, 'error');
        return;
    }
    if (!Array.isArray(arr)) {
        toast('数据必须是 JSON 数组', 'error');
        return;
    }
    const resultDiv = document.getElementById('batch-result');
    resultDiv.innerHTML = '<div class="import-result">导入中...</div>';
    try {
        const result = await api('/api/admin/registry/batch', {
            method: 'POST',
            body: JSON.stringify(arr)
        });
        const errList = (result.errors && result.errors.length)
            ? `<div class="error-list">${result.errors.map(e => escapeHtml(e)).join('<br>')}</div>`
            : '';
        resultDiv.innerHTML = `
            <div class="import-result">
                <div class="result-row">总计: <b>${result.total}</b> 条</div>
                <div class="result-row text-success">成功: <b>${result.success}</b> 条</div>
                <div class="result-row text-danger">失败: <b>${result.failed}</b> 条</div>
                ${errList}
            </div>
        `;
        toast(`导入完成: 成功 ${result.success} 条, 失败 ${result.failed} 条`,
            result.failed > 0 ? 'warning' : 'success');
        loadRegistry();
    } catch (err) {
        resultDiv.innerHTML = '<div class="import-result text-danger">导入失败: ' + escapeHtml(err.message) + '</div>';
        toast(err.message, 'error');
    }
}

function exportRegistry() {
    const token = getToken();
    // 用 fetch 拿到 blob 再下载，确保带 Authorization 头
    fetch('/api/admin/registry/export', {
        headers: { 'Authorization': 'Bearer ' + token }
    }).then(resp => {
        if (resp.status === 401) {
            clearToken();
            window.location.href = '/';
            throw new Error('未登录');
        }
        return resp.blob();
    }).then(blob => {
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = 'device_registry.csv';
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        URL.revokeObjectURL(url);
        toast('导出成功', 'success');
    }).catch(err => {
        if (err.message !== '未登录') {
            toast('导出失败: ' + err.message, 'error');
        }
    });
}

/* ============ 绑定看板 ============ */

async function loadBindings() {
    const tbody = document.getElementById('bindings-tbody');
    tbody.innerHTML = '<tr class="empty-row"><td colspan="10">加载中...</td></tr>';
    try {
        const search = document.getElementById('bindings-search').value.trim();
        const status = document.getElementById('bindings-status-filter').value;
        const productType = document.getElementById('bindings-product-filter').value;
        const params = new URLSearchParams({
            page: bindingsPage, pageSize: bindingsPageSize
        });
        if (search) params.set('search', search);
        if (status) params.set('status', status);
        if (productType) params.set('product_type', productType);
        const data = await api('/api/admin/bindings?' + params.toString());
        bindingsTotal = data.total;
        renderBindings(data.list);
        renderPagination('bindings-pagination', bindingsPage, bindingsPageSize, bindingsTotal, (p) => {
            bindingsPage = p; loadBindings();
        });
    } catch (err) {
        tbody.innerHTML = '<tr class="empty-row"><td colspan="10" style="color:var(--danger);">加载失败: ' + escapeHtml(err.message) + '</td></tr>';
    }
}

function renderBindings(list) {
    const tbody = document.getElementById('bindings-tbody');
    if (!list || list.length === 0) {
        tbody.innerHTML = '<tr class="empty-row"><td colspan="10">暂无数据</td></tr>';
        return;
    }
    tbody.innerHTML = list.map(r => {
        // 使用 bindings 数组渲染多对多绑定的用户列表
        const bindingUsers = (r.bindings || []).map(b => {
            const name = b.real_name ? `${escapeHtml(b.real_name)} (${escapeHtml(b.username)})` : escapeHtml(b.username || '');
            const tag = b.status === 'pending' ? '<span class="tag tag-warning" style="margin-left:4px;font-size:10px;">待审批</span>'
                      : b.status === 'revoked' ? '<span class="tag tag-default" style="margin-left:4px;font-size:10px;">已撤销</span>'
                      : '';
            return `<div style="margin-bottom:2px;">${name}${tag}</div>`;
        }).join('') || '-';
        const activeCount = r.active_binding_count || 0;
        const pendingCount = r.pending_binding_count || 0;
        const bound = r.bind_status === 'bound';
        const productTypeName = escapeHtml(r.product_type_name || PRODUCT_TYPE_NAMES[r.product_type] || '两柱举升机');
        const accessories = [
            r.has_encoder ? '高度' : '',
            r.has_buzzer ? '蜂鸣' : '',
            r.has_pressure_sensor ? '压力' : '',
            r.has_display ? '屏幕' : ''
        ].filter(Boolean);
        return `
            <tr>
                <td class="break-all" style="max-width:180px;">${escapeHtml(r.device_id)}${r.gateway_id ? `<div class="text-muted" style="font-size:11px;margin-top:2px;">网关 ${escapeHtml(r.gateway_id)}</div>` : ''}</td>
                <td>${escapeHtml(r.name)}</td>
                <td><span class="tag tag-info">${productTypeName}</span>${accessories.length ? `<div style="margin-top:4px;">${accessories.map(a => `<span class="tag tag-default" style="margin-right:3px;">${a}</span>`).join('')}</div>` : '<div class="text-muted" style="font-size:11px;margin-top:4px;">无高度编码器</div>'}</td>
                <td class="break-all" style="max-width:180px;">${escapeHtml(r.uid || '-')}</td>
                <td>${escapeHtml(r.serial || '-')}</td>
                <td>${r.online
                    ? '<span class="tag tag-success">在线</span>'
                    : '<span class="tag tag-default">离线</span>'}</td>
                <td>${bound
                    ? `<span class="tag tag-success">已绑定</span><div class="text-muted" style="font-size:11px;margin-top:2px;">${activeCount}活跃${pendingCount > 0 ? ' / ' + pendingCount + '待审' : ''}</div>`
                    : '<span class="tag tag-default">未绑定</span>'}</td>
                <td>${bindingUsers}</td>
                <td class="nowrap">${escapeHtml(r.bound_at || '-')}</td>
                <td class="nowrap">
                    <span class="action-link" onclick="showDeviceEditModal('${escapeAttr(r.device_id)}')">编辑</span>
                    ${bound
                        ? `&nbsp;|&nbsp;<span class="action-link danger" onclick="forceUnbind('${escapeAttr(r.device_id)}', '${escapeAttr(r.name)}')">强制解绑</span>`
                        : ''}
                </td>
            </tr>
        `;
    }).join('');
}

// set_product_type 接口已移除,产品型号由出厂登记固定,禁止远程切换
// showProductTypeModal 函数已删除

/* ============ 设备编辑 ============ */

async function showDeviceEditModal(deviceId) {
    try {
        const device = await api('/api/devices/' + encodeURIComponent(deviceId));
        document.getElementById('device-edit-id').value = '';
        document.getElementById('device-edit-device-id').value = device.device_id;
        document.getElementById('device-edit-name').value = device.name || '';
        document.getElementById('device-edit-model').value = device.model || '';
        document.getElementById('device-edit-group').value = device.group || '';
        document.getElementById('device-edit-location').value = device.location || '';
        document.getElementById('device-edit-modal-title').textContent = '编辑设备 - ' + device.device_id;
        openModal('device-edit-modal');
    } catch (err) {
        toast(err.message, 'error');
    }
}

async function submitDeviceEdit() {
    const deviceId = document.getElementById('device-edit-device-id').value.trim();
    const name = document.getElementById('device-edit-name').value.trim();
    if (!name) {
        toast('设备名称不能为空', 'error');
        return;
    }
    const model = document.getElementById('device-edit-model').value.trim();
    const group = document.getElementById('device-edit-group').value.trim();
    const location = document.getElementById('device-edit-location').value.trim();
    try {
        await api('/api/devices/' + encodeURIComponent(deviceId), {
            method: 'PUT',
            body: JSON.stringify({ name, model, group_name: group, location })
        });
        toast('修改成功', 'success');
        closeModal('device-edit-modal');
        loadBindings();
    } catch (err) {
        toast(err.message, 'error');
    }
}

function forceUnbind(deviceId, name) {
    showConfirm(`确定要强制解绑设备 "${name}" 吗？该设备的当前绑定关系将被解除。`, async () => {
        try {
            await api('/api/admin/bindings/' + encodeURIComponent(deviceId) + '/unbind', {
                method: 'POST'
            });
            toast('解绑成功', 'success');
            loadBindings();
        } catch (err) {
            toast(err.message, 'error');
        }
    }, '强制解绑');
}

/* ============ 绑定审批中心 ============ */

async function loadBindingRequests() {
    const tbody = document.getElementById('approval-pending-tbody');
    document.getElementById('approval-pending-section').style.display = '';
    document.getElementById('approval-history-section').style.display = 'none';
    tbody.innerHTML = '<tr class="empty-row"><td colspan="7">加载中...</td></tr>';
    try {
        const data = await api('/api/admin/binding-requests?status=pending');
        const list = data.list || data || [];
        if (!list.length) {
            tbody.innerHTML = '<tr class="empty-row"><td colspan="7">暂无待审批申请</td></tr>';
            return;
        }
        tbody.innerHTML = list.map(r => `
            <tr>
                <td>${r.id}</td>
                <td class="break-all">${escapeHtml(r.serial || r.device_id)}</td>
                <td>${escapeHtml(r.device_name || '-')}</td>
                <td>${escapeHtml(r.username || ('用户#' + r.user_id))}</td>
                <td class="nowrap">${escapeHtml(r.requested_at || '-')}</td>
                <td class="break-all" style="max-width:200px;">${escapeHtml(r.request_detail || '-')}</td>
                <td class="nowrap">
                    <span class="action-link" onclick="approveBinding(${r.id})">批准</span>
                    <span class="action-link danger" onclick="rejectBinding(${r.id})">拒绝</span>
                </td>
            </tr>
        `).join('');
    } catch (err) {
        tbody.innerHTML = '<tr class="empty-row"><td colspan="7" style="color:var(--danger);">加载失败: ' + escapeHtml(err.message) + '</td></tr>';
    }
}

async function loadBindingHistory() {
    const tbody = document.getElementById('approval-history-tbody');
    document.getElementById('approval-pending-section').style.display = 'none';
    document.getElementById('approval-history-section').style.display = '';
    tbody.innerHTML = '<tr class="empty-row"><td colspan="8">加载中...</td></tr>';
    try {
        const data = await api('/api/admin/binding-requests/history');
        const list = data.list || data || [];
        if (!list.length) {
            tbody.innerHTML = '<tr class="empty-row"><td colspan="8">暂无审批历史</td></tr>';
            return;
        }
        const statusLabels = { approved: '<span class="tag tag-success">已批准</span>', rejected: '<span class="tag tag-danger">已拒绝</span>', cancelled: '<span class="tag tag-default">已取消</span>' };
        tbody.innerHTML = list.map(r => `
            <tr>
                <td>${r.id}</td>
                <td class="break-all">${escapeHtml(r.serial || r.device_id)}</td>
                <td>${escapeHtml(r.username || ('用户#' + r.user_id))}</td>
                <td>${statusLabels[r.status] || escapeHtml(r.status)}</td>
                <td class="nowrap">${escapeHtml(r.requested_at || '-')}</td>
                <td class="nowrap">${escapeHtml(r.reviewed_at || '-')}</td>
                <td>${escapeHtml(r.reviewer_name || (r.reviewed_by ? '管理员#' + r.reviewed_by : '-'))}</td>
                <td class="break-all" style="max-width:200px;">${escapeHtml(r.review_detail || '-')}</td>
            </tr>
        `).join('');
    } catch (err) {
        tbody.innerHTML = '<tr class="empty-row"><td colspan="8" style="color:var(--danger);">加载失败: ' + escapeHtml(err.message) + '</td></tr>';
    }
}

async function approveBinding(requestId) {
    const note = prompt('批准说明(可选):', '');
    if (note === null) return;
    try {
        await api('/api/admin/binding-requests/' + requestId + '/approve', {
            method: 'POST',
            body: JSON.stringify({ note: note || '' })
        });
        toast('绑定申请已批准', 'success');
        loadBindingRequests();
    } catch (err) {
        toast('批准失败: ' + err.message, 'error');
    }
}

async function rejectBinding(requestId) {
    const note = prompt('拒绝原因:', '');
    if (note === null) return;
    if (!note || !note.trim()) {
        toast('请填写拒绝原因', 'error');
        return;
    }
    try {
        await api('/api/admin/binding-requests/' + requestId + '/reject', {
            method: 'POST',
            body: JSON.stringify({ note: note.trim() })
        });
        toast('绑定申请已拒绝', 'info');
        loadBindingRequests();
    } catch (err) {
        toast('拒绝失败: ' + err.message, 'error');
    }
}

/* ============ 用户管理 ============ */

async function loadUsers() {
    const tbody = document.getElementById('users-tbody');
    tbody.innerHTML = '<tr class="empty-row"><td colspan="10">加载中...</td></tr>';
    try {
        const list = await api('/api/admin/users');
        renderUsers(list);
    } catch (err) {
        tbody.innerHTML = '<tr class="empty-row"><td colspan="10" style="color:var(--danger);">加载失败: ' + escapeHtml(err.message) + '</td></tr>';
    }
}

function renderUsers(list) {
    const tbody = document.getElementById('users-tbody');
    if (!list || list.length === 0) {
        tbody.innerHTML = '<tr class="empty-row"><td colspan="10">暂无数据</td></tr>';
        return;
    }
    const roleMap = {
        admin: { name: '管理员', cls: 'tag-info' },
        user: { name: '用户', cls: 'tag-default' }
    };
    tbody.innerHTML = list.map(u => {
        const role = roleMap[u.role] || { name: u.role, cls: 'tag-default' };
        const enabled = u.enabled === 1 || u.enabled === true;
        const isSelf = currentUser && u.id === currentUser.id;
        return `
            <tr>
                <td>${u.id}</td>
                <td>${escapeHtml(u.username)}${isSelf ? ' <span class="text-muted">(我)</span>' : ''}</td>
                <td><span class="tag ${role.cls}">${role.name}</span></td>
                <td>${escapeHtml(u.real_name || '-')}</td>
                <td>${escapeHtml(u.phone || '-')}</td>
                <td>${enabled
                    ? '<span class="tag tag-success">启用</span>'
                    : '<span class="tag tag-danger">禁用</span>'}</td>
                <td>${u.must_change_password
                    ? '<span class="tag tag-warning">需改密</span>'
                    : '<span class="text-muted">-</span>'}</td>
                <td class="nowrap">${escapeHtml(u.created_at || '-')}</td>
                <td class="nowrap">${escapeHtml(u.last_login || '-')}</td>
                <td class="nowrap">
                    <span class="action-link" onclick="showResetPwdModal(${u.id}, '${escapeAttr(u.username)}')">重置密码</span>
                    ${!isSelf
                        ? `<span class="action-link" onclick="toggleUser(${u.id})">${enabled ? '禁用' : '启用'}</span>
                           <span class="action-link danger" onclick="deleteUser(${u.id}, '${escapeAttr(u.username)}')">删除</span>`
                        : ''}
                </td>
            </tr>
        `;
    }).join('');
}

function showUserModal() {
    document.getElementById('user-modal-title').textContent = '新增用户';
    document.getElementById('user-id').value = '';
    document.getElementById('user-username').value = '';
    document.getElementById('user-username').disabled = false;
    document.getElementById('user-role').value = 'user';
    document.getElementById('user-realname').value = '';
    document.getElementById('user-phone').value = '';
    document.getElementById('user-password').value = '';
    document.getElementById('user-password-group').style.display = '';
    openModal('user-modal');
}

async function submitUser() {
    const username = document.getElementById('user-username').value.trim();
    const password = document.getElementById('user-password').value;
    const role = document.getElementById('user-role').value;
    const real_name = document.getElementById('user-realname').value.trim();
    const phone = document.getElementById('user-phone').value.trim();
    if (!username) { toast('用户名不能为空', 'error'); return; }
    if (!password) { toast('初始密码不能为空', 'error'); return; }

    try {
        await api('/api/admin/users', {
            method: 'POST',
            body: JSON.stringify({ username, password, role, real_name, phone })
        });
        toast('创建成功', 'success');
        closeModal('user-modal');
        loadUsers();
    } catch (err) {
        toast(err.message, 'error');
    }
}

function showResetPwdModal(id, username) {
    document.getElementById('reset-pwd-id').value = id;
    document.getElementById('reset-pwd-username').textContent = username;
    document.getElementById('reset-pwd-new').value = '';
    openModal('reset-pwd-modal');
}

async function submitResetPassword() {
    const id = document.getElementById('reset-pwd-id').value;
    const newPwd = document.getElementById('reset-pwd-new').value;
    // 必须显式输入新密码,后端不再静默回退到默认密码
    if (!newPwd) {
        toast('请输入新密码', 'error');
        return;
    }
    if (newPwd.length < 6) {
        toast('新密码至少6位', 'error');
        return;
    }
    try {
        await api('/api/admin/users/' + id + '/reset-password', {
            method: 'PUT',
            body: JSON.stringify({ new_password: newPwd })
        });
        toast('密码已重置,请通知用户使用新密码登录', 'success', 4000);
        closeModal('reset-pwd-modal');
    } catch (err) {
        toast(err.message, 'error');
    }
}

function toggleUser(id) {
    showConfirm('确定要切换该用户的启用状态吗？', async () => {
        try {
            await api('/api/admin/users/' + id + '/toggle', { method: 'PUT' });
            toast('操作成功', 'success');
            loadUsers();
        } catch (err) {
            toast(err.message, 'error');
        }
    }, '切换用户状态');
}

function deleteUser(id, username) {
    showConfirm(`确定要删除用户 "${username}" 吗？此操作不可恢复。`, async () => {
        try {
            await api('/api/admin/users/' + id, { method: 'DELETE' });
            toast('删除成功', 'success');
            loadUsers();
        } catch (err) {
            toast(err.message, 'error');
        }
    }, '删除用户');
}

/* ============ 操作日志 ============ */

async function loadLogs() {
    const tbody = document.getElementById('logs-tbody');
    tbody.innerHTML = '<tr class="empty-row"><td colspan="7">加载中...</td></tr>';
    try {
        const action = document.getElementById('logs-action').value.trim();
        const device = document.getElementById('logs-device').value.trim();
        const user = document.getElementById('logs-user').value.trim();
        const source = document.getElementById('logs-source-filter').value;
        const start = document.getElementById('logs-start').value;
        const end = document.getElementById('logs-end').value;
        const params = new URLSearchParams({
            page: logsPage, pageSize: logsPageSize
        });
        if (action) params.set('action', action);
        if (device) params.set('device_id', device);
        if (user) params.set('user_id', user);
        if (source) params.set('source', source);
        if (start) params.set('start_date', start);
        if (end) params.set('end_date', end);
        const data = await api('/api/admin/logs?' + params.toString());
        logsTotal = data.total;
        renderLogs(data.list);
        renderPagination('logs-pagination', logsPage, logsPageSize, logsTotal, (p) => {
            logsPage = p; loadLogs();
        });
    } catch (err) {
        tbody.innerHTML = '<tr class="empty-row"><td colspan="7" style="color:var(--danger);">加载失败: ' + escapeHtml(err.message) + '</td></tr>';
    }
}

function renderLogs(list) {
    const tbody = document.getElementById('logs-tbody');
    if (!list || list.length === 0) {
        tbody.innerHTML = '<tr class="empty-row"><td colspan="7">暂无数据</td></tr>';
        return;
    }
    tbody.innerHTML = list.map(l => {
        const userName = l.real_name
            ? escapeHtml(l.real_name) + ' (' + escapeHtml(l.username || '-') + ')'
            : escapeHtml(l.username || '-');
        const sourceTag = l.source === 'binding'
            ? '<span class="tag tag-info">绑定</span>'
            : '<span class="tag tag-default">操作</span>';
        let resultHtml = '-';
        if (l.source === 'operation' && l.result) {
            const cls = l.result === '成功' ? 'tag-success' : 'tag-danger';
            resultHtml = `<span class="tag ${cls}">${escapeHtml(l.result)}</span>`;
        } else if (l.source === 'binding' && l.ip) {
            resultHtml = '<span class="text-muted">IP: ' + escapeHtml(l.ip) + '</span>';
        }
        return `
            <tr>
                <td class="nowrap">${escapeHtml(l.created_at)}</td>
                <td>${sourceTag}</td>
                <td>${escapeHtml(l.action)}</td>
                <td>${userName}</td>
                <td class="break-all" style="max-width:160px;">${escapeHtml(l.device_id || '-')}</td>
                <td style="max-width:300px;word-break:break-all;">${escapeHtml(l.detail || '-')}</td>
                <td>${resultHtml}</td>
            </tr>
        `;
    }).join('');
}

/* ============ 分页组件 ============ */

function renderPagination(containerId, page, pageSize, total, onPage) {
    const container = document.getElementById(containerId);
    const totalPages = Math.ceil(total / pageSize) || 1;
    const start = total === 0 ? 0 : (page - 1) * pageSize + 1;
    const end = Math.min(page * pageSize, total);

    // 生成页码按钮（最多显示 7 个）
    let pageBtns = [];
    if (totalPages <= 7) {
        for (let i = 1; i <= totalPages; i++) pageBtns.push(i);
    } else {
        pageBtns = [1];
        if (page > 4) pageBtns.push('...');
        const s = Math.max(2, page - 1);
        const e = Math.min(totalPages - 1, page + 1);
        for (let i = s; i <= e; i++) pageBtns.push(i);
        if (page < totalPages - 3) pageBtns.push('...');
        pageBtns.push(totalPages);
    }

    container.innerHTML = `
        <div>共 ${total} 条，第 ${start}-${end} 条 / 第 ${page}/${totalPages} 页</div>
        <div class="page-btns">
            <button class="page-btn" ${page <= 1 ? 'disabled' : ''} data-page="${page - 1}">上一页</button>
            ${pageBtns.map(p => {
                if (p === '...') return '<button class="page-btn" disabled>...</button>';
                return `<button class="page-btn ${p === page ? 'active' : ''}" data-page="${p}">${p}</button>`;
            }).join('')}
            <button class="page-btn" ${page >= totalPages ? 'disabled' : ''} data-page="${page + 1}">下一页</button>
        </div>
    `;
    container.querySelectorAll('.page-btn[data-page]').forEach(btn => {
        btn.addEventListener('click', () => {
            const p = parseInt(btn.dataset.page);
            if (!isNaN(p) && p >= 1 && p <= totalPages && p !== page) {
                onPage(p);
            }
        });
    });
}

/* ============ 设备端操作日志(工人物理操作) ============ */

// 操作类型中文映射
const DEVICE_OP_LABELS = {
    up: { name: '上升', cls: 'tag-info' },
    down: { name: '下降', cls: 'tag-info' },
    lock: { name: '锁定', cls: 'tag-warning' },
    unlock: { name: '解锁', cls: 'tag-success' },
    refill: { name: '补油', cls: 'tag-default' },
    estop: { name: '急停', cls: 'tag-danger' },
    photo_alarm: { name: '光电报警', cls: 'tag-danger' },
    rotary_switch: { name: '旋转开关切换', cls: 'tag-default' },
    power_on: { name: '开机', cls: 'tag-success' },
    power_off: { name: '关机', cls: 'tag-default' },
    unknown: { name: '未知', cls: 'tag-default' }
};

async function loadDeviceOps() {
    const tbody = document.getElementById('device-ops-tbody');
    tbody.innerHTML = '<tr class="empty-row"><td colspan="9">加载中...</td></tr>';
    try {
        const serial = document.getElementById('device-op-serial').value.trim();
        const uid = document.getElementById('device-op-uid').value.trim();
        const opType = document.getElementById('device-op-type-filter').value;
        const startDate = document.getElementById('device-op-start').value;
        const endDate = document.getElementById('device-op-end').value;
        const params = new URLSearchParams({
            page: deviceOpsPage, pageSize: deviceOpsPageSize
        });
        if (serial) params.set('device_serial', serial);
        if (uid) params.set('device_uid', uid);
        if (opType) params.set('op_type', opType);
        if (startDate) params.set('start_date', startDate);
        if (endDate) params.set('end_date', endDate);
        const data = await api('/api/device-ops?' + params.toString());
        deviceOpsTotal = data.total;
        renderDeviceOps(data.list);
        renderPagination('device-ops-pagination', deviceOpsPage, deviceOpsPageSize, deviceOpsTotal, (p) => {
            deviceOpsPage = p; loadDeviceOps();
        });
    } catch (err) {
        tbody.innerHTML = '<tr class="empty-row"><td colspan="9" style="color:var(--danger);">加载失败: ' + escapeHtml(err.message) + '</td></tr>';
    }
}

function renderDeviceOps(list) {
    const tbody = document.getElementById('device-ops-tbody');
    if (!list || list.length === 0) {
        tbody.innerHTML = '<tr class="empty-row"><td colspan="9">暂无数据</td></tr>';
        return;
    }
    tbody.innerHTML = list.map(r => {
        const label = DEVICE_OP_LABELS[r.op_type] || DEVICE_OP_LABELS.unknown;
        const resultCls = r.op_result === 'ok' ? 'tag-success' : (r.op_result === 'fail' ? 'tag-danger' : 'tag-default');
        return `
            <tr>
                <td class="nowrap">${escapeHtml(r.occurred_at || '-')}</td>
                <td class="nowrap">${escapeHtml(r.received_at || '-')}</td>
                <td>${escapeHtml(r.device_serial || '-')}</td>
                <td class="break-all" style="max-width:180px;">${escapeHtml(r.device_uid || '-')}</td>
                <td><span class="tag ${label.cls}">${label.name}</span></td>
                <td><span class="tag ${resultCls}">${escapeHtml(r.op_result || '-')}</span></td>
                <td>${r.duration_ms || 0}</td>
                <td>${escapeHtml(r.device_state || '-')}</td>
                <td style="max-width:300px;word-break:break-all;">${escapeHtml(r.detail || '-')}</td>
            </tr>
        `;
    }).join('');
}

async function loadDeviceOpStats() {
    const grid = document.getElementById('device-op-stats-grid');
    grid.innerHTML = '<div class="loading">加载中...</div>';
    try {
        const data = await api('/api/device-ops/stats');
        renderDeviceOpStats(data);
    } catch (err) {
        grid.innerHTML = '<div class="empty-state" style="color:var(--danger);">加载失败: ' + escapeHtml(err.message) + '</div>';
    }
}

function renderDeviceOpStats(data) {
    const grid = document.getElementById('device-op-stats-grid');
    const byType = data.by_type || [];
    const byResult = data.by_result || [];
    // 操作类型分布卡片
    const typeCards = byType.map(item => {
        const label = DEVICE_OP_LABELS[item.op_type] || DEVICE_OP_LABELS.unknown;
        return `<div class="stat-card"><div class="stat-label">${label.name}</div><div class="stat-value">${item.count}</div></div>`;
    }).join('');
    // 结果分布
    const resultCards = byResult.map(item => {
        const cls = item.op_result === 'ok' ? 'success' : (item.op_result === 'fail' ? 'danger' : '');
        return `<div class="stat-card ${cls}"><div class="stat-label">结果: ${escapeHtml(item.op_result || '-')}</div><div class="stat-value">${item.count}</div></div>`;
    }).join('');
    grid.innerHTML = (typeCards || '<div class="empty-state">暂无数据</div>') + (resultCards || '');
}

/* ============ 初始化 ============ */

(async function init() {
    // 点击遮罩关闭弹窗
    document.querySelectorAll('.modal-mask').forEach(mask => {
        mask.addEventListener('click', (e) => {
            if (e.target === mask) mask.classList.remove('show');
        });
    });
    // ESC 关闭弹窗
    document.addEventListener('keydown', (e) => {
        if (e.key === 'Escape') {
            document.querySelectorAll('.modal-mask.show').forEach(m => m.classList.remove('show'));
        }
    });

    const ok = await checkLogin();
    if (!ok) return;

    // 加载默认页（仪表盘）
    loadSection('dashboard');
    loaded.dashboard = true;
})();
