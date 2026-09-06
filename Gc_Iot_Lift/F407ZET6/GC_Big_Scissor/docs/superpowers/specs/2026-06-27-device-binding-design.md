# 举升机设备绑定系统设计

## 1. 当前状态分析

### 1.1 架构概览

```
┌──────────────┐    RS485/UART    ┌──────────────┐    LTE/4G     ┌──────────────┐
│  STM32F407   │ ──────────────→ │  TAS-LTE-892D│ ───────────→ │  MQTT Broker  │
│  (固件)       │   JSON报文      │  (4G DTU)     │   MQTT协议   │  阿里云       │
└──────────────┘                 └──────────────┘              └──────┬───────┘
                                                                      │
                                                               ┌──────▼───────┐
                                                               │  Node.js后端  │
                                                               │  (mqtt-bridge)│
                                                               └──────┬───────┘
                                                                      │
                                                               ┌──────▼───────┐
                                                               │  网页前端     │
                                                               │  (SPA)        │
                                                               └──────────────┘
```

### 1.2 关键代码现状

**固件端** (`APP/Inc/app_lift_iot.h`):
```c
#define LIFT_IOT_DEVICE_ID   "gaochang_lift_f407zet6"   // 硬编码，不可变
#define LIFT_IOT_DEVICE_MODEL "GC-F407-2POST"           // 硬编码
```

**MQTT消息** (`APP/Src/app_lift_iot.c:311`):
```json
{
  "type": "telemetry",
  "device": "gaochang_lift_f407zet6",   // 来自 #define
  "name": "Gaochang-Lift-F407-01",
  "model": "GC-F407-2POST",
  ...
}
```

**服务端自动注册** (`web_monitor/mqtt-bridge.js:296`):
```javascript
// 任何设备连上MQTT就自动创建记录，无归属概念
if (!device) {
  db.prepare('INSERT OR IGNORE INTO devices ...').run(deviceId, ...);
}
```

**数据库** (`web_monitor/database.js:31`):
```sql
CREATE TABLE devices (
  device_id TEXT PRIMARY KEY,   -- 直接用固件的 device 字段
  name TEXT NOT NULL,
  model TEXT DEFAULT 'TL-5000',
  -- 无 owner_id，无绑定状态
);
```

### 1.3 核心问题

| 问题 | 影响 |
|------|------|
| `device_id` 是 `#define` 硬编码 | 不同设备必须编译不同固件 |
| 无绑定/归属概念 | 任何设备连上就能被所有人看到 |
| 自动注册无门槛 | 无法区分合法设备和伪装设备 |
| MQTT 无认证 | 空用户名/密码，任何人可发布消息 |
| 无多设备支持 | `commandTopicFor()` 只有一个特殊映射 |
| `devices` 表无 `owner_id` | 无法关联设备和用户 |

---

## 2. 设计目标

1. 用户输入出厂编号即可绑定设备到自己的账号
2. 一个用户可绑定多台设备（一对多）
3. 只有绑定者能看到设备数据和发送命令
4. 未绑定设备的数据被接收但不展示
5. 支持解绑和重新绑定
6. 固件改动最小化

---

## 3. 方案：芯片UID + 出厂编号映射

### 3.1 核心思路

```
出厂编号 (用户输入)  ←→  芯片UID (固件上报)  ←→  设备记录 (数据库)
     "GC-2026-00001"        "4A003258534B501620383552"     devices表
```

- **芯片UID**：STM32F407 内置 96-bit 唯一ID，通过 `HAL_GetUID()` 读取，零成本，不可伪造
- **出厂编号**：工厂生产时生成的人类可读编号，贴在设备外壳
- **映射关系**：工厂录入到服务端 `device_registry` 表

### 3.2 数据流

