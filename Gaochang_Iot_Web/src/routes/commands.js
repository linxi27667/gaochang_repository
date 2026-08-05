const express = require('express');
const { getDb } = require('../database');
const { authMiddleware, adminOnly } = require('./auth');
const { sendCommand } = require('../mqtt-bridge');
const { nowISO } = require('../utils');

const router = express.Router();

const CMD_TIMEOUT_MS = parseInt(process.env.CMD_TIMEOUT_MS || '15000', 10);
const CMD_MAX_ATTEMPTS = parseInt(process.env.CMD_MAX_ATTEMPTS || '2', 10);

function createMsgId() {
  return 'cmd_' + Date.now().toString(36) + Math.random().toString(36).substr(2, 8);
}

// 检查用户对某设备的访问权限(基于 device_bindings 多对多绑定)
// admin 可访问任意设备;user 只能操作自己有效绑定的设备
function checkDeviceAccess(db, req, deviceId) {
  const isAdmin = req.user && req.user.role === 'admin';
  if (isAdmin) return true;
  const userId = req.user && req.user.id;
  if (!userId) return false;
  const row = db.prepare(
    "SELECT 1 FROM device_bindings WHERE device_id = ? AND user_id = ? AND status = 'active'"
  ).get(deviceId, userId);
  return !!row;
}

// 中间件:校验 :deviceId 的访问权限
function requireDeviceAccess(req, res, next) {
  const db = getDb();
  const deviceId = req.params.deviceId;
  if (!checkDeviceAccess(db, req, deviceId)) {
    return res.status(403).json({ error: '无权操作该设备' });
  }
  next();
}

// 命令入队并发送:使用新的命令状态机
// 关键改动:不再在命令发送时立即修改设备状态,只更新命令队列
// 返回 { msg_id, status } 供调用方使用
function enqueueAndSendCommand(db, deviceId, cmd, msgId, extra = {}, operator = null) {
  const now = nowISO();
  // Destructive commands are idempotent by persistent firmware msg_id dedupe.
  const maxAttempts = CMD_MAX_ATTEMPTS;

  // A second browser tab must not create a new maintenance message while the
  // first one is still awaiting the device response. Reuse the active message
  // ID so the firmware cannot count one physical service twice.
  if (cmd === 'maintenance_done') {
    const active = db.prepare(`
      SELECT msg_id, status
      FROM command_queue
      WHERE device_id = ? AND cmd = ? AND status IN ('pending', 'sent')
      ORDER BY id DESC LIMIT 1
    `).get(deviceId, cmd);
    if (active) {
      return { msg_id: active.msg_id, sent: true, status: active.status, existing: true, error: '' };
    }
  }

  // 查找设备的 chip_uid
  let chipUid = '';
  try {
    const device = db.prepare('SELECT uid FROM devices WHERE device_id = ?').get(deviceId);
    if (device && device.uid) chipUid = device.uid;
    if (!chipUid) {
      const registry = db.prepare('SELECT uid FROM device_registry WHERE serial = ? OR bound_device_id = ?').get(deviceId, deviceId);
      if (registry && registry.uid) chipUid = registry.uid;
    }
  } catch (e) {
    // ignore
  }

  if (!chipUid) {
    console.warn(`[CMD] rejected stage=preflight reason=missing_uid device=${deviceId} cmd=${cmd} msg_id=${msgId}`);
    return { msg_id: msgId, sent: false, status: 'failed', error: '设备未登记芯片 UID，无法发送命令' };
  }
  const online = db.prepare('SELECT online FROM device_status WHERE device_id = ?').get(deviceId);
  if (!online || !online.online) {
    console.warn(`[CMD] rejected stage=preflight reason=device_offline device=${deviceId} uid=${chipUid} cmd=${cmd} msg_id=${msgId}`);
    return { msg_id: msgId, sent: false, status: 'failed', error: '设备当前离线，未发送查询命令' };
  }
  console.log(`[CMD] created device=${deviceId} uid=${chipUid} cmd=${cmd} msg_id=${msgId} at_ms=${Date.now()}`);

  // 入队:status=pending,设置 timeout_at 和 max_attempts
  db.prepare(`
    INSERT INTO command_queue
      (device_id, cmd, msg_id, status, created_at, chip_uid, operator_id, operator_name,
       args_json, attempts, max_attempts, timeout_at)
    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, 0, ?, NULL)
  `).run(
    deviceId, cmd, msgId, 'pending', now,
    chipUid,
    operator ? operator.id : null,
    operator ? operator.username : '',
    JSON.stringify(extra.args || extra || {}),
    maxAttempts
  );

  // 发送命令
  const sent = sendCommand(deviceId, cmd, msgId, extra);

  // sendCommand 内部会调用 updateCommandSent 更新状态为 sent
  // 如果发送失败,状态保持 pending,会被超时检查重试
  if (!sent) {
    db.prepare("UPDATE command_queue SET status = 'failed', result = 'MQTT not connected' WHERE msg_id = ?")
      .run(msgId);
  }

  return { msg_id: msgId, sent, status: sent ? 'sent' : 'failed', error: sent ? '' : 'MQTT 未连接或发布失败' };
}

