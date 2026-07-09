// 客户设备绑定 API
// 提供扫码绑定(SN)、手动输入 UID 绑定、解绑、我的设备列表、SN/UID 预览查询
const express = require('express');
const { getDb } = require('../database');
const { nowISO } = require('../utils');
const { authMiddleware } = require('./auth');
const { rateLimit } = require('../rateLimit');

const router = express.Router();

// 获取客户端 IP（兼容反向代理）
function getClientIp(req) {
  const xff = req.headers['x-forwarded-for'];
  if (xff) return String(xff).split(',')[0].trim();
  return req.ip || (req.connection && req.connection.remoteAddress) || '';
}

// 规范化 UID:STM32 96位 UID = 12 字节 = 24 个十六进制字符
// 统一转大写,去除 0x 前缀和空白/冒号/连字符
function normalizeUid(raw) {
  if (!raw) return '';
  let s = String(raw).trim().toUpperCase();
  if (s.startsWith('0X')) s = s.slice(2);
  s = s.replace(/[\s:\-]/g, '');
  return s;
}

// 校验 UID 格式:24 个十六进制字符
function isValidUid(uid) {
  return /^[0-9A-F]{24}$/.test(uid);
}

function migrateDeviceIdentity(db, oldDeviceId, newDeviceId) {
  if (!oldDeviceId || !newDeviceId || oldDeviceId === newDeviceId) return;

  db.prepare(`
    INSERT INTO devices (device_id, name, model, group_name, location, gateway_id,
      product_type, lift_role, uid, owner_id, bind_status, bound_at,
      has_encoder, has_buzzer, has_pressure_sensor, has_display, created_at)
    SELECT ?, name, model, group_name, location, gateway_id,
      product_type, lift_role, uid, owner_id, bind_status, bound_at,
      has_encoder, has_buzzer, has_pressure_sensor, has_display, created_at
    FROM devices
    WHERE device_id = ?
  `).run(newDeviceId, oldDeviceId);

  db.prepare('UPDATE device_status SET device_id = ? WHERE device_id = ?').run(newDeviceId, oldDeviceId);
  db.prepare('UPDATE alarms SET device_id = ? WHERE device_id = ?').run(newDeviceId, oldDeviceId);
  db.prepare('UPDATE maintenance_records SET device_id = ? WHERE device_id = ?').run(newDeviceId, oldDeviceId);
  db.prepare('UPDATE operation_logs SET device_id = ? WHERE device_id = ?').run(newDeviceId, oldDeviceId);
  db.prepare('UPDATE command_queue SET device_id = ? WHERE device_id = ?').run(newDeviceId, oldDeviceId);
  db.prepare('UPDATE binding_logs SET device_id = ? WHERE device_id = ?').run(newDeviceId, oldDeviceId);
  db.prepare('DELETE FROM devices WHERE device_id = ?').run(oldDeviceId);
}