```
                    工厂生产阶段
                    ────────────
  测试台读取UID ──→ 生成出厂编号 ──→ 写入 device_registry 表
  4A003258...       GC-2026-00001   {serial, uid, model, status=unbound}
                                      │
                                      ↓ 标签打印，贴在设备外壳
                    ────────────
                    用户绑定阶段
                    ────────────
  用户输入出厂编号 ──→ 服务端查找 registry ──→ 找到UID ──→ 绑定到用户账号
  "GC-2026-00001"     serial→uid映射        4A0032...    devices.owner_id = user.id
                                      │
                                      ↓
                    ────────────
                    设备运行阶段
                    ────────────
  固件上报含UID的JSON ──→ mqtt-bridge提取UID ──→ 查devices表 ──→ 更新状态
  {"uid":"4A0032...",...}   data.uid            device记录     写入device_status
                                      │
                                      ↓
                    ────────────
                    前端展示阶段
                    ────────────
  前端请求设备列表 ──→ 服务端过滤 owner_id ──→ 只返回用户绑定的设备
  GET /api/devices    WHERE owner_id=?       JSON响应
```

---

## 4. 数据库设计

### 4.1 新增表：`device_registry`（设备注册表）

工厂预录入的设备清单，独立于运行时的 `devices` 表。

```sql
CREATE TABLE IF NOT EXISTS device_registry (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  serial TEXT UNIQUE NOT NULL,           -- 出厂编号 (用户输入)
  uid TEXT UNIQUE NOT NULL,              -- STM32芯片UID (24位HEX)
  model TEXT NOT NULL DEFAULT '',        -- 设备型号
  batch TEXT DEFAULT '',                 -- 生产批次
  produced_at TEXT DEFAULT '',           -- 生产日期
  status TEXT DEFAULT 'unbound',         -- unbound / bound
  bound_device_id TEXT DEFAULT '',       -- 绑定后对应的 device_id
  created_at TEXT NOT NULL DEFAULT ''
);

CREATE INDEX IF NOT EXISTS idx_registry_uid ON device_registry(uid);
CREATE INDEX IF NOT EXISTS idx_registry_serial ON device_registry(serial);
```

### 4.2 修改表：`devices`（设备表）

```sql
-- 新增字段
ALTER TABLE devices ADD COLUMN owner_id INTEGER DEFAULT NULL;
ALTER TABLE devices ADD COLUMN uid TEXT DEFAULT '';
ALTER TABLE devices ADD COLUMN bind_status TEXT DEFAULT 'unbound';
ALTER TABLE devices ADD COLUMN bound_at TEXT DEFAULT '';

-- owner_id 外键
-- FOREIGN KEY (owner_id) REFERENCES users(id) ON DELETE SET NULL
```

### 4.3 字段说明

| 字段 | 类型 | 说明 |
|------|------|------|
| `owner_id` | INTEGER | 绑定的用户ID，NULL 表示未绑定 |
| `uid` | TEXT | STM32芯片UID，由固件上报 |
| `bind_status` | TEXT | `unbound` / `bound` |
| `bound_at` | TEXT | 绑定时间 |

### 4.4 ER 关系图

```
device_registry (工厂预录入)
┌──────────────────────────────────┐
│ id        │ serial    │ uid      │
│ model     │ batch     │ status   │
│ bound_device_id       │ produced_at │
└──────────────────────────────────┘
         │ serial (用户输入)
         │ uid (固件上报)
         ↓
devices (运行时设备)
┌──────────────────────────────────┐
│ device_id │ name │ model │ uid   │
│ owner_id ─┼──→ users.id          │
│ bind_status │ bound_at           │
│ group_name │ location │ ...      │
└──────────────────────────────────┘
         │
         ↓
device_status (实时状态)
┌──────────────────────────────────┐
│ device_id │ online │ locked │ ...│
└──────────────────────────────────┘
```

---

## 5. 固件改动

### 5.1 读取芯片UID

STM32F407 的 UID 基地址为 `0x1FFF7A10`，共 12 字节（96 位）。

**新增文件/修改**：`APP/Inc/app_lift_iot.h`

