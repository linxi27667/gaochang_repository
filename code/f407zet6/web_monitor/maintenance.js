const express = require('express');
const { getDb } = require('./database');
const { authMiddleware, roleMiddleware } = require('./auth');
const { nowISO } = require('./utils');

const router = express.Router();

router.get('/', authMiddleware, (req, res) => {
  const db = getDb();
  const { device_id, type } = req.query;

  let sql = `SELECT m.*, d.name AS device_name
             FROM maintenance_records m
             LEFT JOIN devices d ON m.device_id = d.device_id
             WHERE 1=1`;
  const params = [];

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
  const r = db.prepare(
    'INSERT INTO maintenance_records (device_id, type, description, handler, result, next_date, cost, created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?)'
  ).run(device_id, type || '保养', description || '', handler || '', result || '进行中', next_date || '', cost || 0, nowISO());

  db.prepare('INSERT INTO operation_logs (user_id, action, device_id, detail, result, created_at) VALUES (?, ?, ?, ?, ?, ?)')
    .run(req.user.id, '添加维修记录', device_id, type || '保养', '成功', nowISO());

  res.json({ id: r.lastInsertRowid, message: '添加成功' });
});

router.put('/:id', authMiddleware, roleMiddleware('admin', 'operator'), (req, res) => {
  const { type, description, handler, result, next_date, cost } = req.body;
  const db = getDb();
  const r = db.prepare(
    'UPDATE maintenance_records SET type=?, description=?, handler=?, result=?, next_date=?, cost=? WHERE id=?'
  ).run(type, description, handler, result, next_date, cost, req.params.id);
  if (r.changes === 0) return res.status(404).json({ error: '记录不存在' });
  res.json({ message: '更新成功' });
});

router.delete('/:id', authMiddleware, roleMiddleware('admin'), (req, res) => {
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
  const records = db.prepare(`
    SELECT m.*, d.name AS device_name
    FROM maintenance_records m
    LEFT JOIN devices d ON m.device_id = d.device_id
    ORDER BY m.id DESC
  `).all();

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