// 允许的能力字段白名单,防止 SQL 注入
const ACCESSORY_FIELDS = new Set(['has_encoder', 'has_buzzer', 'has_pressure_sensor', 'has_display']);

function requireAccessory(db, deviceId, field, label, res) {
  if (!ACCESSORY_FIELDS.has(field)) {
    res.status(400).json({ error: `无效的能力字段: ${field}` });
    return false;
  }
  const row = db.prepare(`SELECT ${field} AS enabled FROM devices WHERE device_id = ?`).get(deviceId);
  if (!row) {
    res.status(404).json({ error: '设备不存在' });
    return false;
  }
  if (!row.enabled) {
    res.status(400).json({ error: `该设备未配置${label}` });
    return false;
  }
  return true;
}

// 记录平台操作日志
function logOp(userId, action, deviceId, detail, result) {
  const db = getDb();
  db.prepare('INSERT INTO operation_logs (user_id, action, device_id, detail, result, created_at) VALUES (?, ?, ?, ?, ?, ?)')
    .run(userId, action, deviceId, detail, result, nowISO());
}

// ============ 锁机/解锁(普通用户可操作,需绑定) ============
// 普通用户只能操作自己有效绑定的设备,admin 可操作任意设备

router.post('/lock/:deviceId', authMiddleware, requireDeviceAccess, (req, res) => {
  const { deviceId } = req.params;
  const msgId = createMsgId();

  const db = getDb();
  const device = db.prepare('SELECT device_id, name FROM devices WHERE device_id = ?').get(deviceId);
  if (!device) return res.status(404).json({ error: '设备不存在' });

  // 关键改动:不再在命令发送时立即修改 device_status.locked
  // 只有收到 command_response(result=succeeded)后才更新设备状态
  const { sent, status } = enqueueAndSendCommand(db, deviceId, 'lock', msgId, {}, req.user);
  logOp(req.user.id, '锁机', deviceId, `远程锁机 ${device.name}`, sent ? '已发送' : '发送失败');

  if (!sent) return res.status(503).json({ error: 'MQTT未连接，锁机命令发送失败', msg_id: msgId, status: 'failed' });

  res.json({ message: '锁机命令已发送，等待设备确认', msg_id: msgId, status });
});

router.post('/unlock/:deviceId', authMiddleware, requireDeviceAccess, (req, res) => {
  const { deviceId } = req.params;
  const msgId = createMsgId();

  const db = getDb();
  const device = db.prepare('SELECT device_id, name FROM devices WHERE device_id = ?').get(deviceId);
  if (!device) return res.status(404).json({ error: '设备不存在' });

  const { sent, status } = enqueueAndSendCommand(db, deviceId, 'unlock', msgId, {}, req.user);
  logOp(req.user.id, '解锁', deviceId, `远程解锁 ${device.name}`, sent ? '已发送' : '发送失败');

  if (!sent) return res.status(503).json({ error: 'MQTT未连接，解锁命令发送失败', msg_id: msgId, status: 'failed' });

  res.json({ message: '解锁命令已发送，等待设备确认', msg_id: msgId, status });
});

// 查询设备状态(绑定该设备的用户或 admin 均可)
router.post('/query/:deviceId', authMiddleware, requireDeviceAccess, (req, res) => {
  const { deviceId } = req.params;
  const msgId = createMsgId();

  const db = getDb();
  const device = db.prepare('SELECT device_id FROM devices WHERE device_id = ?').get(deviceId);
  if (!device) return res.status(404).json({ error: '设备不存在' });

  const { sent, status, error } = enqueueAndSendCommand(db, deviceId, 'get_status', msgId, {}, req.user);
  if (!sent) return res.status(503).json({ error: error || '查询命令发送失败', msg_id: msgId, status: 'failed' });

  res.json({ message: '查询命令已发送', msg_id: msgId, status });
});