```c
// 删除旧的硬编码 device_id
// #define LIFT_IOT_DEVICE_ID   "gaochang_lift_f407zet6"   ← 删除

// 保留型号定义（不同型号固件仍然不同）
#define LIFT_IOT_DEVICE_MODEL       "GC-F407-2POST"

// UID 相关
#define STM32_UID_ADDR              0x1FFF7A10U
#define STM32_UID_LEN               12U

// 运行时变量（替代 #define）
extern char g_device_uid[25];       // 24位HEX + '\0'
extern char g_device_id[48];        // 可选：从UID派生的device_id
```

**新增函数**：读取 UID 并转为 HEX 字符串

```c
// APP/Src/app_lift_iot.c
void App_LiftIot_ReadUID(void)
{
    uint32_t uid0 = *(volatile uint32_t *)(STM32_UID_ADDR);
    uint32_t uid1 = *(volatile uint32_t *)(STM32_UID_ADDR + 4);
    uint32_t uid2 = *(volatile uint32_t *)(STM32_UID_ADDR + 8);

    snprintf(g_device_uid, sizeof(g_device_uid),
             "%08lX%08lX%08lX",
             (unsigned long)uid2,
             (unsigned long)uid1,
             (unsigned long)uid0);

    // 可选：从UID派生 device_id，保持向后兼容
    snprintf(g_device_id, sizeof(g_device_id), "lift_%s", g_device_uid);
}
```

### 5.2 修改MQTT消息格式

**修改**：`App_LiftIot_BuildTelemetryJson()` (`APP/Src/app_lift_iot.c:309`)

```c
// 原来:
// "device\":\"%s\"     ← LIFT_IOT_DEVICE_ID (硬编码)
// 改为:
// "device\":\"%s\"     ← g_device_id (运行时)
// "uid\":\"%s\"        ← g_device_uid (新增)

len = snprintf(buf, size,
    "{\"type\":\"%s\",\"device\":\"%s\",\"uid\":\"%s\",\"name\":\"%s\","
    "\"model\":\"%s\",\"group\":\"%s\","
    // ... 其余不变
    type,
    g_device_id,        // 替代 LIFT_IOT_DEVICE_ID
    g_device_uid,       // 新增：芯片UID
    LIFT_IOT_DEVICE_NAME,
    // ... 其余不变
);
```

同样修改 `BuildStatusJson()` 和 `BuildCommandStatusJson()`。

### 5.3 修改DTU配置

**修改**：`APP/Inc/app_tas_dtu.h`

```c
// MQTT topic 改为动态生成
// 原来: #define TAS_DTU_TOPIC_TELEMETRY  "gaochang/lift/f407zet6/telemetry"
// 改为: 在运行时拼接

// Client ID 也改为动态
// 原来: #define TAS_DTU_CLIENT_ID  "gaochang_lift_f407zet6_dtu"
// 改为: 运行时用 UID 派生
```

**修改**：`APP/Src/app_tas_dtu.c` 的 `App_TasDtu_ConfigMqttChannel()`

```c
// 运行时拼接 topic 和 client_id
static char topic_telemetry[64];
static char topic_command[64];
static char client_id[64];

snprintf(topic_telemetry, sizeof(topic_telemetry),
         "gaochang/lift/%s/telemetry", g_device_uid);
snprintf(topic_command, sizeof(topic_command),
         "gaochang/lift/%s/command", g_device_uid);
snprintf(client_id, sizeof(client_id),
         "lift_%s_dtu", g_device_uid);

// 使用变量替代 #define
App_TasDtu_SendAt("AT+CLIENTID", "\"%s\",1", client_id);
App_TasDtu_SendAt("AT+MQTTPUB", "1,\"%s\",0,0,1,1", topic_telemetry);
// ...
```

### 5.4 固件改动总结