function migrateUnboundStatus(db, sourceDeviceId, registry) {
  const pending = db.prepare(`
    SELECT status_json, updated_at
    FROM unbound_device_status
    WHERE device_id = ? OR uid = ? OR serial = ?
    ORDER BY updated_at DESC
    LIMIT 1
  `).get(sourceDeviceId, registry.uid, registry.serial);

  if (!pending) {
    return;
  }

  try {
    const s = JSON.parse(pending.status_json || '{}');
    db.prepare(`
      INSERT INTO device_status (device_id, online, locked, state, alarm,
        height_left_mm, height_right_mm, height_diff_mm, run_count, run_time_s, uptime_s, ts_ms, updated_at,
        direction, upper_limit, lower_limit, stall, collision_up, collision_down, alarm_code, csq, dtu_state,
        left_pulse, right_pulse, buzzer_on, rotary_switch, up_count, down_count, lock_count, refill_count,
        estop_count, photo_alarm_count, total_run_ms, last_run_at, io_input_json, io_output_json)
      VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
      ON CONFLICT(device_id) DO UPDATE SET
        online=excluded.online, locked=excluded.locked, state=excluded.state, alarm=excluded.alarm,
        height_left_mm=excluded.height_left_mm, height_right_mm=excluded.height_right_mm,
        height_diff_mm=excluded.height_diff_mm, run_count=excluded.run_count, run_time_s=excluded.run_time_s,
        uptime_s=excluded.uptime_s, ts_ms=excluded.ts_ms, updated_at=excluded.updated_at,
        direction=excluded.direction, upper_limit=excluded.upper_limit, lower_limit=excluded.lower_limit,
        stall=excluded.stall, collision_up=excluded.collision_up, collision_down=excluded.collision_down,
        alarm_code=excluded.alarm_code, csq=excluded.csq, dtu_state=excluded.dtu_state,
        left_pulse=excluded.left_pulse, right_pulse=excluded.right_pulse, buzzer_on=excluded.buzzer_on,
        rotary_switch=excluded.rotary_switch, up_count=excluded.up_count, down_count=excluded.down_count,
        lock_count=excluded.lock_count, refill_count=excluded.refill_count,
        estop_count=excluded.estop_count, photo_alarm_count=excluded.photo_alarm_count,
        total_run_ms=excluded.total_run_ms, last_run_at=excluded.last_run_at,
        io_input_json=excluded.io_input_json, io_output_json=excluded.io_output_json
    `).run(
      registry.serial,
      s.online ? 1 : 0,
      s.locked ? 1 : 0,
      s.state || 'idle',
      s.alarm || 'none',
      s.height_left_mm || 0,
      s.height_right_mm || 0,
      s.height_diff_mm || 0,
      s.run_count || 0,
      s.run_time_s || 0,
      s.uptime_s || 0,
      s.ts_ms || Date.now(),
      pending.updated_at || nowISO(),
      s.direction || 'stop',
      s.upper_limit || 0,
      s.lower_limit || 0,
      s.stall || 0,
      s.collision_up || 0,
      s.collision_down || 0,
      s.alarm_code || 0,
      s.csq ?? -1,
      s.dtu_state || '',
      s.left_pulse || 0,
      s.right_pulse || 0,
      s.buzzer_on || 0,
      s.rotary_switch || 'main',
      s.up_count || 0,
      s.down_count || 0,
      s.lock_count || 0,
      s.refill_count || 0,
      s.estop_count || 0,
      s.photo_alarm_count || 0,
      s.total_run_ms || 0,
      s.last_run_at || null,
      s.io_input_json || '{}',
      s.io_output_json || '{}'
    );
  } catch (e) {
    db.prepare('INSERT OR IGNORE INTO device_status (device_id, updated_at) VALUES (?, ?)')
      .run(registry.serial, pending.updated_at || nowISO());
  }

  db.prepare('DELETE FROM unbound_device_status WHERE device_id = ? OR uid = ? OR serial = ?')
    .run(sourceDeviceId, registry.uid, registry.serial);
}

// 所有接口均需登录
router.use(authMiddleware);

// 查询某 SN 的设备信息（绑定前预览,不返回 uid）
// GET /api/binding/lookup?serial=GC-2026-00001
// 同 IP 每分钟 30 次防枚举
router.get('/lookup', rateLimit({ windowMs: 60000, max: 30 }), (req, res) => {
  const serial = (req.query.serial || '').trim();
  if (!serial) {
    return res.status(400).json({ error: '缺少设备编号 serial' });
  }

  const db = getDb();
  const row = db.prepare(`
    SELECT r.serial, r.model, r.product_type, r.display_name, r.status,
           r.has_encoder, r.has_buzzer, r.has_pressure_sensor, r.has_display,
           d.owner_id
    FROM device_registry r
    LEFT JOIN devices d ON d.uid = r.uid
    WHERE r.serial = ?
  `).get(serial);

  if (!row) {
    return res.status(404).json({ error: '设备编号不存在' });
  }

  const userId = req.user.id;
  const boundToMe = row.owner_id !== null && row.owner_id === userId;
  const boundToOther = row.owner_id !== null && row.owner_id !== userId;

  res.json({
    serial: row.serial,
    model: row.model || '',
    product_type: row.product_type || 'double_post',
    display_name: row.display_name || '',
    has_encoder: !!row.has_encoder,
    has_buzzer: !!row.has_buzzer,
    has_pressure_sensor: !!row.has_pressure_sensor,
    has_display: !!row.has_display,
    status: row.status || 'unbound',
    bound_to_me: boundToMe,
    bound_to_other: boundToOther
  });
});

