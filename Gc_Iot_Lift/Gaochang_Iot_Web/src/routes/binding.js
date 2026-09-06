// 客户设备绑定 API(基于 device_bindings 多对多绑定表)
// 提供 serial+bind_code 绑定、解绑、我的设备列表、绑定状态查询
const express = require('express');
const crypto = require('crypto');
const { getDb } = require('../database');
const { nowISO } = require('../utils');
const { authMiddleware } = require('./auth');
const { rateLimit } = require('../rateLimit');

const router = express.Router();

const BIND_CODE_SALT = process.env.BIND_CODE_SALT || 'gaochang_lift_default_salt_2026';
const AUTO_APPROVE_LIMIT = parseInt(process.env.BINDING_AUTO_APPROVE_LIMIT || '3', 10);

const DEFAULT_DEVICE_NAME_PREFIX = {
  screw_lift: '丝杆',
  double_post: '两柱',
  large_scissor: '大剪',
  small_scissor: '小剪',
  thin_scissor: '超薄小剪'
};

function nextDefaultDeviceName(db, productType) {
  const prefix = DEFAULT_DEVICE_NAME_PREFIX[productType] || '举升机';
  const rows = db.prepare('SELECT name FROM devices WHERE product_type = ?').all(productType);
  let next = 0;

  for (const row of rows) {
    const name = String(row.name || '');
    if (!name.startsWith(prefix)) continue;
    const suffix = name.slice(prefix.length);
    if (/^\d+$/.test(suffix)) {
      next = Math.max(next, Number(suffix) + 1);
    }
  }

  return `${prefix}${String(next).padStart(2, '0')}`;
}

// 获取客户端 IP(兼容反向代理)
function getClientIp(req) {
  const xff = req.headers['x-forwarded-for'];
  if (xff) return String(xff).split(',')[0].trim();
  return req.ip || (req.connection && req.connection.remoteAddress) || '';
}

// 规范化 UID:统一转小写,去除 0x 前缀和空白/冒号/连字符
function normalizeUid(raw) {
  if (!raw) return '';
  let s = String(raw).trim().toLowerCase();
  if (s.startsWith('0x')) s = s.slice(2);
  s = s.replace(/[\s:\-]/g, '');
  return s;
}

// 绑定码哈希:SHA-256(salt + bind_code)
function hashBindCode(bindCode) {
  return crypto.createHash('sha256').update(BIND_CODE_SALT + bindCode).digest('hex');
}

// 校验绑定码:对比哈希
function verifyBindCode(bindCode, storedHash) {
  if (!bindCode || !storedHash) return false;
  return hashBindCode(bindCode) === storedHash;
}

// 所有接口均需登录
router.use(authMiddleware);

// 查询某 SN 的设备信息(绑定前预览,不返回 uid 和 bind_code)
// GET /api/binding/lookup?serial=GC-2026-00001
router.get('/lookup', rateLimit({ windowMs: 60000, max: 30 }), (req, res) => {
  const serial = (req.query.serial || '').trim();
  if (!serial) {
    return res.status(400).json({ error: '缺少设备编号 serial' });
  }

  const db = getDb();
  const row = db.prepare(`
    SELECT r.serial, r.model, r.product_type, r.display_name,
           r.has_encoder, r.has_buzzer, r.has_pressure_sensor, r.has_display,
           r.isolation_status
    FROM device_registry r
    WHERE r.serial = ?
  `).get(serial);

  if (!row) {
    return res.status(404).json({ error: '设备编号不存在' });
  }

  // 检查隔离状态
  if (row.isolation_status && row.isolation_status !== 'ok') {
    return res.status(403).json({ error: '设备已隔离,请联系管理员' });
  }

  // 查询当前绑定数量
  const bindCount = db.prepare(
    "SELECT COUNT(*) as cnt FROM device_bindings WHERE device_id = ? AND status = 'active'"
  ).get(serial).cnt;

  // 检查当前用户是否已绑定
  const myBinding = db.prepare(
    "SELECT status FROM device_bindings WHERE device_id = ? AND user_id = ?"
  ).get(serial, req.user.id);

  res.json({
    serial: row.serial,
    model: row.model || '',
    product_type: row.product_type || 'double_post',
    display_name: row.display_name || '',
    has_encoder: !!row.has_encoder,
    has_buzzer: !!row.has_buzzer,
    has_pressure_sensor: !!row.has_pressure_sensor,
    has_display: !!row.has_display,
    current_bindings: bindCount,
    auto_approve_limit: AUTO_APPROVE_LIMIT,
    my_binding_status: myBinding ? myBinding.status : null  // active / pending / revoked
  });
});

