// 设备端操作日志 API(工人在设备上的物理操作,通过 MQTT 上报)
// 与 operation_logs(平台操作日志)区分
const express = require('express');
const { getDb } = require('../database');
const { authMiddleware, adminOnly } = require('./auth');

const router = express.Router();

// 所有接口均需登录
router.use(authMiddleware);

// 操作类型中文映射
const OP_TYPE_LABELS = {
  up_start: '开始上升', up_stop_release: '上升结束', upper_limit: '上限位触发',
  down_start: '开始下降', down_stop_release: '下降结束', lower_limit: '下限位触发',
  up: '上升',
  down: '下降',
  lock: '锁定',
  refill: '补油',
  estop: '急停',
  photo_alarm: '光电报警',
  rotary_switch: '旋转开关切换',
  power_on: '开机',
  power_off: '关机'
};

function labelOp(type) {
  return OP_TYPE_LABELS[type] || type || '';
}

// 内部/技术性事件类型(用户看不懂的,不展示在操作日志和统计中)
const INTERNAL_EVENT_TYPES = ['rise_count'];

const PRODUCT_EVENT_FILTER = `
  AND op_type NOT IN (${INTERNAL_EVENT_TYPES.map(t => `'${t}'`).join(',')})
  AND NOT (op_type = 'photo_alarm' AND device_serial IN
    (SELECT device_id FROM devices WHERE product_type = 'double_post'))
  AND NOT (op_type = 'lower_limit' AND device_serial IN
    (SELECT device_id FROM devices WHERE product_type IN ('double_post','small_scissor')))
  AND NOT (op_type = 'rotary_switch' AND device_serial IN
    (SELECT device_id FROM devices WHERE product_type <> 'large_scissor'))`;

// 构造用户可见设备过滤条件
// 设备操作日志通过 device_serial 关联 devices.device_id
// 非 admin 只能看自己有效绑定(device_bindings)的设备
function buildUserScope(db, req) {
  const isAdmin = req.user && req.user.role === 'admin';
  if (isAdmin) return { scopeSql: '', scopeParams: [] };
  const userId = req.user && req.user.id;
  const rows = db.prepare(
    "SELECT device_id FROM device_bindings WHERE user_id = ? AND status = 'active'"
  ).all(userId);
  const serials = rows.map(r => r.device_id).filter(Boolean);
  if (serials.length === 0) {
    // 用户没绑定任何设备,返回一个永假的过滤
    return { scopeSql: ' AND 0=1', scopeParams: [] };
  }
  const placeholders = serials.map(() => '?').join(',');
  return {
    scopeSql: ` AND device_serial IN (${placeholders})`,
    scopeParams: serials
  };
}

// 分页查询设备操作日志
// GET /api/device-ops?device_serial=GC-2026-00001&product_type=small_scissor&op_type=up&start_date=&end_date=&page=1&pageSize=20
router.get('/', (req, res) => {
  const db = getDb();
  const { device_serial, device_uid, product_type, op_type, start_date, end_date, page, pageSize } = req.query;
  const pageNum = Math.max(parseInt(page) || 1, 1);
  const size = Math.min(Math.max(parseInt(pageSize) || 20, 1), 500);
  const offset = (pageNum - 1) * size;

  const { scopeSql, scopeParams } = buildUserScope(db, req);

  let where = 'WHERE 1=1';
  const params = [];
  if (device_serial) {
    where += ' AND device_serial = ?';
    params.push(device_serial);
  }
  if (device_uid) {
    where += ' AND device_uid = ?';
    params.push(device_uid);
  }
  if (op_type) {
    where += ' AND op_type = ?';
    params.push(op_type);
  }
  if (start_date) {
    where += ' AND occurred_at >= ?';
    // 转换T为空格以匹配DB格式(YYYY-MM-DD HH:mm:ss)
    params.push(start_date.replace('T', ' '));
  }
  if (end_date) {
    where += ' AND occurred_at <= ?';
    // datetime-local已含时间则直接用; 仅日期补23:59:59
    const endVal = end_date.includes('T') ? end_date.replace('T', ' ') : end_date + ' 23:59:59';
    params.push(endVal);
  }
  if (product_type) {
    where += ' AND device_serial IN (SELECT device_id FROM devices WHERE product_type = ?)';
    params.push(product_type);
  }
  where += PRODUCT_EVENT_FILTER;
  where += scopeSql;
  params.push(...scopeParams);

  const total = db.prepare(`SELECT COUNT(*) AS cnt FROM device_operation_logs ${where}`).get(...params).cnt;
  const rows = db.prepare(`
    SELECT id, device_uid, device_serial, op_type, op_result, duration_ms,
           detail, device_state, role, device_tick_ms, occurred_at, received_at
    FROM device_operation_logs
    ${where}
    ORDER BY occurred_at DESC
    LIMIT ? OFFSET ?
  `).all(...params, size, offset);

  res.json({
    total, page: pageNum, pageSize: size,
    list: rows.map(r => ({
      ...r,
      op_type_label: labelOp(r.op_type)
    }))
  });
});