| 改动 | 文件 | 工作量 |
|------|------|--------|
| 读取芯片UID | `app_lift_iot.c` | 新增 ~15 行 |
| UID全局变量 | `app_lift_iot.h` | 修改 ~5 行 |
| 消息加uid字段 | `app_lift_iot.c` (3个Build函数) | 修改 ~6 行 |
| DTU topic动态化 | `app_tas_dtu.h` + `app_tas_dtu.c` | 修改 ~20 行 |
| 初始化调用 | `main.c` 或 `App_LiftIot_Init()` | 新增 1 行 |

**总计约 50 行改动**，不影响已有功能逻辑。

---

## 6. 服务端改动

### 6.1 数据库迁移 (`database.js`)

```javascript
// 在 init() 中新增表和字段
db.exec(`
  CREATE TABLE IF NOT EXISTS device_registry (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    serial TEXT UNIQUE NOT NULL,
    uid TEXT UNIQUE NOT NULL,
    model TEXT NOT NULL DEFAULT '',
    batch TEXT DEFAULT '',
    produced_at TEXT DEFAULT '',
    status TEXT DEFAULT 'unbound',
    bound_device_id TEXT DEFAULT '',
    created_at TEXT NOT NULL DEFAULT ''
  );
`);

// devices 表新增字段（通过 ensureColumn 迁移）
ensureColumn(db, 'devices', 'owner_id', 'INTEGER DEFAULT NULL');
ensureColumn(db, 'devices', 'uid', "TEXT DEFAULT ''");
ensureColumn(db, 'devices', 'bind_status', "TEXT DEFAULT 'unbound'");
ensureColumn(db, 'devices', 'bound_at', "TEXT DEFAULT ''");
```

### 6.2 修改 mqtt-bridge.js

**关键改动**：`handleStatusUpdate()` 函数

```javascript
function handleStatusUpdate(deviceId, data) {
  const db = getDb();

  // 1. 优先用 uid 查找设备（替代原来的 device_id 直接匹配）
  const uid = data.uid || '';
  let device = null;

  if (uid) {
    device = db.prepare('SELECT * FROM devices WHERE uid = ?').get(uid);
  }
  if (!device) {
    device = db.prepare('SELECT * FROM devices WHERE device_id = ?').get(deviceId);
  }

  // 2. 未注册设备：查 registry 表自动关联
  if (!device) {
    const registry = uid
      ? db.prepare('SELECT * FROM device_registry WHERE uid = ?').get(uid)
      : null;

    if (registry) {
      // registry 中有记录 → 自动创建 device 记录
      const newDeviceId = registry.serial;  // 用出厂编号作为 device_id
      db.prepare(`INSERT OR IGNORE INTO devices
        (device_id, uid, name, model, group_name, bind_status, created_at)
        VALUES (?, ?, ?, ?, ?, 'unbound', ?)`)
        .run(newDeviceId, uid, `举升机 ${registry.serial}`,
             registry.model, '默认分组', nowISO());
      db.prepare('INSERT OR IGNORE INTO device_status (device_id, updated_at) VALUES (?, ?)')
        .run(newDeviceId, nowISO());
      device = db.prepare('SELECT * FROM devices WHERE device_id = ?').get(newDeviceId);

      // 更新 registry 状态
      db.prepare('UPDATE device_registry SET status = ?, bound_device_id = ? WHERE id = ?')
        .run('unbound', newDeviceId, registry.id);
    } else {
      // registry 中也没有 → 拒绝，不自动创建
      console.warn(`[MQTT] Unknown device uid=${uid} device=${deviceId}, ignored`);
      return;
    }
  }

  // 3. 写入 device_status（无论是否绑定都接收数据）
  // ... 原有的 INSERT/UPDATE 逻辑不变 ...

  // 4. 广播到前端（只广播给绑定该设备的用户的 WebSocket）
  broadcastToOwner(device, {
    type: 'device_status',
    device_id: device.device_id,
    data: { ...data, status }
  });
}
```

**新增**：`broadcastToOwner()` 函数