// 客户绑定设备(通过 serial + bind_code)
// POST /api/binding/bind  Body: { serial, bind_code }
// 返回:201 active(直接绑定成功) 或 202 pending(待审批)
router.post('/bind', rateLimit({ windowMs: 60000, max: 3 }), (req, res) => {
  const serial = (req.body && req.body.serial || '').trim();
  const bindCode = (req.body && req.body.bind_code || '').trim();

  if (!serial || !bindCode) {
    return res.status(400).json({ error: '请提供设备编号 serial 和绑定码 bind_code' });
  }

  const db = getDb();
  const userId = req.user.id;
  const now = nowISO();
  const ip = getClientIp(req);

  // 1. 查询出厂注册表
  const registry = db.prepare(`
    SELECT id, serial, uid, model, product_type, display_name, bind_code_hash,
           has_encoder, has_buzzer, has_pressure_sensor, has_display, isolation_status
    FROM device_registry WHERE serial = ?
  `).get(serial);

  if (!registry) {
    return res.status(404).json({ error: '设备编号不存在' });
  }

  // 2. 隔离状态检查
  if (registry.isolation_status && registry.isolation_status !== 'ok') {
    return res.status(403).json({ error: '设备已隔离,请联系管理员' });
  }

  // 3. 绑定码校验
  if (!registry.bind_code_hash) {
    return res.status(400).json({ error: '该设备未设置绑定码,请联系管理员' });
  }
  if (!verifyBindCode(bindCode, registry.bind_code_hash)) {
    return res.status(401).json({ error: '绑定码错误' });
  }

  // 4. 检查是否已绑定
  const existingBinding = db.prepare(
    "SELECT id, status FROM device_bindings WHERE device_id = ? AND user_id = ?"
  ).get(serial, userId);

  if (existingBinding) {
    if (existingBinding.status === 'active') {
      return res.status(409).json({ error: '该设备已绑定到您的账号', status: 'active' });
    }
    if (existingBinding.status === 'pending') {
      return res.status(409).json({ error: '您已申请绑定该设备,请等待审批', status: 'pending' });
    }
    // revoked 状态:重新绑定
  }

  // 5. 查询当前有效绑定数量
  const activeCount = db.prepare(
    "SELECT COUNT(*) as cnt FROM device_bindings WHERE device_id = ? AND status = 'active'"
  ).get(serial).cnt;

  // 6. 事务执行绑定
  const bindTx = db.transaction(() => {
    // 确保 devices 表有记录(如果不存在则创建)
    const device = db.prepare('SELECT device_id FROM devices WHERE device_id = ?').get(serial);
    if (!device) {
      db.prepare(`
        INSERT INTO devices (device_id, name, model, group_name, uid, owner_id,
          bind_status, bound_at, created_at, product_type,
          has_encoder, has_buzzer, has_pressure_sensor, has_display)
        VALUES (?, ?, ?, ?, ?, NULL, 'bound', ?, ?, ?, ?, ?, ?, ?)
      `).run(
        serial,
        nextDefaultDeviceName(db, registry.product_type || 'double_post'),
        registry.model || '',
        '默认分组',
        registry.uid,
        now,
        now,
        registry.product_type || 'double_post',
        registry.has_encoder ? 1 : 0,
        registry.has_buzzer ? 1 : 0,
        registry.has_pressure_sensor ? 1 : 0,
        registry.has_display ? 1 : 0
      );
      db.prepare('INSERT OR IGNORE INTO device_status (device_id, updated_at) VALUES (?, ?)').run(serial, now);
    }

    // 更新注册表状态
    db.prepare('UPDATE device_registry SET status = ?, bound_device_id = ? WHERE id = ?')
      .run('bound', serial, registry.id);

    const bindDetail = `用户 ${userId} 通过 SN ${serial} + bind_code 绑定设备`;

    if (activeCount < AUTO_APPROVE_LIMIT) {
      // 直接绑定成功
      if (existingBinding && existingBinding.status === 'revoked') {
        // 重新激活已撤销的绑定
        db.prepare("UPDATE device_bindings SET status = 'active', bound_at = ?, revoked_at = NULL, revoked_by = NULL, revoke_reason = '' WHERE id = ?")
          .run(now, existingBinding.id);
      } else {
        db.prepare(`
          INSERT INTO device_bindings (device_id, user_id, status, bind_type, bound_at)
          VALUES (?, ?, 'active', 'normal', ?)
        `).run(serial, userId, now);
      }

      // 记录绑定日志
      db.prepare(`
        INSERT INTO binding_logs (uid, serial, device_id, user_id, action, ip, detail, created_at)
        VALUES (?, ?, ?, ?, 'bind', ?, ?, ?)
      `).run(registry.uid, serial, serial, userId, ip, bindDetail + ' (直接绑定)', now);

      db.prepare(`
        INSERT INTO operation_logs (user_id, action, device_id, detail, result, created_at)
        VALUES (?, ?, ?, ?, ?, ?)
      `).run(userId, '绑定设备', serial, bindDetail + ' (直接绑定)', '成功', now);

      return { status: 'active', code: 201 };
    } else {
      // 超过自动审批上限,创建审批申请
      if (existingBinding && existingBinding.status === 'revoked') {
        db.prepare("UPDATE device_bindings SET status = 'pending', bound_at = ?, revoked_at = NULL WHERE id = ?")
          .run(now, existingBinding.id);
      } else {
        db.prepare(`
          INSERT INTO device_bindings (device_id, user_id, status, bind_type, bound_at)
          VALUES (?, ?, 'pending', 'normal', ?)
        `).run(serial, userId, now);
      }

      // 创建审批申请
      db.prepare(`
        INSERT INTO binding_requests (device_id, user_id, serial, status, request_detail, requested_at)
        VALUES (?, ?, ?, 'pending', ?, ?)
      `).run(serial, userId, serial, bindDetail + ' (需审批)', now);

      db.prepare(`
        INSERT INTO binding_logs (uid, serial, device_id, user_id, action, ip, detail, created_at)
        VALUES (?, ?, ?, ?, 'bind_request', ?, ?, ?)
      `).run(registry.uid, serial, serial, userId, ip, bindDetail + ' (需审批)', now);

      db.prepare(`
        INSERT INTO operation_logs (user_id, action, device_id, detail, result, created_at)
        VALUES (?, ?, ?, ?, ?, ?)
      `).run(userId, '申请绑定设备', serial, bindDetail + ' (需审批)', '待审批', now);

      return { status: 'pending', code: 202 };
    }
  });

  try {
    const result = bindTx();
    const message = result.status === 'active' ? '绑定成功' : '绑定申请已提交,等待管理员审批';
    res.status(result.code).json({
      message,
      status: result.status,
      device_id: serial,
      current_bindings: activeCount + (result.status === 'active' ? 1 : 0),
      auto_approve_limit: AUTO_APPROVE_LIMIT
    });
  } catch (e) {
    console.error('[binding] bind error:', e.message);
    res.status(500).json({ error: '绑定失败,请稍后重试' });
  }
});

