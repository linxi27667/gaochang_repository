const express = require('express');
const { getDb } = require('../database');
const { authMiddleware, adminOnly } = require('./auth');
const { nowISO } = require('../utils');

const router = express.Router();

// 检查用户对某设备的访问权限(基于 device_bindings 多对多绑定)
// admin 可访问任意设备;user 只能访问自己有效绑定的设备
function checkDeviceAccess(db, req, deviceId) {
  const isAdmin = req.user && req.user.role === 'admin';
  if (isAdmin) return true;
  const userId = req.user && req.user.id;
  const row = db.prepare(
    "SELECT 1 FROM device_bindings WHERE device_id = ? AND user_id = ? AND status = 'active'"
  ).get(deviceId, userId);
  return !!row;
}

// 公共:非 admin 强制只看自己有效绑定设备的维保记录
function appendBindingScope(sql, isAdmin, params, userId) {
  if (!isAdmin) {
    sql += ` AND EXISTS (
      SELECT 1 FROM device_bindings b
      WHERE b.device_id = d.device_id AND b.user_id = ? AND b.status = 'active'
    )`;
    params.push(userId);
  }
  return sql;
}

router.get('/', authMiddleware, (req, res) => {
  const db = getDb();
  const { device_id, type } = req.query;
  const isAdmin = req.user && req.user.role === 'admin';
  const userId = req.user && req.user.id;

  let sql = `SELECT m.*, d.name AS device_name
             FROM maintenance_records m
             LEFT JOIN devices d ON m.device_id = d.device_id
             WHERE 1=1`;
  const params = [];

  // 非 admin 强制只看自己有效绑定设备的维保记录
  sql = appendBindingScope(sql, isAdmin, params, userId);
  if (device_id) { sql += ' AND m.device_id = ?'; params.push(device_id); }
  if (type) { sql += ' AND m.type = ?'; params.push(type); }

  sql += ' ORDER BY m.id DESC LIMIT 500';
  const records = db.prepare(sql).all(...params);
  res.json(records);
});

// 按计划 P3:"清报警、清故障、参数配置、出厂登记和维保修改仅管理员可用"
// 因此维保记录的新增/编辑/删除全部收紧为仅 admin

router.post('/', authMiddleware, adminOnly, (req, res) => {
  const { device_id, type, description, handler, result, next_date, cost } = req.body;
  if (!device_id) return res.status(400).json({ error: '设备ID不能为空' });

  const db = getDb();

  const r = db.prepare(
    'INSERT INTO maintenance_records (device_id, type, description, handler, result, next_date, cost, created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?)'
  ).run(device_id, type || '保养', description || '', handler || '', result || '进行中', next_date || '', cost || 0, nowISO());

  db.prepare('INSERT INTO operation_logs (user_id, action, device_id, detail, result, created_at) VALUES (?, ?, ?, ?, ?, ?)')
    .run(req.user.id, '添加维修记录', device_id, type || '保养', '成功', nowISO());

  res.json({ id: r.lastInsertRowid, message: '添加成功' });
});

router.put('/:id', authMiddleware, adminOnly, (req, res) => {
  const db = getDb();
  const record = db.prepare('SELECT device_id FROM maintenance_records WHERE id = ?').get(req.params.id);
  if (!record) return res.status(404).json({ error: '记录不存在' });
  const { type, description, handler, result, next_date, cost } = req.body;
  const r = db.prepare(
    'UPDATE maintenance_records SET type=?, description=?, handler=?, result=?, next_date=?, cost=? WHERE id=?'
  ).run(type, description, handler, result, next_date, cost, req.params.id);
  if (r.changes === 0) return res.status(404).json({ error: '记录不存在' });
  res.json({ message: '更新成功' });
});

router.delete('/:id', authMiddleware, adminOnly, (req, res) => {
  const db = getDb();
  const record = db.prepare('SELECT device_id FROM maintenance_records WHERE id = ?').get(req.params.id);
  if (!record) return res.status(404).json({ error: '记录不存在' });

  db.prepare('DELETE FROM maintenance_records WHERE id = ?').run(req.params.id);

  db.prepare('INSERT INTO operation_logs (user_id, action, device_id, detail, result, created_at) VALUES (?, ?, ?, ?, ?, ?)')
    .run(req.user.id, '删除维修记录', record.device_id, `删除记录ID=${req.params.id}`, '成功', nowISO());

  res.json({ message: '删除成功' });
});

router.get('/export', authMiddleware, (req, res) => {
  const db = getDb();
  const isAdmin = req.user && req.user.role === 'admin';
  const userId = req.user && req.user.id;

  let sql = `
    SELECT m.*, d.name AS device_name
    FROM maintenance_records m
    LEFT JOIN devices d ON m.device_id = d.device_id
    WHERE 1=1`;
  const params = [];
  // 非 admin 强制只导出自己有效绑定设备的维保记录
  sql = appendBindingScope(sql, isAdmin, params, userId);
  sql += ' ORDER BY m.id DESC LIMIT 10000';
  const records = db.prepare(sql).all(...params);

  const BOM = '﻿';
  const header = '设备名称,设备编号,类型,描述,处理人,处理结果,下次保养日期,费用';
  const rows = records.map(r =>
    `"${r.device_name}","${r.device_id}","${r.type}","${r.description}","${r.handler}","${r.result}","${r.next_date}",${r.cost || 0}`
  );
  const csv = BOM + header + '\n' + rows.join('\n');

  res.setHeader('Content-Type', 'text/csv; charset=utf-8');
  res.setHeader('Content-Disposition', 'attachment; filename=maintenance_records.csv');
  res.send(csv);
});

// 兼容旧版前端的保养入口。
// 计数只能由设备 Flash 维护；此接口仅转发 maintenance_done MQTT 命令，
// 设备成功回执后由 mqtt-bridge.js 写入维护记录。
// POST /api/maintenance/register_done/GC-2026-00001
router.post('/register_done/:deviceId', authMiddleware, (req, res) => {
  const db = getDb();
  const { deviceId } = req.params;

  // 检查访问权限
  if (!checkDeviceAccess(db, req, deviceId)) {
    return res.status(403).json({ error: '无权操作该设备' });
  }

  const device = db.prepare('SELECT device_id, name FROM devices WHERE device_id = ?').get(deviceId);
  if (!device) return res.status(404).json({ error: '设备不存在' });

  // 复用命令路由的入队逻辑，确保旧版客户端也走同一套回执状态机。
  const commandsRouter = require('./commands');
  const msgId = commandsRouter.createMsgId();
  const queued = commandsRouter.enqueueAndSendCommand(
    db, deviceId, 'maintenance_done', msgId, {}, req.user
  );
  db.prepare('INSERT INTO operation_logs (user_id, action, device_id, detail, result, created_at) VALUES (?, ?, ?, ?, ?, ?)')
    .run(req.user.id, 'maintenance_done', deviceId, `device ${device.name} (legacy endpoint)`, queued.sent ? 'sent' : 'send_failed', nowISO());

  if (!queued.sent) {
    return res.status(503).json({
      error: queued.error || 'MQTT未连接，保养命令发送失败',
      msg_id: queued.msg_id || msgId,
      status: 'failed'
    });
  }

  res.json({
    message: queued.existing ? '保养命令已在等待设备确认' : '保养命令已发送，等待设备确认',
    msg_id: queued.msg_id,
    status: queued.status
  });
});

module.exports = router;
