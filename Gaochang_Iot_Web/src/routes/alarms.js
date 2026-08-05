const express = require('express');
const { getDb } = require('../database');
const { authMiddleware, roleMiddleware } = require('./auth');
const { nowISO } = require('../utils');

const router = express.Router();
const HIDDEN_COLLISION_ALARMS = ['collision', 'collision_up', 'collision_down'];
const HIDDEN_WEB_ALARM_SQL = ` AND a.alarm_type NOT IN (${HIDDEN_COLLISION_ALARMS.map(() => '?').join(',')})
  AND NOT (a.alarm_type = 'stall' AND COALESCE(d.product_type, '') <> 'small_scissor')`;

function buildAlarmFilters(req) {
  const { device_id, type, start_date, end_date, status, level, q } = req.query;
  const clauses = ['1=1'];
  const params = [];

  clauses.push(HIDDEN_WEB_ALARM_SQL.replace(/^\s*AND\s+/, ''));
  params.push(...HIDDEN_COLLISION_ALARMS);

  const isAdmin = req.user && req.user.role === 'admin';
  const userId = req.user && req.user.id;
  if (!isAdmin) {
    clauses.push(`EXISTS (
      SELECT 1 FROM device_bindings b
      WHERE b.device_id = d.device_id AND b.user_id = ? AND b.status = 'active'
    )`);
    params.push(userId);
  }

  if (device_id) { clauses.push('a.device_id = ?'); params.push(String(device_id).slice(0, 128)); }
  if (type) { clauses.push('a.alarm_type LIKE ?'); params.push(`%${String(type).slice(0, 64)}%`); }
  if (level && ['warning', 'danger'].includes(level)) { clauses.push('a.level = ?'); params.push(level); }
  if (status === 'unack') clauses.push('a.acknowledged = 0');
  if (status === 'ack') clauses.push('a.acknowledged = 1');
  if (status === 'active') clauses.push('a.resolved_at IS NULL');
  if (status === 'resolved') clauses.push('a.resolved_at IS NOT NULL');
  if (start_date) { clauses.push('a.created_at >= ?'); params.push(String(start_date).slice(0, 32)); }
  if (end_date) { clauses.push('a.created_at <= ?'); params.push(String(end_date).slice(0, 32) + ' 23:59:59'); }
  if (q) {
    const keyword = `%${String(q).trim().slice(0, 80)}%`;
    clauses.push(`(
      CAST(a.id AS TEXT) LIKE ? OR a.device_id LIKE ? OR COALESCE(d.name, '') LIKE ?
      OR a.alarm_type LIKE ? OR COALESCE(a.message, '') LIKE ?
    )`);
    params.push(keyword, keyword, keyword, keyword, keyword);
  }

  return { where: clauses.join(' AND '), params };
}

router.get('/', authMiddleware, (req, res) => {
  const db = getDb();
  const { where, params } = buildAlarmFilters(req);
  const rawLimit = Number.parseInt(req.query.limit, 10);
  const limit = Number.isFinite(rawLimit) ? Math.max(1, Math.min(rawLimit, 1000)) : 500;

  let sql = `SELECT a.*, d.name AS device_name, d.product_type
             FROM alarms a
             LEFT JOIN devices d ON a.device_id = d.device_id
             WHERE ${where}
             ORDER BY a.id DESC LIMIT ${limit}`;
  const alarms = db.prepare(sql).all(...params);
  res.json(alarms);
});

router.get('/summary', authMiddleware, (req, res) => {
  const db = getDb();
  const { where, params } = buildAlarmFilters(req);
  const summary = db.prepare(`
    SELECT
      COUNT(*) AS total,
      COALESCE(SUM(CASE WHEN a.acknowledged = 0 THEN 1 ELSE 0 END), 0) AS unacknowledged,
      COALESCE(SUM(CASE WHEN a.resolved_at IS NULL THEN 1 ELSE 0 END), 0) AS active,
      COALESCE(SUM(CASE WHEN a.level = 'danger' AND a.resolved_at IS NULL THEN 1 ELSE 0 END), 0) AS critical,
      MAX(a.created_at) AS latest_at
    FROM alarms a
    LEFT JOIN devices d ON a.device_id = d.device_id
    WHERE ${where}
  `).get(...params);
  res.json(summary);
});