// 通过 UID 查询设备信息（手动输入 UID 绑定前预览,仅返回脱敏信息）
// GET /api/binding/lookup-by-uid?uid=4A0032000000000000000000
// 同 IP 每分钟 15 次防枚举
router.get('/lookup-by-uid', rateLimit({ windowMs: 60000, max: 15 }), (req, res) => {
  const uid = normalizeUid(req.query.uid);
  if (!uid) {
    return res.status(400).json({ error: '缺少设备 UID' });
  }
  if (!isValidUid(uid)) {
    return res.status(400).json({ error: 'UID 格式错误,应为 24 位十六进制字符' });
  }

  const db = getDb();
  const row = db.prepare(`
    SELECT r.serial, r.model, r.product_type, r.display_name, r.status,
           r.has_encoder, r.has_buzzer, r.has_pressure_sensor, r.has_display,
           d.owner_id
    FROM device_registry r
    LEFT JOIN devices d ON d.uid = r.uid
    WHERE r.uid = ?
  `).get(uid);

  if (!row) {
    return res.status(404).json({ error: '该 UID 未在出厂注册表中找到,请确认 UID 是否正确或联系厂商录入' });
  }

  const userId = req.user.id;
  const boundToMe = row.owner_id !== null && row.owner_id === userId;
  const boundToOther = row.owner_id !== null && row.owner_id !== userId;

  // 返回脱敏后的 UID(前 6 位 + **** + 后 4 位),供前端回显
  const maskedUid = uid.length === 24
    ? uid.slice(0, 6) + '****' + uid.slice(-4)
    : uid;

  res.json({
    uid_masked: maskedUid,
    serial: row.serial,
    model: row.model || '',
    product_type: row.product_type || 'double_post',
    display_name: row.display_name || '',
    has_encoder: !!row.has_encoder,
    has_buzzer: !!row.has_buzzer,
    has_pressure_sensor: !!row.has_pressure_sensor,
    has_display: !!row.has_display,
    status: row.status || 'unbound',
    bound_to_me: boundToMe,
    bound_to_other: boundToOther
  });
});

