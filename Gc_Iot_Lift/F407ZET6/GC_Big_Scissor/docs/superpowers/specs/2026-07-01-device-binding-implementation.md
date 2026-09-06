# 举升机设备绑定实现方案（短期应急 + 长期平滑升级）

> 编写日期：2026-07-01
> 适用场景：STM32F407 + TAS-LTE-892D 4G DTU + 自建 Node.js + EMQX
> 设计原则：短期最小代价上线，长期无缝升级，**短期投入的所有工作在长期方案中全部复用，零废弃**

---

## 0. 核心设计思想

**一个 UID，三个阶段，零废弃**。

STM32F407 芯片自带的 96-bit UID（地址 `0x1FFF7A10`）是整个方案的核心身份。短期和长期方案**用同一个 UID**，区别只在于"围绕 UID 做多少安全加固"：

| 阶段 | UID 用途 | 认证强度 | 适用场景 |
|------|---------|---------|---------|
| **短期（15 台订单）** | 派生 ClientID + Topic，但 broker 暂不校验 | 弱（仅靠 topic 隔离） | 小批量交付，快速上线 |
| **长期（量产）** | 派生 ClientID + Username + Password + Topic，EMQX HTTP 认证 + ACL | 强（broker 层校验） | 量产，公网部署 |

**关键**：短期固件代码和长期固件代码**95% 相同**，升级时只需补充 Username/Password 配置 + 启用 EMQX 认证。短期录入的 `device_registry` 表、贴标、客户绑定关系，**长期全部直接复用，不用迁移**。

---

## 1. 短期方案（15 台订单应急）

### 1.1 目标

- 15 台设备各自能远程监控
- 客户能区分哪台是哪台
- 不需要为每台编译不同固件
- 所有工作在长期方案中可复用

### 1.2 架构

```
┌──────────────┐   UART+AT    ┌──────────┐   4G+明文MQTT   ┌──────────┐
│  STM32F407   │ ──────────→ │  TAS DTU  │ ──────────────→ │  EMQX     │
│  读UID派生参数│              │  透传     │                 │  1883     │
└──────────────┘              └──────────┘                 └────┬─────┘
                                                                │
                                                       ┌────────▼─────────┐
                                                       │  Node.js 后端     │
                                                       │  - mqtt-bridge   │
                                                       │  - /api/devices  │
                                                       │  - /bind 页面    │
                                                       │  - device_registry│
                                                       └────────┬─────────┘
                                                                │
                                                       ┌────────▼─────────┐
                                                       │  客户浏览器/微信  │
                                                       │  扫码绑定         │
                                                       └──────────────────┘
```

短期 **EMQX 暂不启用 HTTP 认证**，broker 接受所有连接，靠 topic 中的 UID 做逻辑隔离。这是短期权宜，长期方案会补上。

### 1.3 固件改动（共约 30 行，所有 15 台烧同一份）

#### 1.3.1 新增全局变量和 UID 读取函数