router.get('/unacknowledged', authMiddleware, (req, res) => {
  const db = getDb();
  const isAdmin = req.user && req.user.role === 'admin';
  const userId = req.user && req.user.id;

  let sql = `
    SELECT a.*, d.name AS device_name, d.product_type
    FROM alarms a
    LEFT JOIN devices d ON a.device_id = d.device_id
    WHERE a.acknowledged = 0`;
  const params = [];
  sql += HIDDEN_WEB_ALARM_SQL;
  params.push(...HIDDEN_COLLISION_ALARMS);
  if (!isAdmin) {
    sql += ` AND EXISTS (
      SELECT 1 FROM device_bindings b
      WHERE b.device_id = d.device_id AND b.user_id = ? AND b.status = 'active'
    )`;
    params.push(userId);
  }
  sql += ' ORDER BY a.id DESC LIMIT 500';
  const alarms = db.prepare(sql).all(...params);
  res.json(alarms);
});

// 公共:校验报警归属设备是否被当前用户绑定(admin 直接放行)
function checkAlarmAccess(db, req, alarmId) {
  const isAdmin = req.user && req.user.role === 'admin';
  if (isAdmin) return true;
  const userId = req.user && req.user.id;
  const row = db.prepare(`
    SELECT 1 FROM alarms a
    JOIN device_bindings b ON b.device_id = a.device_id AND b.user_id = ? AND b.status = 'active'
    WHERE a.id = ?
  `).get(userId, alarmId);
  return !!row;
}

router.put('/:id/acknowledge', authMiddleware, (req, res) => {
  const db = getDb();
  const alarmId = parseInt(req.params.id);
  if (!checkAlarmAccess(db, req, alarmId)) {
    return res.status(403).json({ error: '无权操作该报警' });
  }
  // 先查 alarm 的 device_id,用于操作日志关联
  const alarm = db.prepare('SELECT device_id FROM alarms WHERE id = ?').get(alarmId);
  if (!alarm) return res.status(404).json({ error: '报警不存在' });
  const result = db.prepare('UPDATE alarms SET acknowledged = 1 WHERE id = ?').run(alarmId);

  db.prepare('INSERT INTO operation_logs (user_id, action, device_id, detail, result, created_at) VALUES (?, ?, ?, ?, ?, ?)')
    .run(req.user.id, '确认报警', alarm.device_id, `确认报警ID=${alarmId}`, '成功', nowISO());

  res.json({ message: '已确认' });
});

router.put('/:id/resolve', authMiddleware, (req, res) => {
  const db = getDb();
  const alarmId = parseInt(req.params.id);
  if (!checkAlarmAccess(db, req, alarmId)) {
    return res.status(403).json({ error: '无权操作该报警' });
  }
  // 先查 alarm 的 device_id,用于操作日志关联
  const alarm = db.prepare('SELECT device_id FROM alarms WHERE id = ?').get(alarmId);
  if (!alarm) return res.status(404).json({ error: '报警不存在' });
  const result = db.prepare(
    'UPDATE alarms SET acknowledged = 1, resolved_at = ? WHERE id = ?'
  ).run(nowISO(), alarmId);

  db.prepare('INSERT INTO operation_logs (user_id, action, device_id, detail, result, created_at) VALUES (?, ?, ?, ?, ?, ?)')
    .run(req.user.id, '解除报警', alarm.device_id, `解除报警ID=${alarmId}`, '成功', nowISO());

  res.json({ message: '已解除' });
});

module.exports = router;