```javascript
// 前端连接 WebSocket 时需要携带 JWT token
// wsClients Map: ws -> { userId, deviceIds }
const wsClients = new Map();

function broadcastToOwner(device, message) {
  const data = JSON.stringify(message);
  for (const [ws, info] of wsClients) {
    try {
      if (ws.readyState === 1) {
        // 未绑定设备：不广播给任何人
        if (!device.owner_id) continue;
        // 已绑定设备：只广播给所有者
        if (info.userId === device.owner_id) {
          ws.send(data);
        }
      }
    } catch (e) {
      wsClients.delete(ws);
    }
  }
}
```

### 6.3 新增绑定API (`routes/binding.js`)

```javascript
const express = require('express');
const { getDb } = require('./database');
const { nowISO } = require('./utils');
const { authMiddleware } = require('./auth');

const router = express.Router();

/**
 * POST /api/devices/bind
 * 用户输入出厂编号绑定设备
 * Body: { serial: "GC-2026-00001" }
 */
router.post('/bind', authMiddleware, (req, res) => {
  const { serial } = req.body;
  if (!serial) {
    return res.status(400).json({ error: '请输入出厂编号' });
  }

  const db = getDb();

  // 1. 查 registry
  const registry = db.prepare(
    'SELECT * FROM device_registry WHERE serial = ?'
  ).get(serial.trim().toUpperCase());

  if (!registry) {
    return res.status(404).json({ error: '设备编号不存在，请检查输入' });
  }

  // 2. 查 devices 表（设备可能还没上线过）
  let device = db.prepare(
    'SELECT * FROM devices WHERE uid = ? OR device_id = ?'
  ).get(registry.uid, registry.serial);

  // 3. 如果设备记录不存在，创建一个
  if (!device) {
    db.prepare(`INSERT INTO devices
      (device_id, uid, name, model, group_name, bind_status, owner_id, bound_at, created_at)
      VALUES (?, ?, ?, ?, ?, 'bound', ?, ?, ?)`)
      .run(registry.serial, registry.uid, `举升机 ${serial}`,
           registry.model, '默认分组', req.user.id, nowISO(), nowISO());
    db.prepare('INSERT OR IGNORE INTO device_status (device_id, updated_at) VALUES (?, ?)')
      .run(registry.serial, nowISO());
  } else {
    // 4. 检查是否已绑定
    if (device.owner_id && device.owner_id !== req.user.id) {
      return res.status(409).json({ error: '该设备已被其他用户绑定' });
    }
    if (device.owner_id === req.user.id) {
      return res.status(409).json({ error: '该设备已绑定到您的账号' });
    }

    // 5. 执行绑定
    db.prepare(`UPDATE devices SET owner_id = ?, bind_status = 'bound', bound_at = ?
      WHERE device_id = ?`)
      .run(req.user.id, nowISO(), device.device_id);
  }

  // 6. 更新 registry 状态
  db.prepare("UPDATE device_registry SET status = 'bound' WHERE id = ?")
    .run(registry.id);

  // 7. 记录操作日志
  db.prepare(`INSERT INTO operation_logs (user_id, action, device_id, detail, result, created_at)
    VALUES (?, ?, ?, ?, ?, ?)`)
    .run(req.user.id, '绑定设备', registry.serial,
         `绑定设备 ${serial}`, '成功', nowISO());

  res.json({
    message: '绑定成功',
    device_id: registry.serial,
    model: registry.model
  });
});

/**
 * POST /api/devices/unbind
 * 用户解绑自己的设备
 * Body: { device_id: "GC-2026-00001" }
 */
router.post('/unbind', authMiddleware, (req, res) => {
  const { device_id } = req.body;
  if (!device_id) {
    return res.status(400).json({ error: '设备ID不能为空' });
  }

  const db = getDb();
  const device = db.prepare('SELECT * FROM devices WHERE device_id = ?').get(device_id);

  if (!device) {
    return res.status(404).json({ error: '设备不存在' });
  }
  if (device.owner_id !== req.user.id) {
    return res.status(403).json({ error: '只能解绑自己的设备' });
  }

  // 解绑
  db.prepare(`UPDATE devices SET owner_id = NULL, bind_status = 'unbound', bound_at = ''
    WHERE device_id = ?`).run(device_id);

  // 更新 registry
  if (device.uid) {
    db.prepare("UPDATE device_registry SET status = 'unbound' WHERE uid = ?")
      .run(device.uid);
  }

  // 操作日志
  db.prepare(`INSERT INTO operation_logs (user_id, action, device_id, detail, result, created_at)
    VALUES (?, ?, ?, ?, ?, ?)`)
    .run(req.user.id, '解绑设备', device_id,
         `解绑设备 ${device.name}`, '成功', nowISO());

  res.json({ message: '解绑成功' });
});

/**
 * GET /api/devices/bound
 * 获取当前用户绑定的设备列表
 */
router.get('/bound', authMiddleware, (req, res) => {
  const db = getDb();
  const devices = db.prepare(`
    SELECT d.*, s.online, s.locked, s.state, s.alarm,
           s.height_left_mm, s.height_right_mm, s.run_count, s.run_time_s
    FROM devices d
    LEFT JOIN device_status s ON d.device_id = s.device_id
    WHERE d.owner_id = ?
    ORDER BY d.device_id
  `).all(req.user.id);

  res.json(devices);
});

module.exports = router;
```