修改 [APP/Inc/app_lift_iot.h](file:///e:/MCU/gaochang/code/f407zet6/APP/Inc/app_lift_iot.h)：

```c
/* ============ 设备身份（运行时从芯片读取，所有板子固件相同） ============ */
#define STM32_UID_ADDR              0x1FFF7A10U
#define STM32_UID_LEN               12U

extern char g_device_uid[25];       /* 24位HEX + '\0'，例如 "4A003258534B501620383552" */
extern char g_device_id[48];        /* 派生 ID，例如 "lift_4A003258534B501620383552" */

void App_LiftIot_ReadUID(void);     /* 在 Init 中调用一次 */
```

修改 [APP/Src/app_lift_iot.c](file:///e:/MCU/gaochang/code/f407zet6/APP/Src/app_lift_iot.c)：

```c
#include <stdio.h>
#include "stm32f4xx_hal.h"

char g_device_uid[25] = {0};
char g_device_id[48]  = {0};

void App_LiftIot_ReadUID(void)
{
    uint32_t uid0 = *(volatile uint32_t *)(STM32_UID_ADDR);
    uint32_t uid1 = *(volatile uint32_t *)(STM32_UID_ADDR + 4U);
    uint32_t uid2 = *(volatile uint32_t *)(STM32_UID_ADDR + 8U);

    snprintf(g_device_uid, sizeof(g_device_uid),
             "%08lX%08lX%08lX",
             (unsigned long)uid2,
             (unsigned long)uid1,
             (unsigned long)uid0);

    snprintf(g_device_id, sizeof(g_device_id),
             "lift_%s", g_device_uid);

    elog_i("LIFT_IOT", "Device UID: %s", g_device_uid);
    elog_i("LIFT_IOT", "Device ID : %s", g_device_id);
}

void App_LiftIot_Init(void)
{
    memset(&g_lift_iot_status, 0, sizeof(g_lift_iot_status));
    g_lift_iot_last_direction = DIR_STOP;
    g_lift_iot_last_alarm = ALARM_NONE;
    g_iot_event_pending = 0U;

    App_LiftIot_ReadUID();   /* 新增：启动时读 UID */
}
```

#### 1.3.2 DTU 参数改为运行时派生

修改 [APP/Inc/app_tas_dtu.h](file:///e:/MCU/gaochang/code/f407zet6/APP/Inc/app_tas_dtu.h)：

```c
/* ============ MQTT 参数（运行时用 UID 派生，所有板子固件相同） ============ */
/* 删除原硬编码：
 *   #define TAS_DTU_CLIENT_ID   "gaochang_lift_f407zet6_dtu"
 *   #define TAS_DTU_MQTT_USERNAME  ""
 *   #define TAS_DTU_MQTT_PASSWORD  ""
 *   #define TAS_DTU_TOPIC_TELEMETRY  "gaochang/lift/f407zet6/telemetry"
 *   #define TAS_DTU_TOPIC_COMMAND_SUB "gaochang/lift/#"
 */

/* topic 模板：运行时用 UID 拼接成 dev/<UID>/up、dev/<UID>/down */
#define TAS_DTU_TOPIC_UP_FMT        "dev/%s/up"
#define TAS_DTU_TOPIC_DOWN_FMT      "dev/%s/down"
```

修改 [APP/Src/app_tas_dtu.c](file:///e:/MCU/gaochang/code/f407zet6/APP/Src/app_tas_dtu.c) 的 `App_TasDtu_ConfigMqttChannel()`（原 491-585 行）：

```c
extern char g_device_uid[];   /* 来自 app_lift_iot.c */

static tas_dtu_result_t App_TasDtu_ConfigMqttChannel(void)
{
    tas_dtu_result_t result;
    char client_id[40];
    char topic_up[40];
    char topic_down[40];

    /* 用 UID 派生所有参数 */
    snprintf(client_id,  sizeof(client_id),  "dev_%s", g_device_uid);
    snprintf(topic_up,   sizeof(topic_up),   TAS_DTU_TOPIC_UP_FMT,   g_device_uid);
    snprintf(topic_down, sizeof(topic_down), TAS_DTU_TOPIC_DOWN_FMT, g_device_uid);

    result = App_TasDtu_SendConfigCommand("AT+DTUMODE=2,1", 3000U);
    if (result != TAS_DTU_RESULT_OK) return result;

    result = App_TasDtu_SendConfigFormat(5000U,
                                         "AT+IPPORT=\"%s\",%u,1",
                                         TAS_DTU_BROKER_HOST,
                                         (unsigned int)TAS_DTU_BROKER_PORT);
    if (result != TAS_DTU_RESULT_OK) return result;

    /* ClientID 用 UID 派生 */
    result = App_TasDtu_SendConfigFormat(3000U,
                                         "AT+CLIENTID=\"%s\",1",
                                         client_id);
    if (result != TAS_DTU_RESULT_OK) return result;

    /* 短期：Username/Password 留空（EMQX 暂不校验）
     * 长期升级时改用 UID 作为 Username/Password（见长期方案 2.3.2） */
    result = App_TasDtu_SendConfigFormat(3000U,
                                         "AT+USERPWD=\"\",\"\",1");
    if (result != TAS_DTU_RESULT_OK) return result;

    /* 订阅自己的命令 topic，绝不能用 # 通配 */
    result = App_TasDtu_SendConfigFormat(3000U,
                                         "AT+MQTTSUB=1,\"%s\",0,1,1",
                                         topic_down);
    if (result != TAS_DTU_RESULT_OK) return result;

    /* 发布到自己的数据 topic */
    result = App_TasDtu_SendConfigFormat(3000U,
                                         "AT+MQTTPUB=1,\"%s\",0,0,1,1",
                                         topic_up);
    if (result != TAS_DTU_RESULT_OK) return result;

    /* 其余 AT 配置保持不变（MQTTKEEP / CLEANSESSION / 等） */
    result = App_TasDtu_SendConfigCommand("AT+MQTTPUB=0,\"\",0,0,2,1", 3000U);
    if (result != TAS_DTU_RESULT_OK) return result;

    result = App_TasDtu_SendConfigCommand("AT+MQTTKEEP=120,1", 3000U);
    if (result != TAS_DTU_RESULT_OK) return result;

    result = App_TasDtu_SendConfigCommand("AT+CLEANSESSION=1,1", 3000U);
    if (result != TAS_DTU_RESULT_OK) return result;

    result = App_TasDtu_SendConfigCommand("AT+BLOCKINFO=0,0", 3000U);
    if (result != TAS_DTU_RESULT_OK) return result;

    result = App_TasDtu_SendConfigCommand("AT+AUTOSTATUS=1,1", 3000U);
    if (result != TAS_DTU_RESULT_OK) return result;

    result = App_TasDtu_SendConfigCommand("AT+DTUPACKET=0,1024", 3000U);
    if (result != TAS_DTU_RESULT_OK) return result;

    result = App_TasDtu_SendConfigCommand("AT+RELINKTIME=30", 3000U);
    if (result != TAS_DTU_RESULT_OK) return result;

    return App_TasDtu_SendConfigCommand("AT+DSCTIME=300", 3000U);
}
```

#### 1.3.3 MQTT 消息加 uid 字段

修改 [APP/Src/app_lift_iot.c](file:///e:/MCU/gaochang/code/f407zet6/APP/Src/app_lift_iot.c) 的 `App_LiftIot_BuildTelemetryJson()`（原 309 行附近），在 JSON 中加 `"uid"` 字段，并把 `"device"` 改用 `g_device_id`：

```c
len = snprintf(buf, size,
    "{\"type\":\"%s\",\"device\":\"%s\",\"uid\":\"%s\",\"name\":\"%s\",\"model\":\"%s\","
    /* ... 其余字段不变 ... */
    type,
    g_device_id,           /* 原来是 LIFT_IOT_DEVICE_ID */
    g_device_uid,          /* 新增 */
    LIFT_IOT_DEVICE_NAME,
    LIFT_IOT_DEVICE_MODEL,
    /* ... 其余参数不变 ... */
);
```

同样修改 `App_LiftIot_BuildStatusJson()` 和 `App_LiftIot_BuildCommandStatusJson()` 两个函数。

#### 1.3.4 固件改动小结

| 改动 | 文件 | 行数 |
|------|------|------|
| 新增 UID 全局变量和读取函数 | app_lift_iot.h / app_lift_iot.c | +18 |
| Init 中调用 ReadUID | app_lift_iot.c | +1 |
| DTU 配置改为 UID 派生 | app_tas_dtu.h / app_tas_dtu.c | +15 / -5 |
| 三个 BuildJson 加 uid 字段 | app_lift_iot.c | +6 |

**总计约 30 行净增**。所有 15 台板子烧**同一份** .hex。

### 1.4 服务端改动

#### 1.4.1 数据库新增 device_registry 表

```sql
CREATE TABLE IF NOT EXISTS device_registry (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  serial TEXT UNIQUE NOT NULL,           -- GC-2026-00001，贴在设备外壳
  uid TEXT UNIQUE NOT NULL,              -- STM32 96-bit UID HEX
  model TEXT NOT NULL DEFAULT '',
  batch TEXT DEFAULT '',
  produced_at TEXT DEFAULT '',
  status TEXT DEFAULT 'unbound',         -- unbound / bound
  bound_device_id TEXT DEFAULT '',
  created_at TEXT NOT NULL DEFAULT ''
);
CREATE INDEX IF NOT EXISTS idx_registry_uid ON device_registry(uid);
CREATE INDEX IF NOT EXISTS idx_registry_serial ON device_registry(serial);
```

#### 1.4.2 devices 表加字段

```sql
ALTER TABLE devices ADD COLUMN owner_id INTEGER DEFAULT NULL;
ALTER TABLE devices ADD COLUMN uid TEXT DEFAULT '';
ALTER TABLE devices ADD COLUMN bind_status TEXT DEFAULT 'unbound';
ALTER TABLE devices ADD COLUMN bound_at TEXT DEFAULT '';
```

#### 1.4.3 mqtt-bridge 改造（关键）

修改消息处理逻辑：用 uid 查找设备，未注册的设备拒绝接收。

```javascript
function handleStatusUpdate(deviceId, data) {
  const db = getDb();
  const uid = data.uid || '';

  // 1. 优先用 uid 查找设备
  let device = null;
  if (uid) {
    device = db.prepare('SELECT * FROM devices WHERE uid = ?').get(uid);
  }

  // 2. 设备表没有 → 查 registry，自动创建 devices 记录
  if (!device) {
    const registry = uid
      ? db.prepare('SELECT * FROM device_registry WHERE uid = ?').get(uid)
      : null;

    if (registry) {
      const newDeviceId = registry.serial;  // 用 SN 作为 device_id
      db.prepare(`INSERT OR IGNORE INTO devices
        (device_id, uid, name, model, group_name, bind_status, created_at)
        VALUES (?, ?, ?, ?, ?, 'unbound', ?)`)
        .run(newDeviceId, uid, `举升机 ${registry.serial}`,
             registry.model, '默认分组', nowISO());
      db.prepare('INSERT OR IGNORE INTO device_status (device_id, updated_at) VALUES (?, ?)')
        .run(newDeviceId, nowISO());
      device = db.prepare('SELECT * FROM devices WHERE device_id = ?').get(newDeviceId);
    } else {
      // 未注册设备：拒绝处理，不自动创建
      console.warn(`[MQTT] Unknown device uid=${uid}, ignored`);
      return;
    }
  }

  // 3. 写入 device_status（无论是否绑定都接收，但不推送给任何人）
  // ... 原有 INSERT/UPDATE 逻辑保持不变 ...

  // 4. 只推送给绑定该设备的用户
  if (device.owner_id) {
    broadcastToOwner(device, {
      type: 'device_status',
      device_id: device.device_id,
      data: { ...data }
    });
  }
}

function broadcastToOwner(device, message) {
  const payload = JSON.stringify(message);
  for (const [ws, info] of wsClients) {
    try {
      if (ws.readyState === 1 && info.userId === device.owner_id) {
        ws.send(payload);
      }
    } catch (e) {
      wsClients.delete(ws);
    }
  }
}
```

#### 1.4.4 命令下发改造

修改命令下发逻辑，按设备 UID 拼 topic：

```javascript
function commandTopicFor(device) {
  // 原来是固定的 gaochang/lift/f407zet6/command
  // 改为按 UID 隔离
  return `dev/${device.uid}/down`;
}

function sendCommand(deviceId, command) {
  const db = getDb();
  const device = db.prepare('SELECT * FROM devices WHERE device_id = ?').get(deviceId);
  if (!device || !device.uid) {
    throw new Error('Device not found or UID missing');
  }
  // 校验调用者是设备所有者（在路由层做）
  const topic = commandTopicFor(device);
  mqttClient.publish(topic, JSON.stringify(command));
}
```

#### 1.4.5 绑定 API

```javascript
const express = require('express');
const router = express.Router();

/**
 * GET /bind?sn=GC-2026-00001
 * 客户扫码打开的绑定页面
 */
router.get('/bind', (req, res) => {
  const sn = req.query.sn;
  if (!sn) return res.status(400).send('缺少设备编号');
  res.send(`
    <html>
    <body>
      <h2>绑定设备 ${sn}</h2>
      <p>请先登录后点击绑定</p>
      <button onclick="doBind()">绑定</button>
      <script>
        async function doBind() {
          const r = await fetch('/api/devices/bind', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({serial: '${sn}'})
          });
          const j = await r.json();
          alert(j.message || (r.ok ? '绑定成功' : '绑定失败'));
          if (r.ok) location.href = '/devices';
        }
      </script>
    </body>
    </html>
  `);
});

/**
 * POST /api/devices/bind
 * Body: { serial: "GC-2026-00001" }
 */
router.post('/api/devices/bind', authMiddleware, (req, res) => {
  const db = getDb();
  const { serial } = req.body;
  const userId = req.user.id;

  if (!serial) return res.status(400).json({message: '请输入设备编号'});

  // 限流：同 IP 每分钟最多 3 次（略，用 express-rate-limit）

  // 1. 查 registry
  const registry = db.prepare('SELECT * FROM device_registry WHERE serial = ?').get(serial);
  if (!registry) {
    return res.status(404).json({message: '设备编号不存在，请检查输入'});
  }

  // 2. 检查是否已被他人绑定
  const device = db.prepare('SELECT * FROM devices WHERE uid = ?').get(registry.uid);
  if (device && device.owner_id && device.owner_id !== userId) {
    return res.status(409).json({message: '该设备已被其他用户绑定'});
  }
  if (device && device.owner_id === userId) {
    return res.status(409).json({message: '该设备已绑定到您的账号'});
  }

  // 3. 绑定
  const now = nowISO();
  db.prepare(`UPDATE devices SET owner_id=?, bind_status='bound', bound_at=? WHERE uid=?`)
    .run(userId, now, registry.uid);
  db.prepare(`UPDATE device_registry SET status='bound', bound_device_id=? WHERE id=?`)
    .run(device ? device.device_id : registry.serial, registry.id);

  // 4. 记录日志
  db.prepare(`INSERT INTO binding_logs (uid, serial, user_id, action, ip, created_at)
              VALUES (?, ?, ?, 'bind', ?, ?)`)
    .run(registry.uid, serial, userId, req.ip, now);

  res.json({message: '绑定成功', device_id: registry.serial});
});

/**
 * POST /api/devices/unbind
 * Body: { device_id: "GC-2026-00001" }
 */
router.post('/api/devices/unbind', authMiddleware, (req, res) => {
  const db = getDb();
  const { device_id } = req.body;
  const userId = req.user.id;

  const device = db.prepare('SELECT * FROM devices WHERE device_id = ?').get(device_id);
  if (!device) return res.status(404).json({message: '设备不存在'});
  if (device.owner_id !== userId) {
    return res.status(403).json({message: '只能解绑自己的设备'});
  }

  const now = nowISO();
  db.prepare(`UPDATE devices SET owner_id=NULL, bind_status='unbound', bound_at=NULL
              WHERE device_id=?`).run(device_id);
  db.prepare(`UPDATE device_registry SET status='unbound', bound_device_id=''
              WHERE uid=?`).run(device.uid);
  db.prepare(`INSERT INTO binding_logs (uid, serial, user_id, action, ip, created_at)
              VALUES (?, ?, ?, 'unbind', ?, ?)`)
    .run(device.uid, device_id, userId, req.ip, now);

  res.json({message: '解绑成功'});
});

/**
 * GET /api/devices
 * 返回当前用户绑定的设备列表
 */
router.get('/api/devices', authMiddleware, (req, res) => {
  const db = getDb();
  const devices = db.prepare(
    `SELECT d.*, ds.online, ds.locked, ds.updated_at
     FROM devices d
     LEFT JOIN device_status ds ON d.device_id = ds.device_id
     WHERE d.owner_id = ?
     ORDER BY d.bound_at DESC`
  ).all(req.user.id);
  res.json(devices);
});

module.exports = router;
```

#### 1.4.6 binding_logs 表

```sql
CREATE TABLE IF NOT EXISTS binding_logs (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  uid TEXT NOT NULL,
  serial TEXT NOT NULL,
  user_id INTEGER,
  action TEXT,           -- bind / unbind
  ip TEXT,
  created_at TEXT
);
```

### 1.5 工厂操作流程（15 台订单）

#### 1.5.1 录入 device_registry

15 台板子逐台上电，从串口/RTT 日志读到 UID，手工录入：

```sql
INSERT INTO device_registry (serial, uid, model, batch, produced_at, status, created_at) VALUES
('GC-2026-00001', '4A003258534B501620383552', 'GC-F407-2POST', 'BATCH-001', '2026-07-01', 'unbound', '2026-07-01T00:00:00Z'),
('GC-2026-00002', '5B003258534B501620383553', 'GC-F407-2POST', 'BATCH-001', '2026-07-01', 'unbound', '2026-07-01T00:00:00Z'),
-- ... 共 15 条 ...
('GC-2026-00015', '6C003258534B501620383564', 'GC-F407-2POST', 'BATCH-001', '2026-07-01', 'unbound', '2026-07-01T00:00:00Z');
```

#### 1.5.2 贴标

每台设备外壳贴一张标签，内容：

```
┌─────────────────────────────┐
│  举升机 GC-2026-00001       │
│                             │
│  扫码绑定 ↓                  │
│  ▓▓▓▓▓                      │
│  ▓▓▓▓▓▓▓▓▓▓▓                │
│  ▓▓▓▓▓▓▓                    │
│                             │
│  https://你的域名/bind?sn=GC-2026-00001
└─────────────────────────────┘
```

**关键操作**：上电读 UID 时立刻贴对应 SN 的标签，避免 UID 和 SN 对应关系错乱。

#### 1.5.3 交付和客户绑定

```
客户收到设备 → 撕标签扫二维码 → 微信打开 /bind?sn=xxx → 登录 → 点绑定 → 完成
```

客户在网页上看到的是 `举升机 GC-2026-00001`，可自行改名为"1号车间A位"等。

### 1.6 短期方案的安全状态

| 威胁 | 短期是否防 | 说明 |
|------|----------|------|
| 未知设备连 broker 发垃圾数据 | ❌ 不防 | EMQX 暂不校验，靠 Node.js 层拒绝未注册 UID |
| 设备 A 订阅设备 B 的命令 | ✅ 防 | 固件订阅的是 `dev/<自己的UID>/down`，不用通配符 |
| 攻击者伪造 UID 发数据 | ❌ 不防 | 短期接受这个风险（15 台小批量内网可控） |
| 客户 A 看到客户 B 的设备 | ✅ 防 | 服务端按 owner_id 过滤 |
| 未绑定设备的数据被任何人看到 | ✅ 防 | 未绑定不推送给任何人 |

**短期风险接受**：15 台量小，broker IP 不公开，攻击面有限。**长期方案必须补 EMQX 认证**。

---

## 2. 长期方案（量产平滑升级）

### 2.1 升级目标

在短期方案基础上，**不改数据结构、不改固件主体逻辑、不改客户绑定流程**，只补两层安全：

1. **EMQX HTTP 认证**：设备连接 broker 时校验 UID 合法性
2. **EMQX HTTP ACL**：设备发布/订阅时校验 topic 必须含自己的 UID

### 2.2 短期→长期迁移清单（零废弃）

| 短期资产 | 长期是否复用 | 升级动作 |
|---------|------------|---------|
| `device_registry` 表 | ✅ 直接复用 | 无 |
| `devices` 表及字段 | ✅ 直接复用 | 无 |
| `binding_logs` 表 | ✅ 直接复用 | 无 |
| 固件 UID 读取代码 | ✅ 直接复用 | 无 |
| 固件 DTU 配置代码 | ✅ 95% 复用 | 只改 USERPWD 一行 |
| 固件 Topic 派生代码 | ✅ 直接复用 | 无 |
| 绑定 API / 绑定页面 | ✅ 直接复用 | 无 |
| 客户扫码绑定流程 | ✅ 直接复用 | 无 |
| 已贴的 15 台标签 | ✅ 直接复用 | 无 |
| 15 台客户的绑定关系 | ✅ 直接复用 | 无 |
| mqtt-bridge 消息处理 | ✅ 直接复用 | 无 |

**唯一新增**：EMQX 配置 + Node.js 两个回调接口（`/mqtt/auth` 和 `/mqtt/acl`）。

### 2.3 长期固件改动（仅 1 处）

[APP/Src/app_tas_dtu.c](file:///e:/MCU/gaochang/code/f407zet6/APP/Src/app_tas_dtu.c) 的 `App_TasDtu_ConfigMqttChannel()` 中，把 USERPWD 从空改为 UID：

```c
/* 短期：
 * App_TasDtu_SendConfigFormat(3000U, "AT+USERPWD=\"\",\"\",1");
 * 长期：用 UID 作为 username 和 password */
result = App_TasDtu_SendConfigFormat(3000U,
                                     "AT+USERPWD=\"%s\",\"%s\",1",
                                     g_device_uid, g_device_uid);
```

**为什么 password = UID**：DTU 不支持 HMAC 计算，password 只能是静态字符串。明文 MQTT 下，HMAC 也防不了中间人，UID 已是设备唯一身份，配合 EMQX ACL 即可防横向移动。详见 2.6 安全分析。

### 2.4 EMQX 配置

#### 2.4.1 启用 HTTP 认证插件

EMQX Dashboard 或 `emqx.conf`：

```hocon
authentication = [
  {
    mechanism = password_based
    backend = http
    method = post
    url = "http://127.0.0.1:3000/mqtt/auth"
    headers {
      content-type = "application/json"
    }
    body {
      username = "${username}"
      password = "${password}"
      clientid = "${clientid}"
    }
  }
]
```

#### 2.4.2 启用 HTTP ACL 授权

```hocon
authorization {
  sources = [
    {
      type = http
      method = post
      url = "http://127.0.0.1:3000/mqtt/acl"
      headers {
        content-type = "application/json"
      }
      body {
        username = "${username}"
        clientid = "${clientid}"
        topic = "${topic}"
        action = "${action}"
      }
    }
  ]
  no_match = deny
  deny_action = disconnect
}
```

### 2.5 Node.js 回调接口

#### 2.5.1 `/mqtt/auth` 认证回调

```javascript
/**
 * EMQX 设备连接时调用
 * 校验 username(uid) 在 device_registry 中存在，且 password == uid
 */
router.post('/mqtt/auth', (req, res) => {
  const { username, password, clientid } = req.body;
  const db = getDb();

  // username 必须是已注册的 UID
  const registry = db.prepare('SELECT 1 FROM device_registry WHERE uid = ?')
                    .get(username);

  if (registry && password === username) {
    return res.json({ result: 'allow' });
  }
  res.json({ result: 'deny' });
});
```

#### 2.5.2 `/mqtt/acl` 授权回调

```javascript
/**
 * EMQX 设备发布/订阅时调用
 * 校验 topic 中的 UID 必须等于 username 的 UID
 *   dev/<UID>/up   → 只有该 UID 能 publish
 *   dev/<UID>/down → 只有该 UID 能 subscribe
 */
router.post('/mqtt/acl', (req, res) => {
  const { username, topic, action } = req.body;

  // 解析 topic: dev/<UID>/up 或 dev/<UID>/down
  const match = topic.match(/^dev\/([0-9A-F]{24})\/(up|down)$/);
  if (!match) return res.json({ result: 'deny' });

  const topicUid = match[1];
  const direction = match[2];

  // topic 中的 UID 必须等于自己的 username
  if (topicUid !== username) {
    return res.json({ result: 'deny' });
  }

  // up 只能发布，down 只能订阅
  if (direction === 'up' && action === 'publish') {
    return res.json({ result: 'allow' });
  }
  if (direction === 'down' && action === 'subscribe') {
    return res.json({ result: 'allow' });
  }

  res.json({ result: 'deny' });
});
```

### 2.6 长期方案的安全状态

| 威胁 | 长期是否防 | 防御层 |
|------|----------|--------|
| 未知设备连 broker | ✅ | EMQX HTTP Authn 拒绝 |
| 设备 A 订阅设备 B 的命令 | ✅ | EMQX HTTP Authz 拒绝 |
| 攻击者伪造 UID 发数据 | ✅ 部分 | 伪造 UID 必须在 registry 中存在，且只能操作自己那台 |
| 攻击者抓包拿到 UID 后伪造 | ⚠️ 同上 | 明文 MQTT 无法防抓包，但伪造只能影响自己那台，不会横向扩散 |
| 客户 A 看到客户 B 的设备 | ✅ | 服务端 owner_id 过滤 |
| 未绑定设备的数据被看到 | ✅ | 服务端不推送 + 未绑定设备即使连上也不广播 |

**关于"password = UID 是否安全"**：

明文 MQTT 下，无论用静态 password 还是 HMAC，攻击者抓包都能拿到凭证。区别只在于：
- **静态 password=UID**：抓包后能伪造该设备，但只能伪造这一台
- **HMAC(device_secret, ...)**：抓包后能伪造该设备，也只能伪造这一台

两者防护效果**等价**，HMAC 多了 device_secret 的管理成本（要写 W25QXX、做工厂烧录工具），但没带来实质安全提升。真正防横向移动靠的是 EMQX ACL，两种方案都依赖它。

所以长期方案选 `password = UID`，**不做 HMAC、不写 W25QXX、不做工厂烧录工具**，把复杂度压到最低。

---

## 3. 实施路线图

### Phase 1：短期方案上线（解决 15 台订单）

| 步骤 | 内容 | 产出 |
|------|------|------|
| 1.1 | 固件：加 UID 读取 + DTU 参数派生 + JSON 加 uid 字段 | 一份 .hex，15 台共用 |
| 1.2 | 服务端：建 device_registry / devices 加字段 / binding_logs 表 | 数据库 schema |
| 1.3 | 服务端：mqtt-bridge 用 uid 查设备 + 按 owner_id 推送 | mqtt-bridge.js |
| 1.4 | 服务端：绑定 API + /bind 页面 | binding.js |
| 1.5 | 工厂：15 台板子上电读 UID → 录入 registry → 贴标 | 15 张标签 |
| 1.6 | 客户：扫码绑定 | 15 台设备各自有归属 |

### Phase 2：长期方案升级（量产前）

| 步骤 | 内容 | 产出 |
|------|------|------|
| 2.1 | 固件：USERPWD 改为 UID（改 1 行） | 新 .hex |
| 2.2 | 服务端：新增 /mqtt/auth + /mqtt/acl 接口 | 2 个路由 |
| 2.3 | EMQX：启用 HTTP Authn + Authz 插件 | emqx.conf |
| 2.4 | 回归测试：15 台老设备重新烧新固件，验证绑定关系不丢 | 验证报告 |

**Phase 2 不动数据库、不动绑定逻辑、不动客户侧**。已绑定的 15 台客户无感知升级。

### Phase 3：工厂工具化（量产）

| 步骤 | 内容 | 产出 |
|------|------|------|
| 3.1 | 工厂烧录+录入工具（串口读 UID → 调 API 录入 → 打印标签） | 工具脚本 |
| 3.2 | 批量生产流程文档 | SOP |

---

## 4. 风险与对策

| 风险 | 阶段 | 对策 |
|------|------|------|
| 短期 broker 裸奔被攻击 | Phase 1 | 15 台量小可接受；改非标准端口；EMQX 限流 |
| UID 与 SN 对应关系错乱 | Phase 1 | 上电读 UID 时立刻贴标，不拖延 |
| 客户扫码打不开页面 | Phase 1 | 备用手动输入 SN 的表单 |
| 长期升级后老设备连不上 | Phase 2 | 升级前给 15 台老设备重烧新固件（仅改 USERPWD 一行） |
| EMQX 回调接口宕机导致全部设备断连 | Phase 2 | Node.js 后端加健康检查；EMQX 配置缓存策略，允许短期回调失败时放行已认证设备 |

---

## 5. 决策记录

| 决策 | 选择 | 理由 |
|------|------|------|
| 设备身份 | 芯片 UID | 零成本、不可伪造、所有板子固件相同 |
| 绑定凭证 | SN（二维码）| 客户零输入，扫码即绑 |
| 认证方式 | password = UID | 明文 MQTT 下 HMAC 无实质提升，简化管理 |
| Topic 结构 | `dev/<UID>/up` 和 `dev/<UID>/down` | 简洁、UID 直接隔离、EMQX ACL 易校验 |
| 是否用 W25QXX 存密钥 | 否 | 增加工厂工具复杂度，无实质安全提升 |
| 是否用 ClaimCode | 否 | SN 已是物理编号，扫码绑定已足够 |
| 短期是否上 EMQX 认证 | 否 | 15 台小批量接受弱安全，量产后补 |
| 升级路径 | 固件改 1 行 + EMQX 加配置 | 零数据迁移、零客户感知 |

---

## 6. 附：UID 读取验证方法

固件烧录后，上电通过 RTT Viewer 或串口看到如下日志即表示 UID 读取成功：

```
[I] [LIFT_IOT] Device UID: 4A003258534B501620383552
[I] [LIFT_IOT] Device ID : lift_4A003258534B501620383552
```

15 台板子的 UID 应各不相同。若出现全 0 或全 F，检查芯片型号是否为 STM32F407（F4 系列 UID 地址统一为 `0x1FFF7A10`）。

---

## 7. 文件改动清单

### 短期方案（Phase 1）

| 文件 | 改动类型 | 说明 |
|------|---------|------|
| [APP/Inc/app_lift_iot.h](file:///e:/MCU/gaochang/code/f407zet6/APP/Inc/app_lift_iot.h) | 修改 | 加 UID 相关声明 |
| [APP/Src/app_lift_iot.c](file:///e:/MCU/gaochang/code/f407zet6/APP/Src/app_lift_iot.c) | 修改 | 加 ReadUID 函数，3 个 BuildJson 加 uid 字段 |
| [APP/Inc/app_tas_dtu.h](file:///e:/MCU/gaochang/code/f407zet6/APP/Inc/app_tas_dtu.h) | 修改 | 删硬编码 topic/clientid，加 topic 模板 |
| [APP/Src/app_tas_dtu.c](file:///e:/MCU/gaochang/code/f407zet6/APP/Src/app_tas_dtu.c) | 修改 | ConfigMqttChannel 改为 UID 派生 |
| 服务端 database.js | 修改 | 加 device_registry / binding_logs 表，devices 加字段 |
| 服务端 mqtt-bridge.js | 修改 | 用 uid 查设备，按 owner_id 推送 |
| 服务端 binding.js | 新增 | 绑定/解绑/列表 API + /bind 页面 |
| 服务端 server.js | 修改 | 注册 binding 路由 |

### 长期方案（Phase 2）

| 文件 | 改动类型 | 说明 |
|------|---------|------|
| [APP/Src/app_tas_dtu.c](file:///e:/MCU/gaochang/code/f407zet6/APP/Src/app_tas_dtu.c) | 修改 | USERPWD 改为 UID（1 行） |
| 服务端 mqtt-auth.js | 新增 | /mqtt/auth + /mqtt/acl 接口 |
| EMQX emqx.conf | 修改 | 启用 HTTP Authn + Authz |

---

**本方案核心**：一个 UID 贯穿始终，短期投入的所有代码和数据在长期方案中全部复用，升级时零数据迁移、零客户感知。