// 批量锁机/解锁(仅 admin)
// 保养与使用计数只由设备成功回执更新，避免 Web 端乐观修改。
router.post('/maintenance_done/:deviceId', authMiddleware, requireDeviceAccess, (req, res) => {
  const { deviceId } = req.params;
  const db = getDb();
  const device = db.prepare('SELECT device_id, name FROM devices WHERE device_id = ?').get(deviceId);
  if (!device) return res.status(404).json({ error: '设备不存在' });
  const msgId = createMsgId();
  const queued = enqueueAndSendCommand(db, deviceId, 'maintenance_done', msgId, {}, req.user);
  const { sent, status } = queued;
  logOp(req.user.id, 'maintenance_done', deviceId, `device ${device.name}`, sent ? 'sent' : 'send_failed');
  if (!sent) return res.status(503).json({ error: queued.error || 'MQTT not connected', msg_id: queued.msg_id || msgId, status: 'failed' });
  res.json({ message: queued.existing ? 'maintenance command already pending' : 'maintenance command sent, awaiting device response', msg_id: queued.msg_id, status });
});

router.post('/reset_usage/:deviceId', authMiddleware, adminOnly, requireDeviceAccess, (req, res) => {
  const { deviceId } = req.params;
  if (!req.body || req.body.confirm_device_id !== deviceId) {
    return res.status(400).json({ error: 'confirm_device_id must exactly match deviceId' });
  }
  const db = getDb();
  const device = db.prepare('SELECT device_id, name FROM devices WHERE device_id = ?').get(deviceId);
  if (!device) return res.status(404).json({ error: 'device not found' });
  const msgId = createMsgId();
  const { sent, status } = enqueueAndSendCommand(db, deviceId, 'reset_usage', msgId, {}, req.user);
  logOp(req.user.id, 'reset_usage', deviceId, `device ${device.name}; device ID confirmed`, sent ? 'sent' : 'send_failed');
  if (!sent) return res.status(503).json({ error: 'MQTT not connected', msg_id: msgId, status: 'failed' });
  res.json({ message: 'usage reset command sent, awaiting device response', msg_id: msgId, status });
});

router.post('/lock-all', authMiddleware, adminOnly, (req, res) => {
  const db = getDb();
  const devices = db.prepare('SELECT device_id FROM devices').all();
  const results = [];

  for (const d of devices) {
    const msgId = createMsgId();
    const { sent, status } = enqueueAndSendCommand(db, d.device_id, 'lock', msgId, {}, req.user);
    // 不再立即修改 device_status.locked
    results.push({ device_id: d.device_id, msg_id: msgId, sent, status });
  }

  logOp(req.user.id, '批量锁机', 'ALL', '全部设备', `共${devices.length}台`);
  res.json({ message: `已向${devices.length}台设备发送锁机命令`, results });
});

router.post('/unlock-all', authMiddleware, adminOnly, (req, res) => {
  const db = getDb();
  const devices = db.prepare('SELECT device_id FROM devices').all();
  const results = [];

  for (const d of devices) {
    const msgId = createMsgId();
    const { sent, status } = enqueueAndSendCommand(db, d.device_id, 'unlock', msgId, {}, req.user);
    results.push({ device_id: d.device_id, msg_id: msgId, sent, status });
  }

  logOp(req.user.id, '批量解锁', 'ALL', '全部设备', `共${devices.length}台`);
  res.json({ message: `已向${devices.length}台设备发送解锁命令`, results });
});

// ============ 蜂鸣器(普通用户可操作,需绑定+能力校验) ============

router.post('/buzzer_on/:deviceId', authMiddleware, requireDeviceAccess, (req, res) => {
  const { deviceId } = req.params;
  const msgId = createMsgId();
  const db = getDb();
  const device = db.prepare('SELECT device_id, name FROM devices WHERE device_id = ?').get(deviceId);
  if (!device) return res.status(404).json({ error: '设备不存在' });
  if (!requireAccessory(db, deviceId, 'has_buzzer', '蜂鸣器', res)) return;

  const { sent, status } = enqueueAndSendCommand(db, deviceId, 'buzzer_on', msgId, {}, req.user);
  logOp(req.user.id, '开启蜂鸣器', deviceId, `开启蜂鸣器 ${device.name}`, sent ? '已发送' : '发送失败');

  if (!sent) return res.status(503).json({ error: 'MQTT未连接，命令发送失败', msg_id: msgId, status: 'failed' });
  res.json({ message: '蜂鸣器开启命令已发送，等待设备确认', msg_id: msgId, status });
});

