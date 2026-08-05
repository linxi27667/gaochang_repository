# 高昌举升机物联网平台增强实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为平台添加用户注册、设备删除、多型号支持、配件系统、蜂鸣器控制等功能，准备上市。

**Architecture:** 现有 Node.js + Express + SQLite + vanilla JS SPA 架构不变，新增功能遵循现有代码模式。后端新增路由和中间件，前端在现有单页应用中添加UI。

**Tech Stack:** Node.js, Express, SQLite (better-sqlite3), JWT, bcryptjs, MQTT, WebSocket, Vanilla JS

---

## 涉及文件

| 文件 | 职责 |
|------|------|
| `database.js` | 数据库schema定义、版本迁移 |
| `auth.js` | 用户认证、注册路由 |
| `rateLimit.js` | **新建** - IP限流中间件 |
| `devices.js` | 设备CRUD、级联删除 |
| `commands.js` | 蜂鸣器MQTT命令 |
| `logs.js` | 操作日志查询增强 |
| `mqtt-bridge.js` | 解析蜂鸣器状态 |
| `server.js` | 注册新路由 |
| `public/index.html` | 注册模态框、设备表单改动 |
| `public/app.js` | 所有前端UI改动 |
| `public/style.css` | 新增样式 |
| `public/i18n.js` | 新增翻译词条 |

---

### Task 1: 修复警报}号Bug

**Files:**
- Modify: `public/app.js:637`

- [ ] **Step 1: 修复缺失的 `</button>` 闭合标签**

在 `public/app.js` 第637行，将：
```javascript
${!a.resolved_at ? `<button class="btn btn-sm btn-success" onclick="resolveAlarm(${a.id})">${t('alarms.resolve')}</button>` : `<span style="font-size:12px;color:var(--success);">${t('alarms.resolved')}</span>`}
```
改为：
```javascript
${!a.resolved_at ? `<button class="btn btn-sm btn-success" onclick="resolveAlarm(${a.id})">${t('alarms.resolve')}</button>` : `<span style="font-size:12px;color:var(--success);">${t('alarms.resolved')}</span>`}
```

确认第637行的 `` ` `` 闭合前有 `</button>`。

- [ ] **Step 2: 提交**

```bash
git add public/app.js
git commit -m "fix: close button tag in alarm resolve rendering"
```

---

### Task 2: 数据库Schema更新

**Files:**
- Modify: `database.js`

- [ ] **Step 1: 在 devices 表定义中添加配件字段**

在 `database.js` 的 `CREATE TABLE IF NOT EXISTS devices` 语句中，在 `created_at` 之前添加4个布尔字段：

```sql
has_encoder INTEGER DEFAULT 0,
has_buzzer INTEGER DEFAULT 0,
has_pressure_sensor INTEGER DEFAULT 0,
has_display INTEGER DEFAULT 0,
```

- [ ] **Step 2: 在 device_status 表定义中添加蜂鸣器状态字段**

在 `CREATE TABLE IF NOT EXISTS device_status` 语句中，添加：

```sql
buzzer_on INTEGER DEFAULT 0,
```

- [ ] **Step 3: 添加数据库版本迁移逻辑**

在 `init()` 函数末尾、seed 数据之前，添加迁移代码处理已有数据库：

```javascript
// 版本迁移：添加新字段（如果不存在）
function addColumnIfNotExists(table, column, type) {
  const cols = db.prepare(`PRAGMA table_info(${table})`).all().map(c => c.name);
  if (!cols.includes(column)) {
    db.exec(`ALTER TABLE ${table} ADD COLUMN ${column} ${type}`);
    console.log(`[DB] Added ${table}.${column}`);
  }
}

addColumnIfNotExists('devices', 'has_encoder', 'INTEGER DEFAULT 0');
addColumnIfNotExists('devices', 'has_buzzer', 'INTEGER DEFAULT 0');
addColumnIfNotExists('devices', 'has_pressure_sensor', 'INTEGER DEFAULT 0');
addColumnIfNotExists('devices', 'has_display', 'INTEGER DEFAULT 0');
addColumnIfNotExists('device_status', 'buzzer_on', 'INTEGER DEFAULT 0');
```

- [ ] **Step 4: 提交**

```bash
git add database.js
git commit -m "feat: add device accessories and buzzer status fields to DB schema"
```

---

### Task 3: 后端 - IP限流中间件

**Files:**
- Create: `rateLimit.js`

- [ ] **Step 1: 创建 rateLimit.js**

```javascript
// IP限流中间件 - 内存Map存储，重启清零
const requests = new Map();