### 6.4 修改 `devices.js` 路由

现有 `GET /api/devices` 需要改为只返回当前用户绑定的设备：

```javascript
router.get('/', authMiddleware, (req, res) => {
  const db = getDb();

  // admin 可以看到所有设备，普通用户只能看到自己绑定的
  let devices;
  if (req.user.role === 'admin') {
    devices = db.prepare(`
      SELECT d.*, s.online, s.locked, s.state, s.alarm, ...
      FROM devices d
      LEFT JOIN device_status s ON d.device_id = s.device_id
      ORDER BY d.device_id
    `).all();
  } else {
    devices = db.prepare(`
      SELECT d.*, s.online, s.locked, s.state, s.alarm, ...
      FROM devices d
      LEFT JOIN device_status s ON d.device_id = s.device_id
      WHERE d.owner_id = ?
      ORDER BY d.device_id
    `).all(req.user.id);
  }

  res.json(devices.map(d => ({ ... })));
});
```

### 6.5 修改 `commandTopicFor()` 多设备支持

当前逻辑只有一个特殊映射，需要改为通用的 uid→topic 映射：

```javascript
// mqtt-bridge.js
function commandTopicFor(deviceId) {
  const db = getDb();
  const device = db.prepare('SELECT uid FROM devices WHERE device_id = ?').get(deviceId);

  if (device && device.uid) {
    // 新设备：用 uid 作为 MQTT topic 中的 gateway 部分
    return `${topicPrefix}/${device.uid}/command`;
  }

  // 降级：兼容旧设备（device_id 直接作为 gateway）
  const gatewayId = deviceId === defaultDeviceId ? defaultGatewayId : deviceId;
  return `${topicPrefix}/${gatewayId}/command`;
}
```

对应固件端 topic 格式：`gaochang/lift/{uid}/command`，固件订阅 `gaochang/lift/#` 通配符不变。

### 6.6 修改 `commands.js` 路由

发送命令前需要验证用户是否有权操作该设备：

```javascript
// 在 sendCommand 前增加权限检查
function checkDeviceOwnership(deviceId, userId, userRole) {
  if (userRole === 'admin') return true;
  const db = getDb();
  const device = db.prepare('SELECT owner_id FROM devices WHERE device_id = ?').get(deviceId);
  return device && device.owner_id === userId;
}
```

### 6.7 修改 `server.js`

```javascript
// 新增绑定路由
const bindingRouter = require('./binding');
app.use('/api/devices', bindingRouter);
```

### 6.8 修改 WebSocket 认证