// 客户解绑(解除自己的绑定)
// DELETE /api/binding/:deviceId
router.delete('/:deviceId', (req, res) => {
  const deviceId = req.params.deviceId.trim();
  if (!deviceId) {
    return res.status(400).json({ error: '缺少设备编号 device_id' });
  }

  const db = getDb();
  const userId = req.user.id;
  const now = nowISO();
  const ip = getClientIp(req);

  // 查询绑定记录
  const binding = db.prepare(
    "SELECT id, status FROM device_bindings WHERE device_id = ? AND user_id = ? AND status IN ('active', 'pending')"
  ).get(deviceId, userId);

  if (!binding) {
    return res.status(404).json({ error: '未找到您的有效绑定记录' });
  }

  const tx = db.transaction(() => {
    // 撤销绑定
    db.prepare("UPDATE device_bindings SET status = 'revoked', revoked_at = ?, revoked_by = ?, revoke_reason = '用户主动解绑' WHERE id = ?")
      .run(now, userId, binding.id);

    // 取消待审批申请(完整字段:reviewed_by + review_detail)
    if (binding.status === 'pending') {
      db.prepare("UPDATE binding_requests SET status = 'cancelled', reviewed_at = ?, reviewed_by = ?, review_detail = ? WHERE device_id = ? AND user_id = ? AND status = 'pending'")
        .run(now, userId, '用户主动解绑', deviceId, userId);
    }

    // 检查是否还有其他有效绑定,如果没有则更新设备状态
    const remaining = db.prepare(
      "SELECT COUNT(*) as cnt FROM device_bindings WHERE device_id = ? AND status = 'active'"
    ).get(deviceId).cnt;

    if (remaining === 0) {
      db.prepare("UPDATE devices SET bind_status = 'unbound', bound_at = NULL WHERE device_id = ?").run(deviceId);
      db.prepare("UPDATE device_registry SET status = 'unbound', bound_device_id = '' WHERE serial = ?").run(deviceId);
    }

    // 记录日志
    db.prepare(`
      INSERT INTO binding_logs (uid, serial, device_id, user_id, action, ip, detail, created_at)
      VALUES (?, ?, ?, ?, 'unbind', ?, ?, ?)
    `).run('', deviceId, deviceId, userId, ip, `用户 ${userId} 解绑设备 ${deviceId}`, now);

    db.prepare(`
      INSERT INTO operation_logs (user_id, action, device_id, detail, result, created_at)
      VALUES (?, ?, ?, ?, ?, ?)
    `).run(userId, '解绑设备', deviceId, `用户 ${userId} 解绑设备 ${deviceId}`, '成功', now);
  });

  try {
    tx();
    res.json({ message: '解绑成功' });
  } catch (e) {
    console.error('[binding] unbind error:', e.message);
    res.status(500).json({ error: '解绑失败,请稍后重试' });
  }
});

