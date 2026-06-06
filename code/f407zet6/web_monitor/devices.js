const express = require('express');
const { getDb } = require('./database');
const { authMiddleware, roleMiddleware } = require('./auth');

const router = express.Router();

router.get('/', authMiddleware, (req, res) => {
  const db = getDb();

  const devices = db.prepare(`
    SELECT d.device_id, d.name, d.model, d.group_name, d.location, d.created_at,
           s.online, s.locked, s.state, s.alarm, s.height_left_mm,
           s.height_right_mm, s.height_diff_mm, s.run_count, s.run_time_s,
           s.ts_ms, s.updated_at
    FROM devices d
    LEFT JOIN device_status s ON d.device_id = s.device_id
    ORDER BY d.device_id
  `).all();

  res.json(devices.map(d => ({
    device_id: d.device_id,
    name: d.name,
    model: d.model,
    group: d.group_name,
    location: d.location,
    created_at: d.created_at,
    online: !!d.online,
    locked: !!d.locked,
    state: d.state || 'idle',
    alarm: d.alarm || 'none',
    height_left_mm: d.height_left_mm || 0,
    height_right_mm: d.height_right_mm || 0,
    height_diff_mm: d.height_diff_mm || 0,
    run_count: d.run_count || 0,
    run_time_s: d.run_time_s || 0,
    ts_ms: d.ts_ms || 0,
    updated_at: d.updated_at
  })));
});

router.get('/:id', authMiddleware, (req, res) => {
  const db = getDb();
  const d = db.prepare(`
    SELECT d.device_id, d.name, d.model, d.group_name, d.location, d.created_at,
           s.online, s.locked, s.state, s.alarm, s.height_left_mm,
           s.height_right_mm, s.height_diff_mm, s.run_count, s.run_time_s,
           s.ts_ms, s.updated_at
    FROM devices d
    LEFT JOIN device_status s ON d.device_id = s.device_id
    WHERE d.device_id = ?
  `).get(req.params.id);

  if (!d) return res.status(404).json({ error: '设备不存在' });

  res.json({
    device_id: d.device_id,
    name: d.name,
    model: d.model,
    group: d.group_name,
    location: d.location,
    created_at: d.created_at,
    online: !!d.online,
    locked: !!d.locked,
    state: d.state || 'idle',
    alarm: d.alarm || 'none',
    height_left_mm: d.height_left_mm || 0,
    height_right_mm: d.height_right_mm || 0,
    height_diff_mm: d.height_diff_mm || 0,
    run_count: d.run_count || 0,
    run_time_s: d.run_time_s || 0,
    ts_ms: d.ts_ms || 0,
    updated_at: d.updated_at
  });
});

router.post('/', authMiddleware, roleMiddleware('admin'), (req, res) => {
  const { device_id, name, model, group_name, location } = req.body;
  if (!device_id || !name) {
    return res.status(400).json({ error: '设备ID和名称不能为空' });
  }
  const db = getDb();
  try {
    db.prepare('INSERT INTO devices (device_id, name, model, group_name, location) VALUES (?, ?, ?, ?, ?)')
      .run(device_id, name, model || 'TL-5000', group_name || '默认分组', location || '');
    db.prepare('INSERT INTO device_status (device_id) VALUES (?)').run(device_id);
    res.json({ message: '设备添加成功', device_id });
  } catch (e) {
    if (e.message.includes('UNIQUE')) {
      return res.status(409).json({ error: '设备ID已存在' });
    }
    res.status(500).json({ error: '添加失败' });
  }
});

router.put('/:id', authMiddleware, roleMiddleware('admin'), (req, res) => {
  const { name, model, group_name, location } = req.body;
  const db = getDb();
  const result = db.prepare('UPDATE devices SET name = ?, model = ?, group_name = ?, location = ? WHERE device_id = ?')
    .run(name, model, group_name, location, req.params.id);
  if (result.changes === 0) return res.status(404).json({ error: '设备不存在' });
  res.json({ message: '更新成功' });
});

router.delete('/:id', authMiddleware, roleMiddleware('admin'), (req, res) => {
  const db = getDb();
  db.prepare('DELETE FROM devices WHERE device_id = ?').run(req.params.id);
  res.json({ message: '删除成功' });
});

router.get('/overview/summary', authMiddleware, (req, res) => {
  const db = getDb();
  const stats = db.prepare(`
    SELECT
      COUNT(*) AS total,
      SUM(CASE WHEN s.online = 1 AND s.locked = 0 AND s.alarm = 'none' THEN 1 ELSE 0 END) AS online_count,
      SUM(CASE WHEN s.online = 0 OR s.online IS NULL THEN 1 ELSE 0 END) AS offline_count,
      SUM(CASE WHEN s.alarm != 'none' AND s.alarm IS NOT NULL THEN 1 ELSE 0 END) AS fault_count,
      SUM(CASE WHEN s.locked = 1 THEN 1 ELSE 0 END) AS locked_count,
      SUM(s.run_count) AS total_run_count,
      SUM(s.run_time_s) AS total_run_time_s
    FROM devices d
    LEFT JOIN device_status s ON d.device_id = s.device_id
  `).get();

  res.json({
    total: stats.total || 0,
    online: stats.online_count || 0,
    offline: stats.offline_count || 0,
    fault: stats.fault_count || 0,
    locked: stats.locked_count || 0,
    total_run_count: stats.total_run_count || 0,
    total_run_time_s: stats.total_run_time_s || 0
  });
});

module.exports = router;