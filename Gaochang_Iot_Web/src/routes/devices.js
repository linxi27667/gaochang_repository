const express = require('express');
const { getDb, PRODUCT_CONFIGS } = require('../database');
const { nowISO } = require('../utils');
const { authMiddleware, adminOnly } = require('./auth');

const router = express.Router();

// 产品型号映射,供前端展示
const PRODUCT_TYPE_MAP = PRODUCT_CONFIGS.reduce((m, p) => {
  m[p.product_type] = p.display_name;
  return m;
}, {});
const VALID_PRODUCT_TYPES = PRODUCT_CONFIGS.map(p => p.product_type);

// 设备详情字段投影(列表/详情共用)
function projectDevice(d) {
  return {
    device_id: d.device_id,
    name: d.name,
    model: d.model,
    gateway_id: d.gateway_id || '',
    group: d.group_name,
    location: d.location,
    uid: d.uid || '',
    owner_id: d.owner_id,
    product_type: d.product_type || 'double_post',
    product_type_name: PRODUCT_TYPE_MAP[d.product_type] || d.product_type || '两柱举升机',
    lift_role: d.lift_role || 'main',
    bind_status: d.bind_status || 'unbound',
    bound_at: d.bound_at || '',
    created_at: d.created_at,
    has_encoder: !!d.has_encoder,
    has_buzzer: !!d.has_buzzer,
    has_pressure_sensor: !!d.has_pressure_sensor,
    has_display: !!d.has_display,
    online: !!d.online,
    locked: !!d.locked,
    state: d.state || 'idle',
    alarm: d.alarm || 'none',
    rotary_switch: d.rotary_switch || 'main',
    height_left_mm: d.height_left_mm || 0,
    height_right_mm: d.height_right_mm || 0,
    height_diff_mm: d.height_diff_mm || 0,
    run_count: d.run_count || 0,
    run_time_s: d.run_time_s || 0,
    uptime_s: d.uptime_s || 0,
    ts_ms: d.ts_ms || 0,
    updated_at: d.updated_at,
    direction: d.direction || 'stop',
    upper_limit: d.upper_limit || 0,
    lower_limit: d.lower_limit || 0,
    stall: d.stall || 0,
    collision_up: d.collision_up || 0,
    collision_down: d.collision_down || 0,
    left_up_collision: d.left_up_collision || 0,
    right_up_collision: d.right_up_collision || 0,
    left_down_collision: d.left_down_collision || 0,
    right_down_collision: d.right_down_collision || 0,
    alarm_code: d.alarm_code || 0,
    csq: d.csq ?? -1,
    dtu_state: d.dtu_state || '',
    left_pulse: d.left_pulse || 0,
    right_pulse: d.right_pulse || 0,
    // 多产品操作计数
    up_count: d.up_count || 0,
    down_count: d.down_count || 0,
    lock_count: d.lock_count || 0,
    refill_count: d.refill_count || 0,
    estop_count: d.estop_count || 0,
    photo_alarm_count: d.photo_alarm_count || 0,
    total_run_ms: d.total_run_ms || 0,
    last_run_at: d.last_run_at || '',
    io_input_json: d.io_input_json || '{}',
    io_output_json: d.io_output_json || '{}',
    buzzer_on: d.buzzer_on || 0,
    total_lift_count: d.total_lift_count || 0,
    maintenance_lift_count: d.maintenance_lift_count || 0,
    maintenance_threshold: d.maintenance_threshold || 5000,
    maintenance_count: d.maintenance_count || 0,
    last_maintenance_total: d.last_maintenance_total || 0,
    maintenance_due: d.maintenance_due ? 1 : 0,
    usage_epoch: d.usage_epoch || 0,
    maintenance_revision: d.maintenance_revision || 0
  };
}