// 定期清理过期记录（每5分钟）
setInterval(() => {
  const now = Date.now();
  for (const [key, data] of requests) {
    if (now - data.start > data.windowMs) requests.delete(key);
  }
}, 5 * 60 * 1000);

function rateLimit({ windowMs = 60000, max = 3 } = {}) {
  return (req, res, next) => {
    const ip = req.ip || req.connection.remoteAddress || 'unknown';
    const key = `${ip}:${req.baseUrl}${req.path}`;
    const now = Date.now();

    let record = requests.get(key);
    if (!record || now - record.start > windowMs) {
      record = { start: now, count: 0 };
      requests.set(key, record);
    }

    record.count++;
    if (record.count > max) {
      return res.status(429).json({ error: '请求过于频繁，请稍后再试' });
    }
    next();
  };
}

module.exports = { rateLimit };
```

- [ ] **Step 2: 提交**

```bash
git add rateLimit.js
git commit -m "feat: add IP rate limiting middleware"
```

---

### Task 4: 后端 - 开放注册路由

**Files:**
- Modify: `auth.js`

- [ ] **Step 1: 在 auth.js 顶部导入 rateLimit**

在文件顶部添加：
```javascript
const { rateLimit } = require('./rateLimit');
```

- [ ] **Step 2: 添加公开注册路由**

在 `/register` 路由之后，添加：

```javascript
// 公开注册 - 无需登录，带IP限流和验证码
router.post('/register-public', rateLimit({ windowMs: 60000, max: 3 }), (req, res) => {
  const { username, password, real_name, phone, captcha_answer, captcha_expected } = req.body;

  // 参数校验
  if (!username || !password) {
    return res.status(400).json({ error: '用户名和密码不能为空' });
  }
  if (!/^[a-zA-Z0-9_]{3,20}$/.test(username)) {
    return res.status(400).json({ error: '用户名只能包含字母、数字、下划线，3-20位' });
  }
  if (password.length < 6) {
    return res.status(400).json({ error: '密码至少6位' });
  }

  // 验证码校验（前端生成数学题，提交答案）
  if (Number(captcha_answer) !== Number(captcha_expected)) {
    return res.status(400).json({ error: '验证码错误' });
  }

  const db = getDb();
  const existing = db.prepare('SELECT id FROM users WHERE username = ?').get(username);
  if (existing) {
    return res.status(409).json({ error: '用户名已存在' });
  }

  const hash = bcrypt.hashSync(password, 10);
  try {
    const result = db.prepare(
      'INSERT INTO users (username, password_hash, role, real_name, phone, created_at) VALUES (?, ?, ?, ?, ?, ?)'
    ).run(username, hash, 'viewer', real_name || '', phone || '', nowISO());

    // 记录操作日志
    db.prepare('INSERT INTO operation_logs (user_id, action, detail, result, created_at) VALUES (?, ?, ?, ?, ?)')
      .run(result.lastInsertRowid, '用户注册', `新用户注册: ${username}`, '成功', nowISO());

    res.json({ message: '注册成功', username });
  } catch (e) {
    res.status(500).json({ error: '注册失败' });
  }
});
```

- [ ] **Step 3: 提交**

```bash
git add auth.js rateLimit.js
git commit -m "feat: add public user registration with rate limiting"
```

---

### Task 5: 后端 - 设备级联删除

**Files:**
- Modify: `devices.js`

- [ ] **Step 1: 增强 DELETE 路由实现级联删除**

将现有的 `router.delete('/:id', ...)` 替换为：

```javascript
router.delete('/:id', authMiddleware, roleMiddleware('admin'), (req, res) => {
  const deviceId = req.params.id;
  const db = getDb();

  const device = db.prepare('SELECT device_id, name FROM devices WHERE device_id = ?').get(deviceId);
  if (!device) return res.status(404).json({ error: '设备不存在' });

  // 级联删除：事务确保原子性
  const deleteAll = db.transaction(() => {
    db.prepare('DELETE FROM device_status WHERE device_id = ?').run(deviceId);
    db.prepare('DELETE FROM alarms WHERE device_id = ?').run(deviceId);
    db.prepare('DELETE FROM maintenance_records WHERE device_id = ?').run(deviceId);
    db.prepare('DELETE FROM operation_logs WHERE device_id = ?').run(deviceId);
    db.prepare('DELETE FROM command_queue WHERE device_id = ?').run(deviceId);
    db.prepare('DELETE FROM devices WHERE device_id = ?').run(deviceId);
  });

  try {
    deleteAll();
    // 记录删除操作日志
    db.prepare('INSERT INTO operation_logs (user_id, action, device_id, detail, result, created_at) VALUES (?, ?, ?, ?, ?, ?)')
      .run(req.user.id, '删除设备', deviceId, `删除设备 ${device.name}`, '成功', nowISO());
    res.json({ message: '设备已删除' });
  } catch (e) {
    res.status(500).json({ error: '删除失败' });
  }
});
```

- [ ] **Step 2: 提交**

```bash
git add devices.js
git commit -m "feat: cascade delete device with all related data"
```

---

### Task 6: 后端 - 蜂鸣器MQTT命令

**Files:**
- Modify: `commands.js`

- [ ] **Step 1: 添加蜂鸣器开启命令路由**

在 `commands.js` 的 `router.post('/rename/:deviceId', ...)` 之前添加：

```javascript
// 开启蜂鸣器
router.post('/buzzer_on/:deviceId', authMiddleware, roleMiddleware('admin', 'operator'), (req, res) => {
  const { deviceId } = req.params;
  const msgId = createMsgId();

  const db = getDb();
  const device = db.prepare('SELECT device_id, name FROM devices WHERE device_id = ?').get(deviceId);
  if (!device) return res.status(404).json({ error: '设备不存在' });

  const sent = enqueueAndSendCommand(db, deviceId, 'buzzer_on', msgId);

  db.prepare('INSERT INTO operation_logs (user_id, action, device_id, detail, result, created_at) VALUES (?, ?, ?, ?, ?, ?)')
    .run(req.user.id, '开启蜂鸣器', deviceId, `开启蜂鸣器 ${device.name}`, sent ? '已发送' : '发送失败', nowISO());

  if (!sent) return res.status(503).json({ error: 'MQTT未连接，命令发送失败', msg_id: msgId });

  db.prepare('UPDATE device_status SET buzzer_on = 1 WHERE device_id = ?').run(deviceId);
  res.json({ message: '蜂鸣器开启命令已发送', msg_id: msgId });
});