// 兼容旧接口:POST /api/binding/unbind  Body: { device_id }
router.post('/unbind', (req, res) => {
  const deviceId = (req.body && req.body.device_id || '').trim();
  if (!deviceId) {
    return res.status(400).json({ error: '缺少设备编号 device_id' });
  }
  req.params.deviceId = deviceId;
  // 复用 DELETE 逻辑
  const userId = req.user.id;
  const db = getDb();
  const now = nowISO();
  const ip = getClientIp(req);

  const binding = db.prepare(
    "SELECT id, status FROM device_bindings WHERE device_id = ? AND user_id = ? AND status IN ('active', 'pending')"
  ).get(deviceId, userId);

  if (!binding) {
    return res.status(404).json({ error: '未找到您的有效绑定记录' });
  }

  const tx = db.transaction(() => {
    db.prepare("UPDATE device_bindings SET status = 'revoked', revoked_at = ?, revoked_by = ?, revoke_reason = '用户主动解绑' WHERE id = ?")
      .run(now, userId, binding.id);

    if (binding.status === 'pending') {
      db.prepare("UPDATE binding_requests SET status = 'cancelled', reviewed_at = ?, reviewed_by = ?, review_detail = ? WHERE device_id = ? AND user_id = ? AND status = 'pending'")
        .run(now, userId, '用户主动解绑', deviceId, userId);
    }

    const remaining = db.prepare(
      "SELECT COUNT(*) as cnt FROM device_bindings WHERE device_id = ? AND status = 'active'"
    ).get(deviceId).cnt;

    if (remaining === 0) {
      db.prepare("UPDATE devices SET bind_status = 'unbound', bound_at = NULL WHERE device_id = ?").run(deviceId);
      db.prepare("UPDATE device_registry SET status = 'unbound', bound_device_id = '' WHERE serial = ?").run(deviceId);
    }

    db.prepare(`
      INSERT INTO binding_logs (uid, serial, device_id, user_id, action, ip, detail, created_at)
      VALUES (?, ?, ?, ?, 'unbind', ?, ?, ?)
    `).run('', deviceId, deviceId, userId, ip, `用户 ${userId} 解绑设备 ${deviceId}`, now);

    db.prepare(`
      INSERT INTO operation_logs (user_id, action, device_id, detail, result, created_at)
      VALUES (?, ?, ?, ?, ?, ?)
    `).run(userId, '解绑设备', deviceId, `用户 ${userId} 解绑设备 ${deviceId}`, '成功', now);
  });

  try {
    tx();
    res.json({ message: '解绑成功' });
  } catch (e) {
    console.error('[binding] unbind error:', e.message);
    res.status(500).json({ error: '解绑失败,请稍后重试' });
  }
});

