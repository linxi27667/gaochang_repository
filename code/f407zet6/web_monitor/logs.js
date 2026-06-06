const express = require('express');
const { getDb } = require('./database');
const { authMiddleware, roleMiddleware } = require('./auth');
const { nowISO } = require('./utils');

const router = express.Router();

router.get('/', authMiddleware, (req, res) => {
  const db = getDb();
  const { device_id, action, start_date, end_date, limit, offset } = req.query;

  let sql = `SELECT l.id, l.action, l.detail, l.result, l.created_at,
                    l.device_id, u.username, u.real_name
             FROM operation_logs l
             LEFT JOIN users u ON l.user_id = u.id
             WHERE 1=1`;
  const params = [];

  if (device_id) { sql += ' AND l.device_id = ?'; params.push(device_id); }
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

router.post('/', authMiddleware, (req, res) => {
  const { device_id, action, detail, result } = req.body;
  const db = getDb();
  db.prepare('INSERT INTO operation_logs (user_id, action, device_id, detail, result, created_at) VALUES (?, ?, ?, ?, ?, ?)')
    .run(req.user.id, action || '操作', device_id || '', detail || '', result || '', nowISO());
  res.json({ message: '日志已记录' });
});

module.exports = router;