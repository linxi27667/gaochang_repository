const express = require('express');
const { getDb } = require('../database');
const { authMiddleware, roleMiddleware } = require('./auth');
const { nowISO } = require('../utils');

const router = express.Router();

router.get('/', authMiddleware, (req, res) => {
  const db = getDb();
  const { device_id, type, start_date, end_date } = req.query;
  const isAdmin = req.user && req.user.role === 'admin';
  const userId = req.user && req.user.id;

  let sql = `SELECT a.*, d.name AS device_name
             FROM alarms a
             LEFT JOIN devices d ON a.device_id = d.device_id
             WHERE 1=1`;
  const params = [];

  // 非 admin 强制只看自己绑定的设备
  if (!isAdmin) {
    sql += ' AND d.owner_id = ?';
    params.push(userId);
  }
  if (device_id) { sql += ' AND a.device_id = ?'; params.push(device_id); }
  if (type) { sql += ' AND a.alarm_type LIKE ?'; params.push(`%${type}%`); }
  if (start_date) { sql += ' AND a.created_at >= ?'; params.push(start_date); }
  if (end_date) { sql += " AND a.created_at <= ?"; params.push(end_date + ' 23:59:59'); }

  sql += ' ORDER BY a.id DESC LIMIT 500';
  const alarms = db.prepare(sql).all(...params);
  res.json(alarms);
});

router.get('/unacknowledged', authMiddleware, (req, res) => {
  const db = getDb();
  const isAdmin = req.user && req.user.role === 'admin';
  const userId = req.user && req.user.id;

  let sql = `
    SELECT a.*, d.name AS device_name
    FROM alarms a
    LEFT JOIN devices d ON a.device_id = d.device_id
    WHERE a.acknowledged = 0`;
  const params = [];
  if (!isAdmin) {
    sql += ' AND d.owner_id = ?';
    params.push(userId);
  }
  sql += ' ORDER BY a.id DESC';
  const alarms = db.prepare(sql).all(...params);
  res.json(alarms);
});

router.put('/:id/acknowledge', authMiddleware, (req, res) => {
  const db = getDb();
  const result = db.prepare('UPDATE alarms SET acknowledged = 1 WHERE id = ?').run(req.params.id);
  if (result.changes === 0) return res.status(404).json({ error: '报警不存在' });

  db.prepare('INSERT INTO operation_logs (user_id, action, device_id, detail, result, created_at) VALUES (?, ?, ?, ?, ?, ?)')
    .run(req.user.id, '确认报警', '', `确认报警ID=${req.params.id}`, '成功', nowISO());

  res.json({ message: '已确认' });
});

router.put('/:id/resolve', authMiddleware, (req, res) => {
  const db = getDb();
  const result = db.prepare(
    'UPDATE alarms SET acknowledged = 1, resolved_at = ? WHERE id = ?'
  ).run(nowISO(), req.params.id);
  if (result.changes === 0) return res.status(404).json({ error: '报警不存在' });

  db.prepare('INSERT INTO operation_logs (user_id, action, device_id, detail, result, created_at) VALUES (?, ?, ?, ?, ?, ?)')
    .run(req.user.id, '解除报警', '', `解除报警ID=${req.params.id}`, '成功', nowISO());

  res.json({ message: '已解除' });
});

module.exports = router;