// 关闭蜂鸣器
router.post('/buzzer_off/:deviceId', authMiddleware, roleMiddleware('admin', 'operator'), (req, res) => {
  const { deviceId } = req.params;
  const msgId = createMsgId();

  const db = getDb();
  const device = db.prepare('SELECT device_id, name FROM devices WHERE device_id = ?').get(deviceId);
  if (!device) return res.status(404).json({ error: '设备不存在' });

  const sent = enqueueAndSendCommand(db, deviceId, 'buzzer_off', msgId);

  db.prepare('INSERT INTO operation_logs (user_id, action, device_id, detail, result, created_at) VALUES (?, ?, ?, ?, ?, ?)')
    .run(req.user.id, '关闭蜂鸣器', deviceId, `关闭蜂鸣器 ${device.name}`, sent ? '已发送' : '发送失败', nowISO());

  if (!sent) return res.status(503).json({ error: 'MQTT未连接，命令发送失败', msg_id: msgId });

  db.prepare('UPDATE device_status SET buzzer_on = 0 WHERE device_id = ?').run(deviceId);
  res.json({ message: '蜂鸣器关闭命令已发送', msg_id: msgId });
});
```

- [ ] **Step 2: 提交**

```bash
git add commands.js
git commit -m "feat: add buzzer on/off MQTT commands"
```

---

### Task 7: 后端 - 日志筛选增强

**Files:**
- Modify: `logs.js`

- [ ] **Step 1: 增强 GET /api/logs 路由支持用户筛选**

将现有的 `router.get('/', ...)` 替换为：

```javascript
router.get('/', authMiddleware, (req, res) => {
  const db = getDb();
  const { device_id, action, user_id, start_date, end_date, limit, offset } = req.query;

  let sql = `SELECT l.id, l.action, l.detail, l.result, l.created_at,
                    l.device_id, u.username, u.real_name
             FROM operation_logs l
             LEFT JOIN users u ON l.user_id = u.id
             WHERE 1=1`;
  const params = [];

  if (device_id) { sql += ' AND l.device_id = ?'; params.push(device_id); }
  if (user_id) { sql += ' AND l.user_id = ?'; params.push(parseInt(user_id)); }
  if (action) { sql += ' AND l.action LIKE ?'; params.push(`%${action}%`); }
  if (start_date) { sql += ' AND l.created_at >= ?'; params.push(start_date); }
  if (end_date) { sql += " AND l.created_at <= ?"; params.push(end_date + ' 23:59:59'); }

  sql += ' ORDER BY l.id DESC';

  const total = db.prepare(`SELECT COUNT(*) AS cnt FROM (${sql})`).get(...params).cnt;

  const lim = Math.min(parseInt(limit) || 100, 500);
  const off = parseInt(offset) || 0;
  sql += ' LIMIT ? OFFSET ?';
  params.push(lim, off);

  const logs = db.prepare(sql).all(...params);
  res.json({ total, logs });
});
```

- [ ] **Step 2: 提交**

```bash
git add logs.js
git commit -m "feat: add user_id filter to operation logs API"
```

---

### Task 8: 后端 - 设备CRUD支持配件字段

**Files:**
- Modify: `devices.js`

- [ ] **Step 1: 更新 GET / 路由返回配件字段**

在 `router.get('/', ...)` 的 SQL 查询中，`d.location` 后添加：
```sql
d.has_encoder, d.has_buzzer, d.has_pressure_sensor, d.has_display,
```

在返回对象映射中添加：
```javascript
has_encoder: !!d.has_encoder,
has_buzzer: !!d.has_buzzer,
has_pressure_sensor: !!d.has_pressure_sensor,
has_display: !!d.has_display,
```

- [ ] **Step 2: 更新 GET /:id 路由返回配件字段**

同样在 `router.get('/:id', ...)` 的 SQL 和返回对象中添加配件字段。

- [ ] **Step 3: 更新 POST / 路由支持配件字段**

```javascript
router.post('/', authMiddleware, roleMiddleware('admin'), (req, res) => {
  const { device_id, name, model, group_name, location, has_encoder, has_buzzer, has_pressure_sensor, has_display } = req.body;
  if (!device_id || !name) {
    return res.status(400).json({ error: '设备ID和名称不能为空' });
  }
  const db = getDb();
  try {
    db.prepare('INSERT INTO devices (device_id, name, model, group_name, location, has_encoder, has_buzzer, has_pressure_sensor, has_display) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)')
      .run(device_id, name, model || 'GC-4.0sle', group_name || '默认分组', location || '', has_encoder ? 1 : 0, has_buzzer ? 1 : 0, has_pressure_sensor ? 1 : 0, has_display ? 1 : 0);
    db.prepare('INSERT INTO device_status (device_id) VALUES (?)').run(device_id);
    res.json({ message: '设备添加成功', device_id });
  } catch (e) {
    if (e.message.includes('UNIQUE')) {
      return res.status(409).json({ error: '设备ID已存在' });
    }
    res.status(500).json({ error: '添加失败' });
  }
});
```

- [ ] **Step 4: 更新 PUT /:id 路由支持配件字段**

```javascript
router.put('/:id', authMiddleware, roleMiddleware('admin'), (req, res) => {
  const { name, model, group_name, location, has_encoder, has_buzzer, has_pressure_sensor, has_display } = req.body;
  const db = getDb();
  const result = db.prepare('UPDATE devices SET name = ?, model = ?, group_name = ?, location = ?, has_encoder = ?, has_buzzer = ?, has_pressure_sensor = ?, has_display = ? WHERE device_id = ?')
    .run(name, model, group_name, location, has_encoder ? 1 : 0, has_buzzer ? 1 : 0, has_pressure_sensor ? 1 : 0, has_display ? 1 : 0, req.params.id);
  if (result.changes === 0) return res.status(404).json({ error: '设备不存在' });
  res.json({ message: '更新成功' });
});
```

- [ ] **Step 5: 提交**

```bash
git add devices.js
git commit -m "feat: support device accessories fields in CRUD"
```

---

### Task 9: 后端 - MQTT解析蜂鸣器状态

**Files:**
- Modify: `mqtt-bridge.js`

- [ ] **Step 1: 在 normalizeTelemetry 函数中添加 buzzer_on 字段**

在 `mqtt-bridge.js` 的 `normalizeTelemetry` 函数中，状态字段解析部分添加：

```javascript
if (data.buzzer_on !== undefined) result.buzzer_on = data.buzzer_on ? 1 : 0;
```

- [ ] **Step 2: 在数据库更新SQL中添加 buzzer_on**

在 `updateDeviceStatus` 函数（或等效的设备状态更新逻辑）的 SQL 中，添加 `buzzer_on` 字段到 UPDATE 语句。

- [ ] **Step 3: 提交**

```bash
git add mqtt-bridge.js
git commit -m "feat: parse buzzer_on status from MQTT telemetry"
```

---

### Task 10: 前端 - 注册功能UI

**Files:**
- Modify: `public/index.html`
- Modify: `public/app.js`
- Modify: `public/style.css`

- [ ] **Step 1: 在 index.html 登录表单后添加注册链接**

在 `</form>` 和 `<div class="login-footer">` 之间添加：

```html
<div class="register-link">
    <a href="#" onclick="showRegisterModal();return false;" data-i18n="login.registerLink">注册账号</a>