// 查询我的设备列表(通过 device_bindings 关联)
// GET /api/binding/my-devices
router.get('/my-devices', (req, res) => {
  const db = getDb();
  const userId = req.user.id;

  const rows = db.prepare(`
    SELECT d.device_id, d.name, d.model, d.group_name, d.uid,
           d.product_type, d.lift_role, d.gateway_id,
           d.has_encoder, d.has_buzzer, d.has_pressure_sensor, d.has_display,
           d.created_at,
           b.status AS bind_status, b.bound_at, b.bind_type,
           s.online, s.state, s.alarm, s.updated_at,
           s.rotary_switch,
           s.up_count, s.down_count, s.lock_count, s.refill_count,
           s.estop_count, s.photo_alarm_count,
           s.total_run_ms, s.last_run_at,
           s.run_count, s.run_time_s, s.uptime_s
    FROM device_bindings b
    INNER JOIN devices d ON b.device_id = d.device_id
    LEFT JOIN device_status s ON d.device_id = s.device_id
    WHERE b.user_id = ? AND b.status = 'active'
    ORDER BY b.bound_at DESC
  `).all(userId);

  res.json(rows.map((r) => ({
    device_id: r.device_id,
    name: r.name,
    model: r.model || '',
    group: r.group_name || '',
    uid: r.uid || '',
    product_type: r.product_type || 'double_post',
    lift_role: r.lift_role || 'main',
    gateway_id: r.gateway_id || '',
    has_encoder: r.has_encoder ? 1 : 0,
    has_buzzer: r.has_buzzer ? 1 : 0,
    has_pressure_sensor: r.has_pressure_sensor ? 1 : 0,
    has_display: r.has_display ? 1 : 0,
    bind_status: r.bind_status || 'active',
    bind_type: r.bind_type || 'normal',
    bound_at: r.bound_at || '',
    created_at: r.created_at || '',
    online: !!r.online,
    state: r.state || 'idle',
    alarm: r.alarm || 'none',
    updated_at: r.updated_at || '',
    rotary_switch: r.rotary_switch || 'main',
    up_count: r.up_count || 0,
    down_count: r.down_count || 0,
    lock_count: r.lock_count || 0,
    refill_count: r.refill_count || 0,
    estop_count: r.estop_count || 0,
    photo_alarm_count: r.photo_alarm_count || 0,
    total_run_ms: r.total_run_ms || 0,
    last_run_at: r.last_run_at || '',
    run_count: r.run_count || 0,
    run_time_s: r.run_time_s || 0,
    uptime_s: r.uptime_s || 0
  })));
});

// 查询我的待审批绑定申请
// GET /api/binding/my-requests
router.get('/my-requests', (req, res) => {
  const db = getDb();
  const userId = req.user.id;

  const rows = db.prepare(`
    SELECT r.id, r.device_id, r.serial, r.status, r.request_detail, r.review_detail,
           r.requested_at, r.reviewed_at,
           d.name AS device_name
    FROM binding_requests r
    LEFT JOIN devices d ON r.device_id = d.device_id
    WHERE r.user_id = ?
    ORDER BY r.requested_at DESC
    LIMIT 50
  `).all(userId);

  res.json(rows);
});

module.exports = router;
