const express = require('express');
const { getDb } = require('../database');
const { authMiddleware, adminOnly } = require('./auth');
const { nowISO } = require('../utils');

const router = express.Router();

// 检查用户对某设备的访问权限(绑定即有访问权)
function checkDeviceAccess(db, req, deviceId) {
  const isAdmin = req.user && req.user.role === 'admin';
  if (isAdmin) return true;
  const userId = req.user && req.user.id;
  const row = db.prepare('SELECT 1 FROM devices WHERE device_id = ? AND owner_id = ?').get(deviceId, userId);
  return !!row;
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

  // 非 admin 强制只看自己绑定设备的维保记录
  if (!isAdmin) {
    sql += ' AND d.owner_id = ?';
    params.push(userId);
  }
  if (device_id) { sql += ' AND m.device_id = ?'; params.push(device_id); }
  if (type) { sql += ' AND m.type = ?'; params.push(type); }

  sql += ' ORDER BY m.id DESC LIMIT 500';
  const records = db.prepare(sql).all(...params);
  res.json(records);
});

router.post('/', authMiddleware, (req, res) => {
  const { device_id, type, description, handler, result, next_date, cost } = req.body;
  if (!device_id) return res.status(400).json({ error: '设备ID不能为空' });

  const db = getDb();
  // 权限校验:必须绑定该设备才能添加维保记录
  if (!checkDeviceAccess(db, req, device_id)) {
    return res.status(403).json({ error: '无权操作该设备' });
  }

  const r = db.prepare(
    'INSERT INTO maintenance_records (device_id, type, description, handler, result, next_date, cost, created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?)'
  ).run(device_id, type || '保养', description || '', handler || '', result || '进行中', next_date || '', cost || 0, nowISO());

  db.prepare('INSERT INTO operation_logs (user_id, action, device_id, detail, result, created_at) VALUES (?, ?, ?, ?, ?, ?)')
    .run(req.user.id, '添加维修记录', device_id, type || '保养', '成功', nowISO());

  res.json({ id: r.lastInsertRowid, message: '添加成功' });
});

router.put('/:id', authMiddleware, (req, res) => {
  const db = getDb();
  // 先查出记录,校验权限
  const record = db.prepare('SELECT device_id FROM maintenance_records WHERE id = ?').get(req.params.id);
  if (!record) return res.status(404).json({ error: '记录不存在' });
  if (!checkDeviceAccess(db, req, record.device_id)) {
    return res.status(403).json({ error: '无权操作该设备的维保记录' });
  }
  const { type, description, handler, result, next_date, cost } = req.body;
  const r = db.prepare(
    'UPDATE maintenance_records SET type=?, description=?, handler=?, result=?, next_date=?, cost=? WHERE id=?'
  ).run(type, description, handler, result, next_date, cost, req.params.id);
  if (r.changes === 0) return res.status(404).json({ error: '记录不存在' });
  res.json({ message: '更新成功' });
});

router.delete('/:id', authMiddleware, (req, res) => {
  const db = getDb();
  const record = db.prepare('SELECT device_id FROM maintenance_records WHERE id = ?').get(req.params.id);
  if (!record) return res.status(404).json({ error: '记录不存在' });
  if (!checkDeviceAccess(db, req, record.device_id)) {
    return res.status(403).json({ error: '无权操作该设备的维保记录' });
  }

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
  if (!isAdmin) {
    sql += ' AND d.owner_id = ?';
    params.push(userId);
  }
  sql += ' ORDER BY m.id DESC';
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

module.exports = router;