</div>
```

- [ ] **Step 2: 在 index.html 中添加注册模态框**

在 `</body>` 之前添加：

```html
<!-- 注册模态框 -->
<div id="register-modal" class="modal">
  <div class="modal-content" style="max-width:400px;">
    <div class="modal-header">
      <h3 data-i18n="register.title">注册账号</h3>
      <button class="modal-close" onclick="closeModal('register-modal')">&times;</button>
    </div>
    <div id="register-modal-body"></div>
  </div>
</div>
```

- [ ] **Step 3: 在 app.js 中添加注册相关函数**

```javascript
// 生成数学验证码
function generateCaptcha() {
  const a = Math.floor(Math.random() * 10) + 1;
  const b = Math.floor(Math.random() * 10) + 1;
  return { a, b, answer: a + b, question: `${a} + ${b} = ?` };
}

let currentCaptcha = generateCaptcha();

// 显示注册模态框
function showRegisterModal() {
  currentCaptcha = generateCaptcha();
  document.getElementById('register-modal-body').innerHTML = `
    <form onsubmit="return handleRegister(event)">
      <div class="form-group">
        <label>${t('register.username')}</label>
        <input type="text" id="reg-username" placeholder="${t('register.usernamePh')}" required pattern="[a-zA-Z0-9_]{3,20}">
      </div>
      <div class="form-group">
        <label>${t('register.password')}</label>
        <input type="password" id="reg-password" placeholder="${t('register.passwordPh')}" required minlength="6">
      </div>
      <div class="form-group">
        <label>${t('register.confirmPassword')}</label>
        <input type="password" id="reg-password2" placeholder="${t('register.confirmPasswordPh')}" required>
      </div>
      <div class="form-group">
        <label>${t('register.realName')} (${t('register.optional')})</label>
        <input type="text" id="reg-realname" placeholder="${t('register.realNamePh')}">
      </div>
      <div class="form-group">
        <label>${t('register.phone')} (${t('register.optional')})</label>
        <input type="text" id="reg-phone" placeholder="${t('register.phonePh')}">
      </div>
      <div class="form-group">
        <label>${t('register.captcha')}: <b>${currentCaptcha.question}</b></label>
        <input type="number" id="reg-captcha" placeholder="${t('register.captchaPh')}" required>
      </div>
      <div id="register-error" class="error-msg" style="display:none;"></div>
      <div style="display:flex;gap:8px;margin-top:16px;">
        <button type="submit" class="btn btn-primary" id="reg-btn">${t('register.submit')}</button>
        <button type="button" class="btn btn-outline" onclick="closeModal('register-modal')">${t('common.cancel')}</button>
      </div>
    </form>`;
  document.getElementById('register-modal').classList.add('active');
}