// 操作日志统计(按 op_type 分组 + 按天分组)
// GET /api/device-ops/stats?device_serial=&product_type=&start_date=&end_date=&group_by=type|day
router.get('/stats', (req, res) => {
  const db = getDb();
  const { device_serial, device_uid, product_type, start_date, end_date, group_by } = req.query;
  const { scopeSql, scopeParams } = buildUserScope(db, req);

  let where = 'WHERE 1=1';
  const params = [];
  if (device_serial) {
    where += ' AND device_serial = ?';
    params.push(device_serial);
  }
  if (device_uid) {
    where += ' AND device_uid = ?';
    params.push(device_uid);
  }
  if (start_date) {
    where += ' AND occurred_at >= ?';
    // 转换T为空格以匹配DB格式(YYYY-MM-DD HH:mm:ss)
    params.push(start_date.replace('T', ' '));
  }
  if (end_date) {
    where += ' AND occurred_at <= ?';
    // datetime-local已含时间则直接用; 仅日期补23:59:59
    const endVal = end_date.includes('T') ? end_date.replace('T', ' ') : end_date + ' 23:59:59';
    params.push(endVal);
  }
  if (product_type) {
    where += ' AND device_serial IN (SELECT device_id FROM devices WHERE product_type = ?)';
    params.push(product_type);
  }
  where += PRODUCT_EVENT_FILTER;
  where += scopeSql;
  params.push(...scopeParams);

  // 按 op_type 分组统计
  const byType = db.prepare(`
    SELECT op_type, role, COUNT(*) AS cnt, SUM(duration_ms) AS total_ms
    FROM device_operation_logs
    ${where}
    GROUP BY op_type, role
    ORDER BY cnt DESC
  `).all(...params);

  // 按 op_result 分组(成功/失败)
  const byResult = db.prepare(`
    SELECT op_result, COUNT(*) AS cnt
    FROM device_operation_logs
    ${where}
    GROUP BY op_result
  `).all(...params);

  let byDay = [];
  // 按天分组(最近 30 天)
  const groupBy = group_by === 'day' ? 'day' : null;
  if (groupBy === 'day' || !group_by) {
    byDay = db.prepare(`
      SELECT DATE(occurred_at) AS day, op_type, role, COUNT(*) AS cnt
      FROM device_operation_logs
      ${where}
      GROUP BY DATE(occurred_at), op_type, role
      ORDER BY day DESC
      LIMIT 500
    `).all(...params);
  }

  // 按设备分组统计
  const byDevice = db.prepare(`
    SELECT device_serial,
      SUM(CASE WHEN op_type IN ('up','up_start','up_stop_release') THEN 1 ELSE 0 END) AS up_cnt,
      SUM(CASE WHEN op_type IN ('down','down_start','down_stop_release') THEN 1 ELSE 0 END) AS down_cnt,
      SUM(CASE WHEN op_type = 'lock' THEN 1 ELSE 0 END) AS lock_cnt,
      SUM(CASE WHEN op_type = 'refill' THEN 1 ELSE 0 END) AS refill_cnt,
      SUM(CASE WHEN op_type = 'estop' THEN 1 ELSE 0 END) AS estop_cnt,
      SUM(CASE WHEN op_type = 'photo_alarm' THEN 1 ELSE 0 END) AS photo_alarm_cnt,
      SUM(CASE WHEN op_type = 'upper_limit' THEN 1 ELSE 0 END) AS upper_limit_cnt,
      SUM(CASE WHEN op_type = 'lower_limit' THEN 1 ELSE 0 END) AS lower_limit_cnt,
      SUM(CASE WHEN op_type = 'rotary_switch' THEN 1 ELSE 0 END) AS rotary_switch_cnt,
      SUM(CASE WHEN op_type = 'power_on' THEN 1 ELSE 0 END) AS power_on_cnt,
      SUM(CASE WHEN op_type = 'power_off' THEN 1 ELSE 0 END) AS power_off_cnt
    FROM device_operation_logs
    ${where}
    GROUP BY device_serial
    ORDER BY device_serial
  `).all(...params);

  // 获取设备名称
  const deviceNames = {};
  try {
    const names = db.prepare('SELECT device_id, name FROM devices').all();
    names.forEach(n => { deviceNames[n.device_id] = n.name || n.device_id; });
  } catch (e) { /* ignore */ }

  res.json({
    by_type: byType.map(r => ({
      op_type: r.op_type,
      op_type_label: labelOp(r.op_type),
      role: r.role || '',
      count: r.cnt || 0,
      total_ms: r.total_ms || 0
    })),
    by_result: byResult.map(r => ({
      op_result: r.op_result,
      count: r.cnt || 0
    })),
    by_day: byDay.map(r => ({
      day: r.day,
      op_type: r.op_type,
      op_type_label: labelOp(r.op_type),
      role: r.role || '',
      count: r.cnt || 0
    })),
    by_device: byDevice.map(r => ({
      device_serial: r.device_serial,
      device_name: deviceNames[r.device_serial] || r.device_serial,
      up: r.up_cnt || 0,
      down: r.down_cnt || 0,
      lock: r.lock_cnt || 0,
      refill: r.refill_cnt || 0,
      estop: r.estop_cnt || 0,
      photo_alarm: r.photo_alarm_cnt || 0,
      upper_limit: r.upper_limit_cnt || 0,
      lower_limit: r.lower_limit_cnt || 0,
      rotary_switch: r.rotary_switch_cnt || 0,
      power_on: r.power_on_cnt || 0,
      power_off: r.power_off_cnt || 0
    }))
  });
});

// 最近操作(用于仪表盘/设备详情页实时刷新)
// GET /api/device-ops/recent?device_serial=&limit=20
router.get('/recent', (req, res) => {
  const db = getDb();
  const { device_serial, device_uid } = req.query;
  const limit = Math.min(parseInt(req.query.limit) || 20, 100);
  const { scopeSql, scopeParams } = buildUserScope(db, req);

  let where = 'WHERE 1=1';
  const params = [];
  if (device_serial) {
    where += ' AND device_serial = ?';
    params.push(device_serial);
  }
  if (device_uid) {
    where += ' AND device_uid = ?';
    params.push(device_uid);
  }
  where += PRODUCT_EVENT_FILTER;
  where += scopeSql;
  params.push(...scopeParams);

  const rows = db.prepare(`
    SELECT id, device_uid, device_serial, op_type, op_result, duration_ms,
           detail, device_state, role, device_tick_ms, occurred_at, received_at
    FROM device_operation_logs
    ${where}
    ORDER BY occurred_at DESC
    LIMIT ?
  `).all(...params, limit);

  res.json(rows.map(r => ({
    ...r,
    op_type_label: labelOp(r.op_type)
  })));
});

module.exports = router;