// 客户绑定设备(支持 SN 或 UID,二选一)
// POST /api/binding/bind  Body: { serial?, uid? }
// 同 IP 每分钟 3 次防刷
router.post('/bind', rateLimit({ windowMs: 60000, max: 3 }), (req, res) => {
  const serial = (req.body && req.body.serial || '').trim();
  const uid = normalizeUid(req.body && req.body.uid);

  if (!serial && !uid) {
    return res.status(400).json({ error: '请提供设备编号 serial 或设备 UID' });
  }
  if (uid && !isValidUid(uid)) {
    return res.status(400).json({ error: 'UID 格式错误,应为 24 位十六进制字符' });
  }

  const db = getDb();
  const userId = req.user.id;
  const now = nowISO();
  const ip = getClientIp(req);

  // 1. 通过 serial 或 uid 查询注册记录
  let registry;
  if (serial) {
    registry = db.prepare(`SELECT id, serial, uid, model, product_type, display_name,
      has_encoder, has_buzzer, has_pressure_sensor, has_display
      FROM device_registry WHERE serial = ?`).get(serial);
    if (!registry) {
      return res.status(404).json({ error: '设备编号不存在' });
    }
  } else {
    registry = db.prepare(`SELECT id, serial, uid, model, product_type, display_name,
      has_encoder, has_buzzer, has_pressure_sensor, has_display
      FROM device_registry WHERE uid = ?`).get(uid);
    if (!registry) {
      return res.status(404).json({ error: '该 UID 未在出厂注册表中找到,请确认 UID 是否正确或联系厂商录入' });
    }
  }

  // 2. 查询 devices 表中该 uid 的设备记录
  const device = db.prepare('SELECT device_id, owner_id, bind_status FROM devices WHERE uid = ?').get(registry.uid);
  if (device && device.device_id !== registry.serial) {
    const target = db.prepare('SELECT device_id, uid FROM devices WHERE device_id = ?').get(registry.serial);
    if (target) {
      return res.status(409).json({ error: '出厂编号已对应其他设备，请先在后台检查设备注册表' });
    }
  }

  // 3. 绑定状态校验
  if (device && device.owner_id !== null) {
    if (device.owner_id === userId) {
      return res.status(409).json({ error: '该设备已绑定到您的账号' });
    }
    return res.status(409).json({ error: '该设备已被其他用户绑定' });
  }

  // 4. 事务执行绑定
  const bindTx = db.transaction(() => {
    if (device) {
      const oldDeviceId = device.device_id;
      migrateDeviceIdentity(db, device.device_id, registry.serial);
      // 已有 devices 记录,更新绑定信息
      db.prepare(`
        UPDATE devices
        SET owner_id = ?, bind_status = 'bound', bound_at = ?, product_type = ?,
            has_encoder = ?, has_buzzer = ?, has_pressure_sensor = ?, has_display = ?
        WHERE uid = ?
      `).run(
        userId, now, registry.product_type || 'double_post',
        registry.has_encoder ? 1 : 0, registry.has_buzzer ? 1 : 0,
        registry.has_pressure_sensor ? 1 : 0, registry.has_display ? 1 : 0,
        registry.uid
      );
      migrateUnboundStatus(db, oldDeviceId, registry);
    } else {
      // 没有 devices 记录,先插入一条
      db.prepare(`
        INSERT INTO devices (device_id, name, model, group_name, uid, owner_id,
          bind_status, bound_at, created_at, product_type,
          has_encoder, has_buzzer, has_pressure_sensor, has_display)
        VALUES (?, ?, ?, ?, ?, ?, 'bound', ?, ?, ?, ?, ?, ?, ?)
      `).run(
        registry.serial,
        registry.display_name || ('举升机 ' + registry.serial),
        registry.model || '',
        '默认分组',
        registry.uid,
        userId,
        now,
        now,
        registry.product_type || 'double_post',
        registry.has_encoder ? 1 : 0,
        registry.has_buzzer ? 1 : 0,
        registry.has_pressure_sensor ? 1 : 0,
        registry.has_display ? 1 : 0
      );
      // 同步初始化 device_status 记录,便于后续在线状态展示
      db.prepare('INSERT OR IGNORE INTO device_status (device_id, updated_at) VALUES (?, ?)').run(registry.serial, now);
      migrateUnboundStatus(db, registry.serial, registry);
    }

    // 5. 更新注册表状态
    db.prepare(`
      UPDATE device_registry
      SET status = 'bound', bound_device_id = ?
      WHERE id = ?
    `).run(registry.serial, registry.id);

    // 6. 记录绑定日志
    const bindDetail = uid
      ? `用户 ${userId} 通过 UID ${uid.slice(0, 6)}****${uid.slice(-4)} 绑定设备 ${registry.serial}`
      : `用户 ${userId} 通过 SN ${serial} 绑定设备 ${registry.serial}`;
    db.prepare(`
      INSERT INTO binding_logs (uid, serial, device_id, user_id, action, ip, detail, created_at)
      VALUES (?, ?, ?, ?, 'bind', ?, ?, ?)
    `).run(
      registry.uid,
      registry.serial,
      registry.serial,
      userId,
      ip,
      bindDetail,
      now
    );

    // 7. 记录平台操作日志
    db.prepare(`
      INSERT INTO operation_logs (user_id, action, device_id, detail, result, created_at)
      VALUES (?, ?, ?, ?, ?, ?)
    `).run(
      userId,
      '绑定设备',
      registry.serial,
      bindDetail,
      '成功',
      now
    );
  });

  try {
    bindTx();
    const boundDevice = db.prepare(`
      SELECT device_id, name, model, group_name, uid, product_type, lift_role,
             gateway_id, bind_status, bound_at, created_at,
             has_encoder, has_buzzer, has_pressure_sensor, has_display
      FROM devices
      WHERE device_id = ?
    `).get(registry.serial);

    res.json({
      message: '绑定成功',
      device_id: registry.serial,
      device: boundDevice ? {
        device_id: boundDevice.device_id,
        name: boundDevice.name || '',
        model: boundDevice.model || '',
        group: boundDevice.group_name || '',
        uid: boundDevice.uid || '',
        product_type: boundDevice.product_type || 'double_post',
        lift_role: boundDevice.lift_role || 'main',
        gateway_id: boundDevice.gateway_id || '',
        bind_status: boundDevice.bind_status || 'bound',
        bound_at: boundDevice.bound_at || '',
        created_at: boundDevice.created_at || '',
        has_encoder: boundDevice.has_encoder ? 1 : 0,
        has_buzzer: boundDevice.has_buzzer ? 1 : 0,
        has_pressure_sensor: boundDevice.has_pressure_sensor ? 1 : 0,
        has_display: boundDevice.has_display ? 1 : 0
      } : null
    });
  } catch (e) {
    console.error('[binding] bind error:', e.message);
    res.status(500).json({ error: '绑定失败,请稍后重试' });
  }
});

