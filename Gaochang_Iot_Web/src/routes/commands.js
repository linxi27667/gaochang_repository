const express = require('express');
const { getDb, PRODUCT_CONFIGS } = require('../database');
const { authMiddleware, adminOnly } = require('./auth');
const { sendCommand } = require('../mqtt-bridge');
const { nowISO } = require('../utils');

const router = express.Router();

// 合法的产品型号
const VALID_PRODUCT_TYPES = PRODUCT_CONFIGS.map(p => p.product_type);

function createMsgId() {
  return Date.now().toString(36) + Math.random().toString(36).substr(2, 5);
}

// 检查用户对某设备的访问权限
// admin 可访问任意设备;user 只能操作自己绑定的设备
function checkDeviceAccess(db, req, deviceId) {
  const isAdmin = req.user && req.user.role === 'admin';
  if (isAdmin) return true;
  const userId = req.user && req.user.id;
  const row = db.prepare('SELECT 1 FROM devices WHERE device_id = ? AND owner_id = ?').get(deviceId, userId);
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

function enqueueAndSendCommand(db, deviceId, cmd, msgId, extra = {}) {
  db.prepare('INSERT INTO command_queue (device_id, cmd, msg_id, status, created_at) VALUES (?, ?, ?, ?, ?)')
    .run(deviceId, cmd, msgId, 'pending', nowISO());

  const sent = sendCommand(deviceId, cmd, msgId, extra);
  db.prepare("UPDATE command_queue SET status = ? WHERE msg_id = ? AND status = 'pending'")
    .run(sent ? 'sent' : 'send_failed', msgId);

  return sent;
}

function requireAccessory(db, deviceId, field, label, res) {
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

// ============ 锁机/解锁(仅 admin) ============

router.post('/lock/:deviceId', authMiddleware, adminOnly, requireDeviceAccess, (req, res) => {
  const { deviceId } = req.params;
  const msgId = createMsgId();

  const db = getDb();
  const device = db.prepare('SELECT device_id, name FROM devices WHERE device_id = ?').get(deviceId);
  if (!device) return res.status(404).json({ error: '设备不存在' });

  const sent = enqueueAndSendCommand(db, deviceId, 'lock', msgId);
  logOp(req.user.id, '锁机', deviceId, `远程锁机 ${device.name}`, sent ? '已发送' : '发送失败');

  if (!sent) return res.status(503).json({ error: 'MQTT未连接，锁机命令发送失败', msg_id: msgId });

  db.prepare('UPDATE device_status SET locked = 1 WHERE device_id = ?').run(deviceId);
  res.json({ message: '锁机命令已发送', msg_id: msgId });
});

router.post('/unlock/:deviceId', authMiddleware, adminOnly, requireDeviceAccess, (req, res) => {
  const { deviceId } = req.params;
  const msgId = createMsgId();

  const db = getDb();
  const device = db.prepare('SELECT device_id, name FROM devices WHERE device_id = ?').get(deviceId);
  if (!device) return res.status(404).json({ error: '设备不存在' });

  const sent = enqueueAndSendCommand(db, deviceId, 'unlock', msgId);
  logOp(req.user.id, '解锁', deviceId, `远程解锁 ${device.name}`, sent ? '已发送' : '发送失败');

  if (!sent) return res.status(503).json({ error: 'MQTT未连接，解锁命令发送失败', msg_id: msgId });

  db.prepare('UPDATE device_status SET locked = 0 WHERE device_id = ?').run(deviceId);
  res.json({ message: '解锁命令已发送', msg_id: msgId });
});

// 查询设备状态(绑定该设备的用户或 admin 均可)
router.post('/query/:deviceId', authMiddleware, requireDeviceAccess, (req, res) => {
  const { deviceId } = req.params;
  const msgId = createMsgId();

  const db = getDb();
  const device = db.prepare('SELECT device_id FROM devices WHERE device_id = ?').get(deviceId);
  if (!device) return res.status(404).json({ error: '设备不存在' });

  const sent = enqueueAndSendCommand(db, deviceId, 'get_status', msgId);
  if (!sent) return res.status(503).json({ error: 'MQTT未连接，查询命令发送失败', msg_id: msgId });

  res.json({ message: '查询命令已发送', msg_id: msgId });
});

// 批量锁机/解锁(仅 admin)
router.post('/lock-all', authMiddleware, adminOnly, (req, res) => {
  const db = getDb();
  const devices = db.prepare('SELECT device_id FROM devices').all();
  const results = [];

  for (const d of devices) {
    const msgId = createMsgId();
    const sent = enqueueAndSendCommand(db, d.device_id, 'lock', msgId);
    if (sent) {
      db.prepare('UPDATE device_status SET locked = 1 WHERE device_id = ?').run(d.device_id);
    }
    results.push({ device_id: d.device_id, msg_id: msgId, sent });
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
    const sent = enqueueAndSendCommand(db, d.device_id, 'unlock', msgId);
    if (sent) {
      db.prepare('UPDATE device_status SET locked = 0 WHERE device_id = ?').run(d.device_id);
    }
    results.push({ device_id: d.device_id, msg_id: msgId, sent });
  }

  logOp(req.user.id, '批量解锁', 'ALL', '全部设备', `共${devices.length}台`);
  res.json({ message: `已向${devices.length}台设备发送解锁命令`, results });
});

// ============ 蜂鸣器(仅 admin) ============

router.post('/buzzer_on/:deviceId', authMiddleware, adminOnly, requireDeviceAccess, (req, res) => {
  const { deviceId } = req.params;
  const msgId = createMsgId();
  const db = getDb();
  const device = db.prepare('SELECT device_id, name FROM devices WHERE device_id = ?').get(deviceId);
  if (!device) return res.status(404).json({ error: '设备不存在' });
  if (!requireAccessory(db, deviceId, 'has_buzzer', '蜂鸣器', res)) return;

  const sent = enqueueAndSendCommand(db, deviceId, 'buzzer_on', msgId);
  logOp(req.user.id, '开启蜂鸣器', deviceId, `开启蜂鸣器 ${device.name}`, sent ? '已发送' : '发送失败');

  if (!sent) return res.status(503).json({ error: 'MQTT未连接，命令发送失败', msg_id: msgId });
  res.json({ message: '蜂鸣器开启命令已发送，等待设备确认', msg_id: msgId });
});

router.post('/buzzer_off/:deviceId', authMiddleware, adminOnly, requireDeviceAccess, (req, res) => {
  const { deviceId } = req.params;
  const msgId = createMsgId();
  const db = getDb();
  const device = db.prepare('SELECT device_id, name FROM devices WHERE device_id = ?').get(deviceId);
  if (!device) return res.status(404).json({ error: '设备不存在' });
  if (!requireAccessory(db, deviceId, 'has_buzzer', '蜂鸣器', res)) return;

  const sent = enqueueAndSendCommand(db, deviceId, 'buzzer_off', msgId);
  logOp(req.user.id, '关闭蜂鸣器', deviceId, `关闭蜂鸣器 ${device.name}`, sent ? '已发送' : '发送失败');

  if (!sent) return res.status(503).json({ error: 'MQTT未连接，命令发送失败', msg_id: msgId });
  res.json({ message: '蜂鸣器关闭命令已发送，等待设备确认', msg_id: msgId });
});

// ============ 清除报警(仅 admin) ============

router.post('/clear_alarm/:deviceId', authMiddleware, adminOnly, requireDeviceAccess, (req, res) => {
  const { deviceId } = req.params;
  const msgId = createMsgId();
  const db = getDb();
  const device = db.prepare('SELECT device_id, name FROM devices WHERE device_id = ?').get(deviceId);
  if (!device) return res.status(404).json({ error: '设备不存在' });

  const sent = enqueueAndSendCommand(db, deviceId, 'clear_alarm', msgId);
  logOp(req.user.id, '清除报警', deviceId, `远程清除报警 ${device.name}`, sent ? '已发送' : '发送失败');

  if (!sent) return res.status(503).json({ error: 'MQTT未连接，命令发送失败', msg_id: msgId });

  res.json({ message: '清除报警命令已发送，等待设备确认', msg_id: msgId });
});

// ============ 改名(仅 admin) ============

router.post('/rename/:deviceId', authMiddleware, adminOnly, requireDeviceAccess, (req, res) => {
  const { deviceId } = req.params;
  const { name } = req.body;
  if (!name || name.trim().length === 0) return res.status(400).json({ error: '名称不能为空' });
  if (name.length > 32) return res.status(400).json({ error: '名称不能超过32字符' });

  const msgId = createMsgId();
  const db = getDb();
  const device = db.prepare('SELECT device_id, name FROM devices WHERE device_id = ?').get(deviceId);
  if (!device) return res.status(404).json({ error: '设备不存在' });

  const sent = enqueueAndSendCommand(db, deviceId, 'rename', msgId, { name: name.trim() });
  if (!sent) return res.status(503).json({ error: 'MQTT未连接，改名命令发送失败', msg_id: msgId });

  db.prepare('UPDATE devices SET name = ? WHERE device_id = ?').run(name.trim(), deviceId);
  logOp(req.user.id, '修改设备名称', deviceId, `${device.name} → ${name.trim()}`, '已发送');

  res.json({ message: '改名命令已发送，等待设备确认', msg_id: msgId, old_name: device.name, new_name: name.trim() });
});

// ============ 下发配置(仅 admin) ============
// POST /api/commands/set_config/:deviceId
// Body: { module_enable_mask, motor_hold_ms, motor_to_valve_delay_ms, ... }
router.post('/set_config/:deviceId', authMiddleware, adminOnly, requireDeviceAccess, (req, res) => {
  const { deviceId } = req.params;
  const msgId = createMsgId();
  const db = getDb();
  const device = db.prepare('SELECT device_id, name FROM devices WHERE device_id = ?').get(deviceId);
  if (!device) return res.status(404).json({ error: '设备不存在' });

  const config = req.body || {};
  const sent = enqueueAndSendCommand(db, deviceId, 'set_config', msgId, config);
  logOp(req.user.id, '下发配置', deviceId,
    `下发配置到 ${device.name}: ${JSON.stringify(config)}`,
    sent ? '已发送' : '发送失败');

  if (!sent) return res.status(503).json({ error: 'MQTT未连接，命令发送失败', msg_id: msgId });
  res.json({ message: '配置已下发，等待设备确认', msg_id: msgId });
});

// 查询设备配置(仅 admin)
router.post('/get_config/:deviceId', authMiddleware, adminOnly, requireDeviceAccess, (req, res) => {
  const { deviceId } = req.params;
  const msgId = createMsgId();
  const db = getDb();
  const device = db.prepare('SELECT device_id FROM devices WHERE device_id = ?').get(deviceId);
  if (!device) return res.status(404).json({ error: '设备不存在' });

  const sent = enqueueAndSendCommand(db, deviceId, 'get_config', msgId);
  if (!sent) return res.status(503).json({ error: 'MQTT未连接，命令发送失败', msg_id: msgId });
  res.json({ message: '查询配置命令已发送', msg_id: msgId });
});

// ============ 切换产品型号(仅 admin) ============
// POST /api/commands/set_product_type/:deviceId
// Body: { product_type: 'large_scissor' }
// 该命令下发后,设备会重新初始化对应产品型号的 IO 配置和函数指针
router.post('/set_product_type/:deviceId', authMiddleware, adminOnly, requireDeviceAccess, (req, res) => {
  const { deviceId } = req.params;
  const { product_type } = req.body;
  if (!VALID_PRODUCT_TYPES.includes(product_type)) {
    return res.status(400).json({
      error: `产品型号无效,可选: ${VALID_PRODUCT_TYPES.join(', ')}`
    });
  }

  const msgId = createMsgId();
  const db = getDb();
  const device = db.prepare('SELECT device_id, name, product_type FROM devices WHERE device_id = ?').get(deviceId);
  if (!device) return res.status(404).json({ error: '设备不存在' });

  const sent = enqueueAndSendCommand(db, deviceId, 'set_product_type', msgId, { product_type });
  logOp(req.user.id, '切换产品型号', deviceId,
    `${device.name}: ${device.product_type || 'double_post'} → ${product_type}`,
    sent ? '已发送' : '发送失败');

  if (!sent) return res.status(503).json({ error: 'MQTT未连接，命令发送失败', msg_id: msgId });

  res.json({
    message: '产品型号切换命令已发送，等待设备确认',
    msg_id: msgId,
    old_product_type: device.product_type || 'double_post',
    new_product_type: product_type
  });
});

module.exports = router;