// 公共 SELECT 字段列表(避免重复)
const DEVICE_SELECT_FIELDS = `
  d.device_id, d.name, d.model, d.gateway_id, d.group_name, d.location, d.created_at,
  d.uid, d.owner_id, d.product_type, d.lift_role, d.bind_status, d.bound_at,
  d.has_encoder, d.has_buzzer, d.has_pressure_sensor, d.has_display,
  s.online, s.locked, s.state, s.alarm, s.rotary_switch, s.height_left_mm,
  s.height_right_mm, s.height_diff_mm, s.run_count, s.run_time_s, s.uptime_s,
  s.ts_ms, s.updated_at,
  s.direction, s.upper_limit, s.lower_limit, s.stall, s.collision_up,
  s.collision_down, s.left_up_collision, s.right_up_collision,
  s.left_down_collision, s.right_down_collision, s.alarm_code, s.csq, s.dtu_state,
  s.left_pulse, s.right_pulse, s.buzzer_on,
  s.up_count, s.down_count, s.lock_count, s.refill_count,
  s.estop_count, s.photo_alarm_count,
  s.total_run_ms, s.last_run_at,
  s.io_input_json, s.io_output_json,
  s.total_lift_count, s.maintenance_lift_count, s.maintenance_threshold,
  s.maintenance_count, s.last_maintenance_total, s.maintenance_due, s.usage_epoch, s.maintenance_revision
`;

// 设备列表(支持 product_type / online / keyword 过滤)
// GET /api/devices?product_type=large_scissor&online=1&keyword=GC-2026
// 权限: admin 看全部; user 只看自己有效绑定的设备
router.get('/', authMiddleware, (req, res) => {
  const db = getDb();
  const { product_type, online, keyword } = req.query;
  const isAdmin = req.user && req.user.role === 'admin';
  const userId = req.user && req.user.id;

  let sql = `SELECT ${DEVICE_SELECT_FIELDS}
    FROM devices d
    LEFT JOIN device_status s ON d.device_id = s.device_id
    WHERE 1=1`;
  const params = [];

  // 非 admin 强制只查自己有效绑定的设备(通过 device_bindings 多对多鉴权)
  if (!isAdmin) {
    sql += ` AND EXISTS (
      SELECT 1 FROM device_bindings b
      WHERE b.device_id = d.device_id AND b.user_id = ? AND b.status = 'active'
    )`;
    params.push(userId);
  }

  if (product_type) {
    sql += ' AND d.product_type = ?';
    params.push(product_type);
  }
  if (online === '1' || online === 'true') {
    sql += ' AND s.online = 1';
  } else if (online === '0' || online === 'false') {
    sql += ' AND (s.online = 0 OR s.online IS NULL)';
  }
  if (keyword) {
    sql += ' AND (d.device_id LIKE ? OR d.name LIKE ? OR d.uid LIKE ?)';
    const kw = `%${keyword}%`;
    params.push(kw, kw, kw);
  }

  sql += ' ORDER BY d.device_id';
  const devices = db.prepare(sql).all(...params);
  res.json(devices.map(projectDevice));
});

// 设备详情
// 权限: admin 可查任意; user 只能查自己有效绑定的
router.get('/:id', authMiddleware, (req, res) => {
  const db = getDb();
  const isAdmin = req.user && req.user.role === 'admin';
  const userId = req.user && req.user.id;

  let sql = `SELECT ${DEVICE_SELECT_FIELDS}
    FROM devices d
    LEFT JOIN device_status s ON d.device_id = s.device_id
    WHERE d.device_id = ?`;
  const params = [req.params.id];
  if (!isAdmin) {
    sql += ` AND EXISTS (
      SELECT 1 FROM device_bindings b
      WHERE b.device_id = d.device_id AND b.user_id = ? AND b.status = 'active'
    )`;
    params.push(userId);
  }
  const d = db.prepare(sql).get(...params);

  if (!d) return res.status(404).json({ error: '设备不存在' });
  res.json(projectDevice(d));
});