// 处理注册提交
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
        phone: document.getElementById('reg-phone').value.trim(),
        captcha_answer: parseInt(document.getElementById('reg-captcha').value),
        captcha_expected: currentCaptcha.answer
      })
    });
    const data = await res.json();
    if (!res.ok) throw new Error(data.error || t('register.failed'));

    closeModal('register-modal');
    showToast(t('register.success'), 'success');
  } catch (err) {
    errEl.textContent = err.message;
    errEl.style.display = 'block';
    currentCaptcha = generateCaptcha();
    showRegisterModal();
  } finally {
    btn.disabled = false;
    btn.textContent = t('register.submit');
  }
  return false;
}
```

- [ ] **Step 4: 在 style.css 中添加注册链接样式**

```css
.register-link { text-align: center; margin-top: 12px; }
.register-link a { color: var(--primary); text-decoration: none; font-size: 13px; }
.register-link a:hover { text-decoration: underline; }
```

- [ ] **Step 5: 提交**

```bash
git add public/index.html public/app.js public/style.css
git commit -m "feat: add public user registration UI with captcha"
```

---

### Task 11: 前端 - 设备管理增强（删除、型号、配件）

**Files:**
- Modify: `public/app.js`

- [ ] **Step 1: 在 app.js 顶部添加型号常量**

在文件顶部常量区域添加：

```javascript
// 举升机型号列表
const LIFT_MODELS = ['GC-4.0sle', 'GC-4.0sb', 'GC-4.0MSL', 'GC-4.0PRO-DW'];
```

- [ ] **Step 2: 修改 showAddDeviceModal 函数**

将 model 的 `<input>` 改为 `<select>`，添加配件复选框：

```javascript
async function showAddDeviceModal() {
  const modelOptions = LIFT_MODELS.map(m => `<option value="${m}">${m}</option>`).join('');
  document.getElementById('maintenance-modal-title').textContent = t('devices.addDevice');
  document.getElementById('maintenance-modal-body').innerHTML = `
    <form onsubmit="return addDevice(event)">
      <div class="form-group"><label>${t('devices.deviceId')}</label><input type="text" id="add-device-id" placeholder="${t('devices.idPlaceholder')}" required></div>
      <div class="form-group"><label>${t('devices.deviceName')}</label><input type="text" id="add-device-name" placeholder="${t('devices.namePlaceholder')}" required></div>
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
```

- [ ] **Step 3: 修改 addDevice 函数**

```javascript
async function addDevice(e) {
  e.preventDefault();
  try {
    await api('/devices', {
      method: 'POST',
      body: JSON.stringify({
        device_id: document.getElementById('add-device-id').value.trim(),
        name: document.getElementById('add-device-name').value.trim(),
        model: document.getElementById('add-device-model').value,
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
```

- [ ] **Step 4: 在设备管理页添加删除按钮**

在 `renderDevices()` 函数中，操作列添加删除按钮。找到操作列的按钮区域，添加：

```javascript
${isAdmin ? `<button class="btn btn-sm btn-danger" onclick="deleteDevice('${d.device_id}', '${(d.name||'').replace(/'/g,"\\'")}')">${t('common.delete')}</button>` : ''}
```

- [ ] **Step 5: 添加 deleteDevice 函数**

```javascript
// 删除设备（二次确认）
async function deleteDevice(deviceId, deviceName) {
  if (!confirm(t('devices.deleteConfirm', { name: deviceName }))) return;
  try {
    await api(`/devices/${deviceId}`, { method: 'DELETE' });
    showToast(t('devices.deleteSuccess'), 'success');
    fetchDevices();
  } catch (err) { showToast(err.message, 'error'); }
}
```

- [ ] **Step 6: 在设备详情页根据配件显示/隐藏面板**

在 `showDeviceDetail` 函数中，高度数据区域用 `d.has_encoder` 包裹：

```javascript
${d.has_encoder ? `
  <div style="margin-bottom:16px;">
    <div style="font-size:13px;font-weight:600;margin-bottom:8px;">${t('common.heightData')}</div>
    <div style="font-size:12px;color:var(--text-muted);margin-bottom:4px;">${t('devices.heightLeft')}: ${d.height_left_mm || 0}mm</div>
    <div class="height-bar" style="height:10px;"><div class="height-bar-fill" style="width:${leftPct}%;background:var(--primary);"></div></div>
    <div style="font-size:12px;color:var(--text-muted);margin:8px 0 4px;">${t('devices.heightRight')}: ${d.height_right_mm || 0}mm</div>
    <div class="height-bar" style="height:10px;"><div class="height-bar-fill" style="width:${rightPct}%;background:var(--success);"></div></div>
    <div style="font-size:12px;color:var(--text-muted);margin-top:8px;">${t('devices.heightDiff')}: <b>${d.height_diff_mm || 0}mm</b></div>
  </div>
` : ''}
```

- [ ] **Step 7: 在设备详情页添加蜂鸣器控制**

在操作按钮区域添加蜂鸣器控制（仅当 has_buzzer=true 时显示）：

```javascript
${d.has_buzzer && isOperator ? `
  ${d.buzzer_on
    ? `<button class="btn btn-warning" onclick="sendBuzzerCmd('${d.device_id}', 'buzzer_off');closeModal('device-modal')">${t('devices.buzzerOff')}</button>`
    : `<button class="btn btn-success" onclick="sendBuzzerCmd('${d.device_id}', 'buzzer_on');closeModal('device-modal')">${t('devices.buzzerOn')}</button>`
  }
` : ''}
```

- [ ] **Step 8: 添加 sendBuzzerCmd 函数**

```javascript
// 发送蜂鸣器控制命令
async function sendBuzzerCmd(deviceId, cmd) {
  try {
    await api(`/commands/${cmd}/${deviceId}`, { method: 'POST' });
    showToast(t(`devices.${cmd === 'buzzer_on' ? 'buzzerOnSent' : 'buzzerOffSent'}`), 'success');
  } catch (err) { showToast(err.message, 'error'); }
}
```

- [ ] **Step 9: 在设备卡片上显示配件图标**

在设备卡片渲染中，设备型号下方添加配件图标：

```javascript
<div class="device-accessories">
  ${d.has_encoder ? '<span class="accessory-tag" title="高度编码器">📏</span>' : ''}
  ${d.has_buzzer ? '<span class="accessory-tag" title="蜂鸣器">🔔</span>' : ''}
  ${d.has_pressure_sensor ? '<span class="accessory-tag" title="压力传感器">📊</span>' : ''}
  ${d.has_display ? '<span class="accessory-tag" title="显示屏">🖥️</span>' : ''}
