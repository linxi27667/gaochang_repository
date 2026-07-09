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

// 构造用户可见设备过滤条件
// 设备操作日志通过 device_serial 关联 devices.device_id
// 非 admin 只能看自己绑定的设备(device_serial IN (该用户绑定的 device_id 列表))
function buildUserScope(db, req) {
  const isAdmin = req.user && req.user.role === 'admin';
  if (isAdmin) return { scopeSql: '', scopeParams: [] };
  const userId = req.user && req.user.id;
  const rows = db.prepare('SELECT device_id FROM devices WHERE owner_id = ?').all(userId);
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
// GET /api/device-ops?device_serial=GC-2026-00001&op_type=up&start_date=&end_date=&page=1&pageSize=20
router.get('/', (req, res) => {
  const db = getDb();
  const { device_serial, device_uid, op_type, start_date, end_date, page, pageSize } = req.query;
  const pageNum = Math.max(parseInt(page) || 1, 1);
  const size = Math.min(Math.max(parseInt(pageSize) || 20, 1), 500);
  const offset = (pageNum - 1) * size;

  const { scopeSql, scopeParams } = buildUserScope(db, req);

  let where = 'WHERE 1=1';
  const params = [...scopeParams];
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
    params.push(start_date);
  }
  if (end_date) {
    where += ' AND occurred_at <= ?';
    params.push(end_date + ' 23:59:59');
  }
  where += scopeSql;

  const total = db.prepare(`SELECT COUNT(*) AS cnt FROM device_operation_logs ${where}`).get(...params).cnt;
  const rows = db.prepare(`
    SELECT id, device_uid, device_serial, op_type, op_result, duration_ms,
           detail, device_state, occurred_at, received_at
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
// GET /api/device-ops/stats?device_serial=&start_date=&end_date=&group_by=type|day
router.get('/stats', (req, res) => {
  const db = getDb();
  const { device_serial, device_uid, start_date, end_date, group_by } = req.query;
  const { scopeSql, scopeParams } = buildUserScope(db, req);

  let where = 'WHERE 1=1';
  const params = [...scopeParams];
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
    params.push(start_date);
  }
  if (end_date) {
    where += ' AND occurred_at <= ?';
    params.push(end_date + ' 23:59:59');
  }
  where += scopeSql;

  // 按 op_type 分组统计
  const byType = db.prepare(`
    SELECT op_type, COUNT(*) AS cnt, SUM(duration_ms) AS total_ms
    FROM device_operation_logs
    ${where}
    GROUP BY op_type
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
      SELECT DATE(occurred_at) AS day, op_type, COUNT(*) AS cnt
      FROM device_operation_logs
      ${where}
      GROUP BY DATE(occurred_at), op_type
      ORDER BY day DESC
      LIMIT 500
    `).all(...params);
  }

  res.json({
    by_type: byType.map(r => ({
      op_type: r.op_type,
      op_type_label: labelOp(r.op_type),
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
      count: r.cnt || 0
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
  const params = [...scopeParams];
  if (device_serial) {
    where += ' AND device_serial = ?';
    params.push(device_serial);
  }
  if (device_uid) {
    where += ' AND device_uid = ?';
    params.push(device_uid);
  }
  where += scopeSql;

  const rows = db.prepare(`
    SELECT id, device_uid, device_serial, op_type, op_result, duration_ms,
           detail, device_state, occurred_at, received_at
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