```javascript
wss.on('connection', (ws, req) => {
  // 从 URL 参数或首条消息获取 token
  const url = new URL(req.url, 'http://localhost');
  const token = url.searchParams.get('token');

  if (token) {
    try {
      const decoded = jwt.verify(token, JWT_SECRET);
      wsClients.set(ws, { userId: decoded.id, role: decoded.role });
    } catch (e) {
      ws.close(4001, '认证失败');
      return;
    }
  }

  // ...
});
```

---

## 7. 前端改动

### 7.1 新增"添加设备"页面

```
┌─────────────────────────────────────────────┐
│  添加设备                                    │
│                                             │
│  请输入设备外壳上的出厂编号：                  │
│  ┌─────────────────────────────────────┐     │
│  │  GC-2026-00001                      │     │
│  └─────────────────────────────────────┘     │
│                                             │
│  提示：编号在设备外壳标签上，格式如 GC-XXXX-XXXXX│
│                                             │
│  [  绑定设备  ]                              │
│                                             │
│  ─────────────────────────────────────────  │
│  已绑定设备 (2台)                             │
│                                             │
│  ┌─────────────────────────────────────┐     │
│  │ GC-2026-00001  GC-F407-2POST  在线  │     │
│  │ [解绑]                               │     │
│  └─────────────────────────────────────┘     │
│  ┌─────────────────────────────────────┐     │
│  │ GC-2026-00002  GC-F407-4POST  离线  │     │
│  │ [解绑]                               │     │
│  └─────────────────────────────────────┘     │
└─────────────────────────────────────────────┘
```

### 7.2 修改设备列表

- 只显示当前用户绑定的设备（非admin）
- admin 角色可以看到所有设备，包括未绑定的
- 未绑定设备显示灰色标签"未绑定"

### 7.3 修改设备卡片

```html
<!-- 新增绑定状态标识 -->
<div class="device-card">
  <div class="device-header">
    <span class="device-name">GC-2026-00001</span>
    <span class="device-model">GC-F407-2POST</span>
    <span class="bind-badge bound">已绑定</span>
  </div>
  <!-- ... 原有内容 ... -->
</div>
```

### 7.4 WebSocket 连接改造

```javascript
// 连接时携带 token
const token = localStorage.getItem('token');
const ws = new WebSocket(`ws://${location.host}/ws?token=${token}`);

// 接收消息时过滤：只处理自己绑定的设备
ws.onmessage = (event) => {
  const msg = JSON.parse(event.data);
  if (msg.type === 'device_status') {
    // 前端只更新自己设备列表中的设备
    if (myDeviceIds.has(msg.device_id)) {
      updateDeviceCard(msg.device_id, msg.data);
    }
  }
};
```

---

## 8. 用户流程

### 8.1 绑定流程

```
用户                    网页                    服务端                  设备(固件)
 │                      │                       │                       │
 │  1.登录网页           │                       │                       │
 │─────────────────────→│                       │                       │
 │                      │                       │                       │
 │  2.点击"添加设备"     │                       │                       │
 │─────────────────────→│                       │                       │
 │                      │                       │                       │
 │  3.输入出厂编号       │                       │                       │
 │  "GC-2026-00001"     │                       │                       │
 │─────────────────────→│                       │                       │
 │                      │  4. POST /api/devices/bind                    │
 │                      │─────────────────────→│                       │
 │                      │                       │                       │
 │                      │                       │  5. 查device_registry │
 │                      │                       │  serial→uid映射       │
 │                      │                       │───────┐               │
 │                      │                       │←──────┘               │
 │                      │                       │                       │
 │                      │                       │  6. 创建/更新devices  │
 │                      │                       │  owner_id = user.id   │
 │                      │                       │───────┐               │
 │                      │                       │←──────┘               │
 │                      │                       │                       │
 │                      │  7. 返回成功           │                       │
 │                      │←─────────────────────│                       │
 │  8. 看到"绑定成功"    │                       │                       │
 │←─────────────────────│                       │                       │
 │                      │                       │                       │
 │                      │                       │  (设备可能已在线)      │
 │                      │                       │  9. 收到MQTT消息      │
 │                      │                       │←──────────────────────│
 │                      │                       │                       │
 │                      │                       │  10. 用uid查devices   │
 │                      │                       │  找到owner_id         │
 │                      │                       │───────┐               │
 │                      │                       │←──────┘               │
 │                      │                       │                       │
 │                      │  11. WebSocket推送     │                       │
 │  12. 看到设备实时数据  │←─────────────────────│                       │
 │←─────────────────────│                       │                       │