</div>
```

- [ ] **Step 10: 在 style.css 中添加配件相关样式**

```css
.accessory-checkboxes { display: flex; flex-wrap: wrap; gap: 12px; margin-top: 8px; }
.checkbox-label { display: flex; align-items: center; gap: 6px; font-size: 13px; cursor: pointer; }
.checkbox-label input[type="checkbox"] { width: 16px; height: 16px; cursor: pointer; }
.accessory-tags { display: flex; gap: 4px; margin-top: 4px; }
.accessory-tag { font-size: 14px; }
```

- [ ] **Step 11: 提交**

```bash
git add public/app.js public/style.css
git commit -m "feat: device management with delete, model selection, and accessories"
```

---

### Task 12: 前端 - 日志筛选UI

**Files:**
- Modify: `public/app.js`

- [ ] **Step 1: 修改 renderLogs 函数添加筛选栏**

在日志页面的 `filter-bar` 中添加用户筛选下拉框和时间范围选择：

```javascript
function renderLogs() {
  return `
    <div class="filter-bar">
      <div class="filter-group">
        <label>${t('logs.filterUser')}</label>
        <select id="log-filter-user" onchange="loadLogs()">
          <option value="">${t('logs.allUsers')}</option>
        </select>
      </div>
      <div class="filter-group">
        <label>${t('logs.filterDevice')}</label>
        <select id="log-filter-device" onchange="loadLogs()">
          <option value="">${t('logs.allDevices')}</option>
        </select>
      </div>
      <div class="filter-group">
        <label>${t('logs.filterStart')}</label>
        <input type="date" id="log-filter-start" onchange="loadLogs()">
      </div>
      <div class="filter-group">
        <label>${t('logs.filterEnd')}</label>
        <input type="date" id="log-filter-end" onchange="loadLogs()">
      </div>
    </div>
    <div id="logs-list"></div>`;
}
```

- [ ] **Step 2: 修改 loadLogs 函数传递筛选参数**

```javascript
async function loadLogs() {
  try {
    const userId = document.getElementById('log-filter-user')?.value || '';
    const deviceId = document.getElementById('log-filter-device')?.value || '';
    const startDate = document.getElementById('log-filter-start')?.value || '';
    const endDate = document.getElementById('log-filter-end')?.value || '';

    const params = new URLSearchParams();
    if (userId) params.set('user_id', userId);
    if (deviceId) params.set('device_id', deviceId);
    if (startDate) params.set('start_date', startDate);
    if (endDate) params.set('end_date', endDate);

    const data = await api(`/logs?${params.toString()}`);
    // ... 渲染日志列表
  } catch (err) { showToast(err.message, 'error'); }
}
```

- [ ] **Step 3: 在 loadLogs 中填充筛选下拉框**

```javascript
// 填充用户筛选下拉框
async function loadLogFilterOptions() {
  try {
    const users = await api('/auth/users');
    const userSelect = document.getElementById('log-filter-user');
    if (userSelect && users.length) {
      users.forEach(u => {
        const opt = document.createElement('option');
        opt.value = u.id;
        opt.textContent = u.real_name || u.username;
        userSelect.appendChild(opt);
      });
    }

    const deviceList = await api('/devices');
    const deviceSelect = document.getElementById('log-filter-device');
    if (deviceSelect && deviceList.length) {
      deviceList.forEach(d => {
        const opt = document.createElement('option');
        opt.value = d.device_id;
        opt.textContent = d.name || d.device_id;
        deviceSelect.appendChild(opt);
      });
    }
  } catch (e) { /* ignore */ }
}
```

在 `showApp()` 函数中调用 `loadLogFilterOptions()`。

- [ ] **Step 4: 提交**

```bash
git add public/app.js
git commit -m "feat: add user and date filters to operation logs page"
```

---

### Task 13: 国际化翻译

**Files:**
- Modify: `public/i18n.js`

- [ ] **Step 1: 在 i18n.js 中添加所有新词条**

在中文翻译对象中添加：

```javascript
// 注册
'login.registerLink': '注册账号',
'register.title': '注册账号',
'register.username': '用户名',
'register.usernamePh': '字母、数字、下划线，3-20位',
'register.password': '密码',
'register.passwordPh': '至少6位',
'register.confirmPassword': '确认密码',
'register.confirmPasswordPh': '再次输入密码',
'register.realName': '真实姓名',
'register.realNamePh': '选填',
'register.phone': '手机号',
'register.phonePh': '选填',
'register.captcha': '验证码',
'register.captchaPh': '请输入计算结果',
'register.optional': '选填',
'register.submit': '注册',
'register.submitting': '注册中...',
'register.success': '注册成功，请登录',
'register.failed': '注册失败',
'register.passwordMismatch': '两次密码不一致',