router.post('/buzzer_off/:deviceId', authMiddleware, requireDeviceAccess, (req, res) => {
  const { deviceId } = req.params;
  const msgId = createMsgId();
  const db = getDb();
  const device = db.prepare('SELECT device_id, name FROM devices WHERE device_id = ?').get(deviceId);
  if (!device) return res.status(404).json({ error: '设备不存在' });
  if (!requireAccessory(db, deviceId, 'has_buzzer', '蜂鸣器', res)) return;

  const { sent, status } = enqueueAndSendCommand(db, deviceId, 'buzzer_off', msgId, {}, req.user);
  logOp(req.user.id, '关闭蜂鸣器', deviceId, `关闭蜂鸣器 ${device.name}`, sent ? '已发送' : '发送失败');

  if (!sent) return res.status(503).json({ error: 'MQTT未连接，命令发送失败', msg_id: msgId, status: 'failed' });
  res.json({ message: '蜂鸣器关闭命令已发送，等待设备确认', msg_id: msgId, status });
});

// ============ 清除报警(绑定用户或 admin) ============

router.post('/clear_alarm/:deviceId', authMiddleware, requireDeviceAccess, (req, res) => {
  const { deviceId } = req.params;
  const msgId = createMsgId();
  const db = getDb();
  const device = db.prepare('SELECT device_id, name FROM devices WHERE device_id = ?').get(deviceId);
  if (!device) return res.status(404).json({ error: '设备不存在' });

  const capability = db.prepare('SELECT product_type FROM devices WHERE device_id = ?').get(deviceId);
  if (!capability || !['small_scissor', 'thin_scissor', 'large_scissor'].includes(capability.product_type)) {
    return res.status(400).json({ error: 'product does not support photoelectric alarm clearing' });
  }
  const { sent, status } = enqueueAndSendCommand(db, deviceId, 'clear_alarm', msgId, {}, req.user);
  logOp(req.user.id, '清除报警', deviceId, `远程清除报警 ${device.name}`, sent ? '已发送' : '发送失败');

  if (!sent) return res.status(503).json({ error: 'MQTT未连接，命令发送失败', msg_id: msgId, status: 'failed' });

  res.json({ message: '清除报警命令已发送，等待设备确认', msg_id: msgId, status });
});

// ============ 清除故障(仅 admin) ============
// 清故障与清报警分离:clear_alarm 清报警状态,fault_clear 清故障状态
router.post('/fault_clear/:deviceId', authMiddleware, adminOnly, requireDeviceAccess, (req, res) => {
  const { deviceId } = req.params;
  const msgId = createMsgId();
  const db = getDb();
  const device = db.prepare('SELECT device_id, name FROM devices WHERE device_id = ?').get(deviceId);
  if (!device) return res.status(404).json({ error: '设备不存在' });

  const { sent, status } = enqueueAndSendCommand(db, deviceId, 'fault_clear', msgId, {}, req.user);
  logOp(req.user.id, '清除故障', deviceId, `远程清除故障 ${device.name}`, sent ? '已发送' : '发送失败');

  if (!sent) return res.status(503).json({ error: 'MQTT未连接，命令发送失败', msg_id: msgId, status: 'failed' });

  res.json({ message: '清除故障命令已发送，等待设备确认', msg_id: msgId, status });
});

// ============ 改名 ============
// 直接写入数据库，无需设备确认（名字只是平台显示用，设备不关心）

router.post('/rename/:deviceId', authMiddleware, requireDeviceAccess, (req, res) => {
  const { deviceId } = req.params;
  const { name } = req.body;
  if (!name || name.trim().length === 0) return res.status(400).json({ error: '名称不能为空' });
  if (name.length > 50) return res.status(400).json({ error: '名称不能超过50字符' });

  const db = getDb();
  const device = db.prepare('SELECT device_id, name FROM devices WHERE device_id = ?').get(deviceId);
  if (!device) return res.status(404).json({ error: '设备不存在' });

  const oldName = device.name;
  db.prepare('UPDATE devices SET name = ? WHERE device_id = ?').run(name.trim(), deviceId);

  logOp(req.user.id, '修改设备名称', deviceId, `${oldName} → ${name.trim()}`, '成功');

  res.json({ message: '设备名称已更新', old_name: oldName, new_name: name.trim() });
});

