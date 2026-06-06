const express = require('express');
const { getDb } = require('./database');
const { authMiddleware, roleMiddleware } = require('./auth');
const { sendCommand } = require('./mqtt-bridge');
const { nowISO } = require('./utils');

const router = express.Router();

function createMsgId() {
  return Date.now().toString(36) + Math.random().toString(36).substr(2, 5);
}

function enqueueAndSendCommand(db, deviceId, cmd, msgId, extra = {}) {
  db.prepare('INSERT INTO command_queue (device_id, cmd, msg_id, status, created_at) VALUES (?, ?, ?, ?, ?)')
    .run(deviceId, cmd, msgId, 'pending', nowISO());

  const sent = sendCommand(deviceId, cmd, msgId, extra);
  db.prepare("UPDATE command_queue SET status = ? WHERE msg_id = ? AND status = 'pending'")
    .run(sent ? 'sent' : 'send_failed', msgId);

  return sent;
}

router.post('/lock/:deviceId', authMiddleware, roleMiddleware('admin', 'operator'), (req, res) => {
  const { deviceId } = req.params;
  const msgId = createMsgId();

  const db = getDb();
  const device = db.prepare('SELECT device_id, name FROM devices WHERE device_id = ?').get(deviceId);
  if (!device) return res.status(404).json({ error: '设备不存在' });

  const sent = enqueueAndSendCommand(db, deviceId, 'lock', msgId);

  db.prepare('INSERT INTO operation_logs (user_id, action, device_id, detail, result, created_at) VALUES (?, ?, ?, ?, ?, ?)')
    .run(req.user.id, '锁机', deviceId, `远程锁机 ${device.name}`, sent ? '已发送' : '发送失败', nowISO());

  if (!sent) return res.status(503).json({ error: 'MQTT未连接，锁机命令发送失败', msg_id: msgId });

  db.prepare('UPDATE device_status SET locked = 1 WHERE device_id = ?').run(deviceId);
  res.json({ message: '锁机命令已发送', msg_id: msgId });
});

router.post('/unlock/:deviceId', authMiddleware, roleMiddleware('admin', 'operator'), (req, res) => {
  const { deviceId } = req.params;
  const msgId = createMsgId();

  const db = getDb();
  const device = db.prepare('SELECT device_id, name FROM devices WHERE device_id = ?').get(deviceId);
  if (!device) return res.status(404).json({ error: '设备不存在' });

  const sent = enqueueAndSendCommand(db, deviceId, 'unlock', msgId);

  db.prepare('INSERT INTO operation_logs (user_id, action, device_id, detail, result, created_at) VALUES (?, ?, ?, ?, ?, ?)')
    .run(req.user.id, '解锁', deviceId, `远程解锁 ${device.name}`, sent ? '已发送' : '发送失败', nowISO());

  if (!sent) return res.status(503).json({ error: 'MQTT未连接，解锁命令发送失败', msg_id: msgId });

  db.prepare('UPDATE device_status SET locked = 0 WHERE device_id = ?').run(deviceId);
  res.json({ message: '解锁命令已发送', msg_id: msgId });
});

router.post('/query/:deviceId', authMiddleware, (req, res) => {
  const { deviceId } = req.params;
  const msgId = createMsgId();

  const db = getDb();
  const device = db.prepare('SELECT device_id FROM devices WHERE device_id = ?').get(deviceId);
  if (!device) return res.status(404).json({ error: '设备不存在' });

  const sent = enqueueAndSendCommand(db, deviceId, 'get_status', msgId);
  if (!sent) return res.status(503).json({ error: 'MQTT未连接，查询命令发送失败', msg_id: msgId });

  res.json({ message: '查询命令已发送', msg_id: msgId });
});

router.post('/lock-all', authMiddleware, roleMiddleware('admin'), (req, res) => {
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

  db.prepare('INSERT INTO operation_logs (user_id, action, device_id, detail, result, created_at) VALUES (?, ?, ?, ?, ?, ?)')
    .run(req.user.id, '批量锁机', 'ALL', '全部设备', `共${devices.length}台`, nowISO());

  res.json({ message: `已向${devices.length}台设备发送锁机命令`, results });
});

router.post('/unlock-all', authMiddleware, roleMiddleware('admin'), (req, res) => {
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

  db.prepare('INSERT INTO operation_logs (user_id, action, device_id, detail, result, created_at) VALUES (?, ?, ?, ?, ?, ?)')
    .run(req.user.id, '批量解锁', 'ALL', '全部设备', `共${devices.length}台`, nowISO());

  res.json({ message: `已向${devices.length}台设备发送解锁命令`, results });
});

router.post('/rename/:deviceId', authMiddleware, roleMiddleware('admin'), (req, res) => {
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

  // 设备确认回执也会再次校正名称，这里先乐观更新用于界面即时反馈。
  db.prepare('UPDATE devices SET name = ? WHERE device_id = ?').run(name.trim(), deviceId);

  db.prepare('INSERT INTO operation_logs (user_id, action, device_id, detail, result, created_at) VALUES (?, ?, ?, ?, ?, ?)')
    .run(req.user.id, '修改设备名称', deviceId, `${device.name} → ${name.trim()}`, '已发送', nowISO());

  res.json({ message: '改名命令已发送，等待设备确认', msg_id: msgId, old_name: device.name, new_name: name.trim() });
});

module.exports = router;