// 设备配件
'devices.accessories': '设备配件',
'devices.encoder': '高度编码器',
'devices.buzzer': '蜂鸣器',
'devices.pressureSensor': '压力传感器',
'devices.display': '显示屏',
'devices.buzzerOn': '开启蜂鸣器',
'devices.buzzerOff': '关闭蜂鸣器',
'devices.buzzerOnSent': '蜂鸣器开启命令已发送',
'devices.buzzerOffSent': '蜂鸣器关闭命令已发送',
'devices.deleteConfirm': '确定要删除设备 {name} 吗？删除后数据不可恢复。',
'devices.deleteSuccess': '设备已删除',

// 日志筛选
'logs.filterUser': '操作用户',
'logs.allUsers': '所有用户',
'logs.filterDevice': '设备',
'logs.allDevices': '所有设备',
'logs.filterStart': '开始日期',
'logs.filterEnd': '结束日期',

// 通用
'common.delete': '删除',
```

为其他9种语言添加对应的翻译词条。

- [ ] **Step 2: 提交**

```bash
git add public/i18n.js
git commit -m "feat: add i18n translations for new features"
```

---

### Task 14: 测试验证

- [ ] **Step 1: 启动服务器验证数据库迁移**

```bash
cd web_monitor && node server.js
```

检查控制台输出确认迁移成功。

- [ ] **Step 2: 测试开放注册**

访问登录页，点击"注册账号"，填写表单提交，验证：
- 验证码校验生效
- IP限流生效（连续注册4次应被拒绝）
- 注册成功后可在登录页登录
- 新用户默认为 viewer 角色

- [ ] **Step 3: 测试设备删除**

以admin登录，在设备管理页删除设备，验证：
- 确认弹窗显示
- 设备及其关联数据被删除
- 操作日志记录删除操作

- [ ] **Step 4: 测试设备添加**

添加新设备，验证：
- 型号下拉框显示4个选项
- 配件复选框可勾选
- 添加成功后设备卡片显示配件图标

- [ ] **Step 5: 测试蜂鸣器控制**

在设备详情页验证：
- 仅当 has_buzzer=true 时显示控制按钮
- 点击按钮发送MQTT命令

- [ ] **Step 6: 测试日志筛选**

在操作日志页验证：
- 用户下拉框可筛选
- 日期范围可筛选
- 筛选结果正确

- [ ] **Step 7: 提交最终版本**

```bash
git add -A
git commit -m "feat: platform enhancement ready for launch"
```