// ============ 下发配置(仅 admin) ============
router.post('/set_config/:deviceId', authMiddleware, adminOnly, requireDeviceAccess, (req, res) => {
  const { deviceId } = req.params;
  const msgId = createMsgId();
  const db = getDb();
  const device = db.prepare('SELECT device_id, name FROM devices WHERE device_id = ?').get(deviceId);
  if (!device) return res.status(404).json({ error: '设备不存在' });

  const config = req.body || {};
  const { sent, status } = enqueueAndSendCommand(db, deviceId, 'set_config', msgId, config, req.user);
  logOp(req.user.id, '下发配置', deviceId,
    `下发配置到 ${device.name}: ${JSON.stringify(config)}`,
    sent ? '已发送' : '发送失败');

  if (!sent) return res.status(503).json({ error: 'MQTT未连接，命令发送失败', msg_id: msgId, status: 'failed' });
  res.json({ message: '配置已下发，等待设备确认', msg_id: msgId, status });
});

// 查询设备配置(仅 admin)
router.post('/get_config/:deviceId', authMiddleware, adminOnly, requireDeviceAccess, (req, res) => {
  const { deviceId } = req.params;
  const msgId = createMsgId();
  const db = getDb();
  const device = db.prepare('SELECT device_id FROM devices WHERE device_id = ?').get(deviceId);
  if (!device) return res.status(404).json({ error: '设备不存在' });

  const { sent, status } = enqueueAndSendCommand(db, deviceId, 'get_config', msgId, {}, req.user);
  if (!sent) return res.status(503).json({ error: 'MQTT未连接，命令发送失败', msg_id: msgId, status: 'failed' });
  res.json({ message: '查询配置命令已发送', msg_id: msgId, status });
});

// ============ 命令状态查询 ============
// GET /api/commands/status/:msgId
// 返回命令的当前状态:pending / sent / succeeded / rejected / timeout / failed
router.get('/status/:msgId', authMiddleware, (req, res) => {
  const { msgId } = req.params;
  const db = getDb();
  const cmd = db.prepare(`
    SELECT id, device_id, cmd, msg_id, status, result, attempts, max_attempts,
           created_at, sent_at, responded_at, timeout_at, operator_id, operator_name
    FROM command_queue
    WHERE msg_id = ?
  `).get(msgId);

  if (!cmd) {
    return res.status(404).json({ error: '命令不存在' });
  }

  // 权限校验:普通用户只能查询自己发起的命令或自己绑定设备的命令
  const isAdmin = req.user && req.user.role === 'admin';
  if (!isAdmin) {
    const hasAccess = checkDeviceAccess(db, req, cmd.device_id);
    const isOwner = cmd.operator_id === req.user.id;
    if (!hasAccess && !isOwner) {
      return res.status(403).json({ error: '无权查询该命令' });
    }
  }

  res.json(cmd);
});

// ============ 命令历史查询 ============
// GET /api/commands/history/:deviceId
// 返回指定设备的命令历史(分页)
router.get('/history/:deviceId', authMiddleware, requireDeviceAccess, (req, res) => {
  const { deviceId } = req.params;
  const limit = Math.min(parseInt(req.query.limit || '50', 10), 200);
  const offset = Math.max(parseInt(req.query.offset || '0', 10), 0);

  const db = getDb();
  const commands = db.prepare(`
    SELECT id, device_id, cmd, msg_id, status, result, attempts, max_attempts,
           created_at, sent_at, responded_at, operator_name
    FROM command_queue
    WHERE device_id = ?
    ORDER BY id DESC
    LIMIT ? OFFSET ?
  `).all(deviceId, limit, offset);

  const total = db.prepare('SELECT COUNT(*) as cnt FROM command_queue WHERE device_id = ?').get(deviceId).cnt;

  res.json({ commands, total, limit, offset });
});

// 注意:set_product_type 接口已移除
// 产品型号由固件固定,禁止通过平台远程切换型号

// Expose the command primitives for the legacy maintenance endpoint. Express
// still receives the router function; these properties are only an internal
// compatibility bridge so both endpoints share one queue implementation.
module.exports = router;
module.exports.createMsgId = createMsgId;
module.exports.enqueueAndSendCommand = enqueueAndSendCommand;