// 新建设备(仅 admin)
router.post('/', authMiddleware, adminOnly, (req, res) => {
  const {
    device_id, name, model, group_name, location,
    product_type, lift_role, uid,
    has_encoder, has_buzzer, has_pressure_sensor, has_display
  } = req.body;
  if (!device_id || !name) {
    return res.status(400).json({ error: '设备ID和名称不能为空' });
  }
  if (product_type && !VALID_PRODUCT_TYPES.includes(product_type)) {
    return res.status(400).json({ error: '产品型号无效' });
  }
  const db = getDb();
  try {
    // 事务:devices + device_status + operation_logs 三表同时写入,保证一致性
    const tx = db.transaction(() => {
      db.prepare(`
        INSERT INTO devices (device_id, name, model, group_name, location,
          product_type, lift_role, uid,
          has_encoder, has_buzzer, has_pressure_sensor, has_display, created_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
      `).run(
        device_id, name, model || '', group_name || '默认分组', location || '',
        product_type || 'double_post', lift_role || 'main', uid || '',
        has_encoder ? 1 : 0, has_buzzer ? 1 : 0, has_pressure_sensor ? 1 : 0, has_display ? 1 : 0,
        nowISO()
      );
      db.prepare('INSERT INTO device_status (device_id, updated_at) VALUES (?, ?)').run(device_id, nowISO());

      db.prepare('INSERT INTO operation_logs (user_id, action, device_id, detail, result, created_at) VALUES (?, ?, ?, ?, ?, ?)')
        .run(req.user.id, '添加设备', device_id, `名称:${name}, 型号:${product_type || 'double_post'}`, '成功', nowISO());
    });
    tx();

    res.json({ message: '设备添加成功', device_id });
  } catch (e) {
    if (e.message.includes('UNIQUE')) {
      return res.status(409).json({ error: '设备ID已存在' });
    }
    res.status(500).json({ error: '添加失败' });
  }
});

// 更新设备(仅 admin)
router.put('/:id', authMiddleware, adminOnly, (req, res) => {
  const {
    name, model, group_name, location,
    lift_role,
    has_encoder, has_buzzer, has_pressure_sensor, has_display
  } = req.body;
  const db = getDb();
  const result = db.prepare(`
    UPDATE devices SET
      name = ?, model = ?, group_name = ?, location = ?,
      lift_role = ?,
      has_encoder = ?, has_buzzer = ?, has_pressure_sensor = ?, has_display = ?
    WHERE device_id = ?
  `).run(
    name, model, group_name, location,
    lift_role || 'main',
    has_encoder ? 1 : 0, has_buzzer ? 1 : 0, has_pressure_sensor ? 1 : 0, has_display ? 1 : 0,
    req.params.id
  );
  if (result.changes === 0) return res.status(404).json({ error: '设备不存在' });
  res.json({ message: '更新成功' });
});

// 删除设备(仅 admin)
router.delete('/:id', authMiddleware, adminOnly, (req, res) => {
  const deviceId = req.params.id;
  const db = getDb();

  const device = db.prepare('SELECT device_id, name, uid FROM devices WHERE device_id = ?').get(deviceId);
  if (!device) return res.status(404).json({ error: '设备不存在' });

  const deleteAll = db.transaction(() => {
    db.prepare('DELETE FROM device_status WHERE device_id = ?').run(deviceId);
    db.prepare('DELETE FROM alarms WHERE device_id = ?').run(deviceId);
    db.prepare('DELETE FROM maintenance_records WHERE device_id = ?').run(deviceId);
    db.prepare('DELETE FROM operation_logs WHERE device_id = ?').run(deviceId);
    db.prepare('DELETE FROM command_queue WHERE device_id = ?').run(deviceId);
    db.prepare('DELETE FROM device_operation_logs WHERE device_serial = ?').run(deviceId);
    db.prepare('DELETE FROM devices WHERE device_id = ?').run(deviceId);
    // 解除注册表绑定
    if (device.uid) {
      db.prepare("UPDATE device_registry SET status = 'unbound', bound_device_id = '' WHERE uid = ?").run(device.uid);
    }
  });

  try {
    deleteAll();
    db.prepare('INSERT INTO operation_logs (user_id, action, device_id, detail, result, created_at) VALUES (?, ?, ?, ?, ?, ?)')
      .run(req.user.id, '删除设备', deviceId, `删除设备 ${device.name}`, '成功', nowISO());
    res.json({ message: '设备已删除' });
  } catch (e) {
    res.status(500).json({ error: '删除失败' });
  }
});