// 客户解绑
// POST /api/binding/unbind  Body: { device_id }
router.post('/unbind', (req, res) => {
  const deviceId = (req.body && req.body.device_id || '').trim();
  if (!deviceId) {
    return res.status(400).json({ error: '缺少设备编号 device_id' });
  }

  const db = getDb();
  const userId = req.user.id;
  const now = nowISO();
  const ip = getClientIp(req);

  // 1. 查询设备
  const device = db.prepare('SELECT device_id, uid, owner_id, bind_status FROM devices WHERE device_id = ?').get(deviceId);
  if (!device) {
    return res.status(404).json({ error: '设备不存在' });
  }

  // 2. 权限校验:仅设备绑定者可解绑(管理员可通过 admin 路由强制解绑)
  if (device.owner_id === null || device.owner_id !== userId) {
    return res.status(403).json({ error: '无权操作该设备' });
  }

  const uid = device.uid || '';

  // 3. 事务执行解绑
  const unbindTx = db.transaction(() => {
    db.prepare(`
      UPDATE devices
      SET owner_id = NULL, bind_status = 'unbound', bound_at = NULL
      WHERE device_id = ?
    `).run(deviceId);

    if (uid) {
      db.prepare(`
        UPDATE device_registry
        SET status = 'unbound', bound_device_id = ''
        WHERE uid = ?
      `).run(uid);
    }

    db.prepare(`
      INSERT INTO binding_logs (uid, serial, device_id, user_id, action, ip, detail, created_at)
      VALUES (?, ?, ?, ?, 'unbind', ?, ?, ?)
    `).run(
      uid,
      deviceId,
      deviceId,
      userId,
      ip,
      `用户 ${userId} 解绑设备 ${deviceId}`,
      now
    );

    db.prepare(`
      INSERT INTO operation_logs (user_id, action, device_id, detail, result, created_at)
      VALUES (?, ?, ?, ?, ?, ?)
    `).run(
      userId,
      '解绑设备',
      deviceId,
      `用户 ${userId} 解绑设备 ${deviceId}`,
      '成功',
      now
    );
  });

  try {
    unbindTx();
    res.json({ message: '解绑成功' });
  } catch (e) {
    console.error('[binding] unbind error:', e.message);
    res.status(500).json({ error: '解绑失败,请稍后重试' });
  }
});

// 查询我的设备列表（关联在线状态与多产品字段）
// GET /api/binding/my-devices
router.get('/my-devices', (req, res) => {
  const db = getDb();
  const userId = req.user.id;

  const rows = db.prepare(`
    SELECT d.device_id, d.name, d.model, d.group_name, d.uid,
           d.product_type, d.lift_role, d.gateway_id,
           d.has_encoder, d.has_buzzer, d.has_pressure_sensor, d.has_display,
           d.bind_status, d.bound_at, d.created_at,
           s.online, s.state, s.alarm, s.updated_at,
           s.rotary_switch,
           s.up_count, s.down_count, s.lock_count, s.refill_count,
           s.estop_count, s.photo_alarm_count,
           s.total_run_ms, s.last_run_at,
           s.run_count, s.run_time_s, s.uptime_s
    FROM devices d
    LEFT JOIN device_status s ON d.device_id = s.device_id
    WHERE d.owner_id = ?
    ORDER BY d.bound_at DESC
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
    bind_status: r.bind_status || 'unbound',
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

module.exports = router;