```

### 8.2 解绑流程

```
1. 用户在网页点击设备的"解绑"按钮
2. 确认对话框："确定解绑该设备？解绑后将无法看到设备数据。"
3. POST /api/devices/unbind { device_id: "GC-2026-00001" }
4. 服务端：
   - devices.owner_id = NULL
   - devices.bind_status = 'unbound'
   - device_registry.status = 'unbound'
   - 记录操作日志
5. 前端刷新设备列表
```

### 8.3 设备转手流程

```
1. 原用户 A 解绑设备
2. 新用户 B 输入同一出厂编号绑定
3. 设备无需任何操作，数据连续
```

---

## 9. 安全设计

### 9.1 绑定安全

| 措施 | 说明 |
|------|------|
| 出厂编号不暴露UID | 用户只输入编号，UID只在MQTT内部传输 |
| 一人绑定，他人不可绑 | `owner_id` 唯一约束 |
| 编号输入限流 | 同IP每分钟最多3次绑定尝试 |
| 操作日志 | 所有绑定/解绑操作记录到 `operation_logs` |

### 9.2 数据隔离

| 措施 | 说明 |
|------|------|
| API过滤 | 非admin用户只能查自己的设备 |
| WebSocket过滤 | 只推送给设备所有者 |
| 命令权限 | 发送命令前检查 `owner_id` |
| 未绑定设备不展示 | 数据被接收但不推送给任何人 |

### 9.3 MQTT安全（后续优化）

当前阶段保持MQTT无认证（兼容现有DTU），后续可增加：
- MQTT username/password 用 UID 派生
- Broker ACL 限制 topic 发布权限

---

## 10. 错误处理

| 场景 | HTTP状态码 | 错误信息 |
|------|-----------|---------|
| 出厂编号不存在 | 404 | 设备编号不存在，请检查输入 |
| 设备已被他人绑定 | 409 | 该设备已被其他用户绑定 |
| 设备已绑定给自己 | 409 | 该设备已绑定到您的账号 |
| 解绑非自己的设备 | 403 | 只能解绑自己的设备 |
| 输入为空 | 400 | 请输入出厂编号 |
| 绑定限流触发 | 429 | 操作过于频繁，请稍后重试 |

---

## 11. 实施阶段

### Phase 1：数据库 + 服务端（不需要固件改动）

1. `database.js`：新增 `device_registry` 表，`devices` 表加字段
2. 新建 `binding.js`：绑定/解绑 API
3. 修改 `devices.js`：按 `owner_id` 过滤
4. 修改 `server.js`：注册新路由

### Phase 2：mqtt-bridge 改造

5. 修改 `handleStatusUpdate()`：用 uid 查找设备
6. 修改 `broadcastToClients()`：按 owner_id 过滤
7. 修改 `commandTopicFor()`：支持多设备 topic

### Phase 3：前端

8. 新增"添加设备"页面
9. 修改设备列表按 owner_id 过滤
10. WebSocket 携带 token

### Phase 4：固件（可独立进行）

11. 读取芯片 UID
12. MQTT 消息加 uid 字段
13. DTU topic 动态化

### Phase 5：工厂工具

14. 编写工厂录入脚本/工具（CSV导入或Web管理界面）

---

## 12. 兼容性

- **现有设备**：`gaochang_lift_f407zet6` 作为默认设备，admin 手动绑定
- **现有数据库**：通过 `ensureColumn` 迁移，不破坏现有数据
- **现有前端**：admin 看到所有设备，逐步迁移
- **固件过渡期**：没有 uid 字段的消息用 `device_id` 匹配（降级方案）