// 设备总览统计(支持按 product_type 分组)
router.get('/overview/summary', authMiddleware, (req, res) => {
  const db = getDb();
  const isAdmin = req.user && req.user.role === 'admin';
  const userId = req.user && req.user.id;
  // 非 admin 强制只统计自己有效绑定的设备
  const scopeWhere = isAdmin ? '' : ` AND EXISTS (
    SELECT 1 FROM device_bindings b
    WHERE b.device_id = d.device_id AND b.user_id = ? AND b.status = 'active'
  )`;
  const scopeParams = isAdmin ? [] : [userId];

  const stats = db.prepare(`
    SELECT
      COUNT(*) AS total,
      SUM(CASE WHEN s.online = 1 AND s.locked = 0 AND s.alarm = 'none' THEN 1 ELSE 0 END) AS online_count,
      SUM(CASE WHEN s.online = 0 OR s.online IS NULL THEN 1 ELSE 0 END) AS offline_count,
      SUM(CASE WHEN s.alarm != 'none' AND s.alarm IS NOT NULL THEN 1 ELSE 0 END) AS fault_count,
      SUM(CASE WHEN s.locked = 1 THEN 1 ELSE 0 END) AS locked_count,
      SUM(CASE WHEN s.online = 1 THEN 1 ELSE 0 END) AS online_total,
      SUM(s.run_count) AS total_run_count,
      SUM(s.up_count) AS total_up_count,
      SUM(s.down_count) AS total_down_count,
      SUM(s.total_run_ms) AS total_run_ms
    FROM devices d
    LEFT JOIN device_status s ON d.device_id = s.device_id
    WHERE 1=1 ${scopeWhere}
  `).get(...scopeParams);

  // 按产品型号分组统计(同样按用户隔离)
  const byProduct = db.prepare(`
    SELECT d.product_type,
      COUNT(*) AS total,
      SUM(CASE WHEN s.online = 1 THEN 1 ELSE 0 END) AS online_count,
      SUM(CASE WHEN s.alarm != 'none' AND s.alarm IS NOT NULL THEN 1 ELSE 0 END) AS fault_count
    FROM devices d
    LEFT JOIN device_status s ON d.device_id = s.device_id
    WHERE 1=1 ${scopeWhere}
    GROUP BY d.product_type
  `).all(...scopeParams);

  res.json({
    total: stats.total || 0,
    online: stats.online_count || 0,
    offline: stats.offline_count || 0,
    fault: stats.fault_count || 0,
    locked: stats.locked_count || 0,
    total_run_count: stats.total_run_count || 0,
    total_up_count: stats.total_up_count || 0,
    total_down_count: stats.total_down_count || 0,
    total_run_ms: stats.total_run_ms || 0,
    by_product: byProduct.map(r => ({
      product_type: r.product_type || 'double_post',
      product_type_name: PRODUCT_TYPE_MAP[r.product_type] || r.product_type || '两柱举升机',
      total: r.total || 0,
      online: r.online_count || 0,
      fault: r.fault_count || 0
    }))
  });
});

// 产品型号元数据(供前端动态渲染)
router.get('/meta/products', authMiddleware, (req, res) => {
  res.json(PRODUCT_CONFIGS);
});

module.exports = router;
