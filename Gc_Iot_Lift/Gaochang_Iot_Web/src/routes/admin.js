const express = require('express');
const bcrypt = require('bcryptjs');
const crypto = require('crypto');
const { getDb, PRODUCT_CONFIGS, FIRMWARE_DISPLAY_NAMES } = require('../database');
const { nowISO } = require('../utils');
const { authMiddleware, adminOnly, bumpAuthVersion } = require('./auth');
const { createMsgId, enqueueAndSendCommand } = require('./commands');

const router = express.Router();

// 所有管理接口都需要登录 + admin(网站管理员)角色
router.use(authMiddleware, adminOnly);

// 角色简化为两层
const VALID_ROLES = ['admin', 'user'];

// 产品型号映射
const PRODUCT_TYPE_MAP = PRODUCT_CONFIGS.reduce((m, p) => {
  m[p.product_type] = p.display_name;
  return m;
}, {});

// 默认重置密码
const DEFAULT_RESET_PASSWORD = '123456';
const VALID_PRODUCT_TYPES = PRODUCT_CONFIGS.map(p => p.product_type);
const BIND_CODE_SALT = process.env.BIND_CODE_SALT || 'gaochang_lift_default_salt_2026';

function normalizeUid(raw) {
  if (!raw) return '';
  let s = String(raw).trim().toLowerCase();
  if (s.startsWith('0x')) s = s.slice(2);
  return s.replace(/[\s:\-]/g, '');
}

function isValidUid(uid) {
  return /^[0-9a-f]{24}$/.test(uid);
}

function boolInt(v) {
  return (v === true || v === 1 || v === '1' || v === 'true') ? 1 : 0;
}

function hashBindCode(bindCode) {
  return crypto.createHash('sha256').update(BIND_CODE_SALT + bindCode).digest('hex');
}

function normalizeBindCode(raw) {
  return raw === undefined || raw === null ? '' : String(raw).trim();
}

function getDisplayName(productType) {
  return FIRMWARE_DISPLAY_NAMES[productType] || FIRMWARE_DISPLAY_NAMES.double_post;
}

// 记录操作日志的辅助函数
function logOperation(userId, action, deviceId, detail, result) {
  const db = getDb();
  try {
    db.prepare('INSERT INTO operation_logs (user_id, action, device_id, detail, result, created_at) VALUES (?, ?, ?, ?, ?, ?)')
      .run(userId, action || '管理操作', deviceId || '', detail || '', result || '成功', nowISO());
  } catch (e) {
    console.error('[admin] 记录操作日志失败:', e.message);
  }
}

// Clear the website-side business ledger immediately. Device identity,
// registry and bindings are deliberately outside this transaction.
function initializeShippingResetWebsite(db, deviceId, operatorId, msgId) {
  const device = db.prepare(`
    SELECT d.uid, r.serial FROM devices d
    LEFT JOIN device_registry r ON r.uid = d.uid
    WHERE d.device_id = ?
  `).get(deviceId) || {};
  db.transaction(() => {
    db.prepare(`UPDATE device_status SET
      run_count=0, run_time_s=0, total_run_ms=0, up_count=0, down_count=0,
      lock_count=0, refill_count=0, estop_count=0, photo_alarm_count=0,
      total_lift_count=0, maintenance_lift_count=0, maintenance_count=0,
      last_maintenance_total=0, maintenance_due=0, last_run_at=NULL
      WHERE device_id=?`).run(deviceId);
    db.prepare('DELETE FROM alarms WHERE device_id = ?').run(deviceId);
    db.prepare('DELETE FROM maintenance_records WHERE device_id = ?').run(deviceId);
    db.prepare('DELETE FROM command_queue WHERE device_id = ?').run(deviceId);
    if (device.uid || device.serial) {
      db.prepare(`DELETE FROM device_operation_logs
        WHERE (? <> '' AND device_uid = ?) OR (? <> '' AND device_serial = ?)`)
        .run(device.uid || '', device.uid || '', device.serial || '', device.serial || '');
    }
    db.prepare(`INSERT INTO operation_logs
      (user_id, action, device_id, detail, result, created_at)
      VALUES (?, 'shipping_reset', ?, ?, 'queued', ?)`).run(
      operatorId || null, deviceId, JSON.stringify({ msg_id: msgId, mode: 'website_initialized' }), nowISO());
  })();
}

// 获取客户端 IP
function getClientIp(req) {
  return req.headers['x-forwarded-for'] || (req.socket && req.socket.remoteAddress) || '';
}

// ============ 设备注册表管理 ============

// 导出 CSV（放在 :id 之前避免被参数路由吞掉）
router.get('/registry/export', (req, res) => {
  try {
    const db = getDb();
    const rows = db.prepare(`
      SELECT r.id, r.serial, r.uid, r.product_type, r.display_name, r.model, r.batch, r.produced_at,
             r.has_encoder, r.has_buzzer, r.has_pressure_sensor, r.has_display,
             r.status, r.bound_device_id, r.created_at,
             us.online AS last_online, us.updated_at AS last_seen_at
      FROM device_registry r
      LEFT JOIN unbound_device_status us ON us.device_id = (
        SELECT latest.device_id FROM unbound_device_status latest
        WHERE latest.uid = r.uid
        ORDER BY latest.ts_ms DESC, latest.updated_at DESC, latest.device_id DESC
        LIMIT 1
      )
      ORDER BY r.id DESC
      LIMIT 10000
    `).all();

    const header = ['ID', '出厂编号', '芯片UID', '产品型号', '显示名称', '型号', '批次', '生产日期', '高度编码器', '蜂鸣器', '压力传感器', '显示屏', '状态', '绑定设备ID', '录入时间'];
    const escapeCsv = (v) => {
      const s = (v === null || v === undefined) ? '' : String(v);
      if (/[",\n\r]/.test(s)) {
        return '"' + s.replace(/"/g, '""') + '"';
      }
      return s;
    };
    const lines = [header.map(escapeCsv).join(',')];
    for (const r of rows) {
      lines.push([
        r.id, r.serial, r.uid,
        PRODUCT_TYPE_MAP[r.product_type] || r.product_type || '',
        r.display_name, r.model, r.batch, r.produced_at,
        r.has_encoder ? '是' : '否',
        r.has_buzzer ? '是' : '否',
        r.has_pressure_sensor ? '是' : '否',
        r.has_display ? '是' : '否',
        r.status, r.bound_device_id, r.created_at
      ].map(escapeCsv).join(','));
    }
    const csv = '\ufeff' + lines.join('\r\n');
    res.setHeader('Content-Type', 'text/csv; charset=utf-8');
    res.setHeader('Content-Disposition', 'attachment; filename="device_registry.csv"');
    res.send(csv);
  } catch (e) {
    console.error('[admin] 导出注册表失败:', e);
    res.status(500).json({ error: '导出失败: ' + e.message });
  }
});

// 注册表列表（支持搜索 serial/uid/model，分页）
router.get('/registry', (req, res) => {
  try {
    const db = getDb();
    const { search, status, product_type, page, pageSize } = req.query;
    const pageNum = Math.max(parseInt(page) || 1, 1);
    const size = Math.min(Math.max(parseInt(pageSize) || 20, 1), 200);
    const offset = (pageNum - 1) * size;

    let where = 'WHERE 1=1';
    const params = [];
    if (search) {
      where += ' AND (r.serial LIKE ? OR r.uid LIKE ? OR r.model LIKE ? OR r.batch LIKE ? OR r.display_name LIKE ?)';
      const kw = `%${search}%`;
      params.push(kw, kw, kw, kw, kw);
    }
    if (status) {
      where += ' AND r.status = ?';
      params.push(status);
    }
    if (product_type) {
      where += ' AND r.product_type = ?';
      params.push(product_type);
    }

    const total = db.prepare(`SELECT COUNT(*) AS cnt FROM device_registry r ${where}`).get(...params).cnt;
    const rows = db.prepare(`
      SELECT r.id, r.serial, r.uid, r.product_type, r.display_name, r.model, r.batch, r.produced_at,
             r.has_encoder, r.has_buzzer, r.has_pressure_sensor, r.has_display,
             r.status, r.bound_device_id, r.created_at,
             us.online AS last_online, us.updated_at AS last_seen_at
      FROM device_registry r
      LEFT JOIN unbound_device_status us ON us.device_id = (
        SELECT latest.device_id FROM unbound_device_status latest
        WHERE latest.uid = r.uid
        ORDER BY latest.ts_ms DESC, latest.updated_at DESC, latest.device_id DESC
        LIMIT 1
      )
      ${where}
      ORDER BY r.id DESC
      LIMIT ? OFFSET ?
    `).all(...params, size, offset);

    res.json({
      total, page: pageNum, pageSize: size,
      list: rows.map(r => ({
        ...r,
        last_online: !!r.last_online,
        last_seen_at: r.last_seen_at || '',
        product_type_name: PRODUCT_TYPE_MAP[r.product_type] || r.product_type || '两柱举升机'
      }))
    });
  } catch (e) {
    console.error('[admin] 查询注册表失败:', e);
    res.status(500).json({ error: '查询失败: ' + e.message });
  }
});

// 新增设备注册
router.post('/registry', (req, res) => {
  try {
    const { serial, product_type, model, batch, produced_at } = req.body;
    const uid = normalizeUid(req.body && req.body.uid);
    const bindCode = normalizeBindCode(req.body && req.body.bind_code);
    if (!serial || !uid) {
      return res.status(400).json({ error: '出厂编号和芯片UID不能为空' });
    }
    if (!bindCode) {
      return res.status(400).json({ error: '绑定码不能为空' });
    }
    if (!isValidUid(uid)) {
      return res.status(400).json({ error: '芯片UID格式错误，应为24位十六进制字符' });
    }
    if (product_type && !VALID_PRODUCT_TYPES.includes(product_type)) {
      return res.status(400).json({ error: '产品型号无效' });
    }
    const db = getDb();

    const dupSerial = db.prepare('SELECT id FROM device_registry WHERE serial = ?').get(serial);
    if (dupSerial) return res.status(409).json({ error: '出厂编号已存在' });
    const dupUid = db.prepare('SELECT id FROM device_registry WHERE uid = ?').get(uid);
    if (dupUid) return res.status(409).json({ error: '芯片UID已存在' });

    const result = db.prepare(`
      INSERT INTO device_registry (serial, uid, product_type, display_name, model, batch, produced_at,
        has_encoder, has_buzzer, has_pressure_sensor, has_display, bind_code_hash, status, bound_device_id, created_at)
      VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 'unbound', '', ?)
    `).run(
      serial, uid, product_type || 'double_post', getDisplayName(product_type || 'double_post'), model || '', batch || '', produced_at || '',
      boolInt(req.body.has_encoder), boolInt(req.body.has_buzzer),
      boolInt(req.body.has_pressure_sensor), boolInt(req.body.has_display),
      hashBindCode(bindCode),
      nowISO()
    );

    logOperation(req.user.id, '新增设备注册', '', `出厂编号: ${serial}, UID: ${uid}, 型号: ${product_type || 'double_post'}`, '成功');
    res.json({ id: result.lastInsertRowid, message: '新增成功' });
  } catch (e) {
    console.error('[admin] 新增注册记录失败:', e);
    res.status(500).json({ error: '新增失败: ' + e.message });
  }
});

// 批量导入
router.post('/registry/batch', (req, res) => {
  try {
    const arr = req.body;
    if (!Array.isArray(arr)) {
      return res.status(400).json({ error: '请求体必须是 JSON 数组' });
    }
    if (arr.length === 0) {
      return res.status(400).json({ error: '数组不能为空' });
    }
    if (arr.length > 5000) {
      return res.status(400).json({ error: '单次导入不能超过 5000 条' });
    }

    const db = getDb();
    const insertStmt = db.prepare(`
      INSERT INTO device_registry (serial, uid, product_type, display_name, model, batch, produced_at,
        has_encoder, has_buzzer, has_pressure_sensor, has_display, bind_code_hash, status, bound_device_id, created_at)
      VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 'unbound', '', ?)
    `);
    const checkSerial = db.prepare('SELECT id FROM device_registry WHERE serial = ?');
    const checkUid = db.prepare('SELECT id FROM device_registry WHERE uid = ?');

    let success = 0;
    let failed = 0;
    const errors = [];
    const seenSerials = new Set();
    const seenUids = new Set();

    const tx = db.transaction((items) => {
      items.forEach((item, idx) => {
        const serial = item && item.serial ? String(item.serial).trim() : '';
        const uid = normalizeUid(item && item.uid);
        const product_type = item && item.product_type ? String(item.product_type).trim() : 'double_post';
        const model = item && item.model ? String(item.model).trim() : '';
        const batch = item && item.batch ? String(item.batch).trim() : '';
        const produced_at = item && item.produced_at ? String(item.produced_at).trim() : '';
        const bind_code = normalizeBindCode(item && item.bind_code);
        const bind_code_hash = item && item.bind_code_hash ? String(item.bind_code_hash).trim().toLowerCase() : '';

        if (!serial || !uid) {
          failed++;
          errors.push(`第${idx + 1}行: 出厂编号或UID为空`);
          return;
        }
        if (!isValidUid(uid)) {
          failed++;
          errors.push(`第${idx + 1}行: UID ${uid} 格式错误，应为24位十六进制字符`);
          return;
        }
        if (!VALID_PRODUCT_TYPES.includes(product_type)) {
          failed++;
          errors.push(`第${idx + 1}行: 产品型号 ${product_type} 无效`);
          return;
        }
        if (!bind_code && !/^[0-9a-f]{64}$/.test(bind_code_hash)) {
          failed++;
          errors.push(`第${idx + 1}行: 绑定码为空或 bind_code_hash 格式错误`);
          return;
        }
        if (seenSerials.has(serial)) {
          failed++;
          errors.push(`第${idx + 1}行: 出厂编号 ${serial} 在本次导入中重复`);
          return;
        }
        if (seenUids.has(uid)) {
          failed++;
          errors.push(`第${idx + 1}行: UID ${uid} 在本次导入中重复`);
          return;
        }
        if (checkSerial.get(serial)) {
          failed++;
          errors.push(`第${idx + 1}行: 出厂编号 ${serial} 已存在`);
          return;
        }
        if (checkUid.get(uid)) {
          failed++;
          errors.push(`第${idx + 1}行: UID ${uid} 已存在`);
          return;
        }
        try {
          insertStmt.run(
            serial, uid, product_type, getDisplayName(product_type), model, batch, produced_at,
            boolInt(item.has_encoder), boolInt(item.has_buzzer),
            boolInt(item.has_pressure_sensor), boolInt(item.has_display),
            bind_code ? hashBindCode(bind_code) : bind_code_hash,
            nowISO()
          );
          seenSerials.add(serial);
          seenUids.add(uid);
          success++;
        } catch (err) {
          failed++;
          errors.push(`第${idx + 1}行: ${err.message}`);
        }
      });
    });
    tx(arr);

    logOperation(req.user.id, '批量导入设备注册', '', `成功 ${success} 条, 失败 ${failed} 条`, '成功');
    res.json({ success, failed, total: arr.length, errors: errors.slice(0, 100) });
  } catch (e) {
    console.error('[admin] 批量导入失败:', e);
    res.status(500).json({ error: '批量导入失败: ' + e.message });
  }
});

// 编辑注册记录
router.put('/registry/:id', (req, res) => {
  try {
    const id = parseInt(req.params.id);
    const { serial, product_type, model, batch, produced_at } = req.body;
    const uid = normalizeUid(req.body && req.body.uid);
    const bindCode = normalizeBindCode(req.body && req.body.bind_code);
    if (!serial || !uid) {
      return res.status(400).json({ error: '出厂编号和芯片UID不能为空' });
    }
    if (!isValidUid(uid)) {
      return res.status(400).json({ error: '芯片UID格式错误，应为24位十六进制字符' });
    }
    if (product_type && !VALID_PRODUCT_TYPES.includes(product_type)) {
      return res.status(400).json({ error: '产品型号无效' });
    }
    const db = getDb();
    const existing = db.prepare('SELECT * FROM device_registry WHERE id = ?').get(id);
    if (!existing) {
      return res.status(404).json({ error: '记录不存在' });
    }

    const dupSerial = db.prepare('SELECT id FROM device_registry WHERE serial = ? AND id != ?').get(serial, id);
    if (dupSerial) return res.status(409).json({ error: '出厂编号已存在' });
    const dupUid = db.prepare('SELECT id FROM device_registry WHERE uid = ? AND id != ?').get(uid, id);
    if (dupUid) return res.status(409).json({ error: '芯片UID已存在' });
    if ((existing.status === 'bound' || existing.bound_device_id) && serial !== existing.serial) {
      return res.status(409).json({ error: '已绑定设备不可修改出厂编号，请先解绑后再修改' });
    }

    // 事务:同时更新 device_registry 和 devices,保证两表数据一致
    const editTx = db.transaction(() => {
      db.prepare(`
        UPDATE device_registry
        SET serial = ?, uid = ?, product_type = ?, display_name = ?, model = ?, batch = ?, produced_at = ?,
            has_encoder = ?, has_buzzer = ?, has_pressure_sensor = ?, has_display = ?
        WHERE id = ?
      `).run(
        serial, uid, product_type || 'double_post', getDisplayName(product_type || 'double_post'), model || '', batch || '', produced_at || '',
        boolInt(req.body.has_encoder), boolInt(req.body.has_buzzer),
        boolInt(req.body.has_pressure_sensor), boolInt(req.body.has_display),
        id
      );

      // 同步更新已绑定设备的型号、UID 和能力，避免注册表与设备表分叉
      if (existing.status === 'bound' || existing.bound_device_id) {
        db.prepare(`UPDATE devices SET uid = ?, product_type = ?, has_encoder = ?, has_buzzer = ?,
          has_pressure_sensor = ?, has_display = ? WHERE uid = ? OR device_id = ?`)
          .run(
            uid,
            product_type || 'double_post',
            boolInt(req.body.has_encoder), boolInt(req.body.has_buzzer),
            boolInt(req.body.has_pressure_sensor), boolInt(req.body.has_display),
            existing.uid,
            existing.bound_device_id || ''
          );
      }

      if (bindCode) {
        db.prepare('UPDATE device_registry SET bind_code_hash = ? WHERE id = ?')
          .run(hashBindCode(bindCode), id);
      }
    });
    editTx();

    logOperation(req.user.id, '编辑设备注册', '', `ID: ${id}, 编号: ${serial}`, '成功');
    res.json({ message: '修改成功' });
  } catch (e) {
    console.error('[admin] 编辑注册记录失败:', e);
    res.status(500).json({ error: '修改失败: ' + e.message });
  }
});

// 删除注册记录（只能删 unbound）
router.delete('/registry/:id', (req, res) => {
  try {
    const id = parseInt(req.params.id);
    const db = getDb();
    const row = db.prepare('SELECT * FROM device_registry WHERE id = ?').get(id);
    if (!row) {
      return res.status(404).json({ error: '记录不存在' });
    }
    if (row.status !== 'unbound') {
      return res.status(400).json({ error: '已绑定的记录不能删除，请先解绑' });
    }
    db.prepare('DELETE FROM device_registry WHERE id = ?').run(id);
    logOperation(req.user.id, '删除设备注册', '', `ID: ${id}, 编号: ${row.serial}, UID: ${row.uid}`, '成功');
    res.json({ message: '删除成功' });
  } catch (e) {
    console.error('[admin] 删除注册记录失败:', e);
    res.status(500).json({ error: '删除失败: ' + e.message });
  }
});

// ============ 绑定看板(基于 device_bindings 多对多) ============

// 绑定看板:展示每台设备当前的所有绑定用户
// 支持按 bind_status / product_type / 关键词搜索
router.get('/bindings', (req, res) => {
  try {
    const db = getDb();
    const { status, search, product_type, page, pageSize } = req.query;
    const pageNum = Math.max(parseInt(page) || 1, 1);
    const size = Math.min(Math.max(parseInt(pageSize) || 20, 1), 200);
    const offset = (pageNum - 1) * size;

    let where = 'WHERE 1=1';
    const params = [];
    if (status) {
      where += ' AND d.bind_status = ?';
      params.push(status);
    }
    if (product_type) {
      where += ' AND d.product_type = ?';
      params.push(product_type);
    }
    if (search) {
      where += ' AND (d.device_id LIKE ? OR d.name LIKE ? OR d.uid LIKE ? OR r.serial LIKE ?)';
      const kw = `%${search}%`;
      params.push(kw, kw, kw, kw);
    }

    // 主查询:设备基础信息 + 绑定数量
    const total = db.prepare(`
      SELECT COUNT(*) AS cnt
      FROM devices d
      LEFT JOIN device_registry r ON d.uid = r.uid
      ${where}
    `).get(...params).cnt;

    const rows = db.prepare(`
      SELECT d.device_id, d.name, d.model, d.gateway_id, d.uid, d.product_type,
             d.bind_status, d.bound_at, d.group_name, d.location,
             d.has_encoder, d.has_buzzer, d.has_pressure_sensor, d.has_display,
             r.serial, r.batch, r.display_name,
             s.online,
             (SELECT COUNT(*) FROM device_bindings b
              WHERE b.device_id = d.device_id AND b.status = 'active') AS active_binding_count,
             (SELECT COUNT(*) FROM device_bindings b
              WHERE b.device_id = d.device_id AND b.status = 'pending') AS pending_binding_count
      FROM devices d
      LEFT JOIN device_registry r ON d.uid = r.uid
      LEFT JOIN device_status s ON d.device_id = s.device_id
      ${where}
      ORDER BY d.bind_status DESC, d.bound_at DESC
      LIMIT ? OFFSET ?
    `).all(...params, size, offset);

    // 拉取每台设备的绑定用户列表
    const deviceIds = rows.map(r => r.device_id);
    let bindingUsers = [];
    if (deviceIds.length > 0) {
      const placeholders = deviceIds.map(() => '?').join(',');
      bindingUsers = db.prepare(`
        SELECT b.device_id, b.user_id, b.status, b.bind_type, b.bound_at,
               u.username, u.real_name, u.phone
        FROM device_bindings b
        LEFT JOIN users u ON b.user_id = u.id
        WHERE b.device_id IN (${placeholders})
          AND b.status IN ('active', 'pending')
        ORDER BY b.device_id, b.status DESC, b.bound_at ASC
      `).all(...deviceIds);
    }
    const bindingMap = {};
    for (const bu of bindingUsers) {
      if (!bindingMap[bu.device_id]) bindingMap[bu.device_id] = [];
      bindingMap[bu.device_id].push({
        user_id: bu.user_id,
        username: bu.username,
        real_name: bu.real_name,
        phone: bu.phone,
        status: bu.status,
        bind_type: bu.bind_type,
        bound_at: bu.bound_at
      });
    }

    res.json({
      total, page: pageNum, pageSize: size,
      list: rows.map(r => ({
        device_id: r.device_id,
        name: r.name,
        model: r.model,
        gateway_id: r.gateway_id || '',
        uid: r.uid,
        product_type: r.product_type || 'double_post',
        product_type_name: PRODUCT_TYPE_MAP[r.product_type] || r.product_type || '两柱举升机',
        serial: r.serial,
        batch: r.batch,
        display_name: r.display_name,
        bind_status: r.bind_status,
        bound_at: r.bound_at,
        group_name: r.group_name,
        location: r.location,
        has_encoder: !!r.has_encoder,
        has_buzzer: !!r.has_buzzer,
        has_pressure_sensor: !!r.has_pressure_sensor,
        has_display: !!r.has_display,
        online: !!r.online,
        active_binding_count: r.active_binding_count || 0,
        pending_binding_count: r.pending_binding_count || 0,
        bindings: bindingMap[r.device_id] || []
      }))
    });
  } catch (e) {
    console.error('[admin] 查询绑定看板失败:', e);
    res.status(500).json({ error: '查询失败: ' + e.message });
  }
});

// 发货前数据清理设备列表。账号筛选只匹配有效绑定，不改变绑定关系。
router.get('/shipping-reset/devices', (req, res) => {
  try {
    const db = getDb();
    const { account, search, product_type, online, queue } = req.query;
    let where = 'WHERE 1=1';
    const params = [];

    if (product_type) {
      where += ' AND d.product_type = ?';
      params.push(product_type);
    }
    if (online === '1' || online === '0') {
      where += ' AND COALESCE(s.online, 0) = ?';
      params.push(online === '1' ? 1 : 0);
    }
    if (search) {
      const keyword = `%${search}%`;
      where += ' AND (d.device_id LIKE ? OR d.name LIKE ? OR d.uid LIKE ? OR r.serial LIKE ?)';
      params.push(keyword, keyword, keyword, keyword);
    }
    if (account) {
      const keyword = `%${account}%`;
      where += ` AND EXISTS (
        SELECT 1 FROM device_bindings account_binding
        JOIN users account_user ON account_user.id = account_binding.user_id
        WHERE account_binding.device_id = d.device_id AND account_binding.status = 'active'
          AND (account_user.username LIKE ? OR account_user.real_name LIKE ? OR account_user.phone LIKE ?)
      )`;
      params.push(keyword, keyword, keyword);
    }
    if (queue === '1') {
      where += ` AND EXISTS (
        SELECT 1 FROM command_queue queue_filter
         WHERE queue_filter.device_id = d.device_id
           AND queue_filter.purpose IN ('shipping_reset_deferred', 'shipping_reset', 'shipping_reset_admin_enter', 'shipping_reset_admin_exit')
           AND queue_filter.status IN ('pending', 'sent')
      )`;
    }

    const rows = db.prepare(`
      SELECT d.device_id, d.name, d.uid, d.product_type, r.serial,
             COALESCE(s.online, 0) AS online,
             COALESCE(s.run_count, 0) AS run_count,
             COALESCE(s.run_time_s, 0) AS run_time_s,
             COALESCE(s.total_lift_count, 0) AS total_lift_count,
             COALESCE(s.maintenance_lift_count, 0) AS maintenance_lift_count,
             (SELECT COUNT(*) FROM alarms a WHERE a.device_id = d.device_id) AS alarm_count,
             (SELECT COUNT(*) FROM maintenance_records m WHERE m.device_id = d.device_id) AS maintenance_record_count,
             (SELECT COUNT(*) FROM command_queue q WHERE q.device_id = d.device_id) AS command_count,
             (SELECT COUNT(*) FROM device_operation_logs o
               WHERE (d.uid <> '' AND o.device_uid = d.uid) OR (r.serial IS NOT NULL AND r.serial <> '' AND o.device_serial = r.serial)) AS device_log_count,
             (SELECT GROUP_CONCAT(account_user.username, ' / ')
                FROM device_bindings account_binding
                JOIN users account_user ON account_user.id = account_binding.user_id
               WHERE account_binding.device_id = d.device_id AND account_binding.status = 'active') AS account_names,
             (SELECT COUNT(*) FROM command_queue pending
               WHERE pending.device_id = d.device_id
                 AND pending.purpose IN ('shipping_reset_deferred', 'shipping_reset', 'shipping_reset_admin_enter', 'shipping_reset_admin_exit')
                 AND pending.status IN ('pending', 'sent')) AS reset_pending
             ,(SELECT pending.cmd FROM command_queue pending
                WHERE pending.device_id = d.device_id
                  AND pending.purpose IN ('shipping_reset_deferred', 'shipping_reset', 'shipping_reset_admin_enter', 'shipping_reset_admin_exit')
                  AND pending.status IN ('pending', 'sent')
                ORDER BY pending.id DESC LIMIT 1) AS reset_cmd
             ,(SELECT pending.msg_id FROM command_queue pending
                WHERE pending.device_id = d.device_id
                  AND pending.purpose IN ('shipping_reset_deferred', 'shipping_reset', 'shipping_reset_admin_enter', 'shipping_reset_admin_exit')
                  AND pending.status IN ('pending', 'sent')
                ORDER BY pending.id DESC LIMIT 1) AS reset_msg_id
             ,(SELECT pending.status FROM command_queue pending
                WHERE pending.device_id = d.device_id
                  AND pending.purpose IN ('shipping_reset_deferred', 'shipping_reset', 'shipping_reset_admin_enter', 'shipping_reset_admin_exit')
                  AND pending.status IN ('pending', 'sent')
                ORDER BY pending.id DESC LIMIT 1) AS reset_status
             ,(SELECT pending.purpose FROM command_queue pending
                WHERE pending.device_id = d.device_id
                  AND pending.purpose IN ('shipping_reset_deferred', 'shipping_reset', 'shipping_reset_admin_enter', 'shipping_reset_admin_exit')
                  AND pending.status IN ('pending', 'sent')
                ORDER BY pending.id DESC LIMIT 1) AS reset_purpose
             ,(SELECT pending.created_at FROM command_queue pending
                WHERE pending.device_id = d.device_id
                  AND pending.purpose IN ('shipping_reset_deferred', 'shipping_reset', 'shipping_reset_admin_enter', 'shipping_reset_admin_exit')
                  AND pending.status IN ('pending', 'sent')
                ORDER BY pending.id DESC LIMIT 1) AS reset_created_at
             ,(SELECT pending.sent_at FROM command_queue pending
                WHERE pending.device_id = d.device_id
                  AND pending.purpose IN ('shipping_reset_deferred', 'shipping_reset', 'shipping_reset_admin_enter', 'shipping_reset_admin_exit')
                  AND pending.status IN ('pending', 'sent')
                ORDER BY pending.id DESC LIMIT 1) AS reset_sent_at
        FROM devices d
        LEFT JOIN device_status s ON s.device_id = d.device_id
        LEFT JOIN device_registry r ON r.uid = d.uid
        ${where}
       ORDER BY d.product_type, d.device_id
       LIMIT 500
    `).all(...params);

    res.json({
      list: rows.map(row => ({
        ...row,
        online: !!row.online,
        reset_pending: !!row.reset_pending,
        reset_queue: row.reset_status ? {
          cmd: row.reset_cmd,
          msg_id: row.reset_msg_id,
          status: row.reset_status,
          purpose: row.reset_purpose,
          created_at: row.reset_created_at,
          sent_at: row.reset_sent_at
        } : null,
        product_type_name: PRODUCT_TYPE_MAP[row.product_type] || row.product_type || '两柱举升机',
        account_names: row.account_names || '未绑定账号'
      }))
    });
  } catch (e) {
    console.error('[admin] 查询发货前清理设备失败:', e);
    res.status(500).json({ error: '查询失败: ' + e.message });
  }
});

router.post('/shipping-reset/start', (req, res) => {
  try {
    const deviceIds = Array.isArray(req.body && req.body.device_ids)
      ? [...new Set(req.body.device_ids.map(value => String(value || '').trim()).filter(Boolean))]
      : [];
    if (deviceIds.length === 0) return res.status(400).json({ error: '请选择至少一台设备' });
    if (deviceIds.length > 100) return res.status(400).json({ error: '单次最多清理 100 台设备' });

    const db = getDb();
    const results = [];
    for (const deviceId of deviceIds) {
      const device = db.prepare(`
        SELECT d.device_id, d.name, d.product_type, COALESCE(s.online, 0) AS online
          FROM devices d LEFT JOIN device_status s ON s.device_id = d.device_id
         WHERE d.device_id = ?
      `).get(deviceId);
      if (!device) {
        results.push({ device_id: deviceId, status: 'failed', error: '设备不存在' });
        continue;
      }
      const existing = db.prepare(`
        SELECT msg_id, status FROM command_queue
         WHERE device_id = ? AND purpose IN ('shipping_reset_deferred', 'shipping_reset', 'shipping_reset_admin_enter', 'shipping_reset_admin_exit')
           AND status IN ('pending', 'sent')
         ORDER BY id DESC LIMIT 1
      `).get(deviceId);
      if (existing) {
        results.push({ device_id: deviceId, msg_id: existing.msg_id, status: existing.status, existing: true });
        continue;
      }

      const msgId = createMsgId();
      const isLargeScissor = device.product_type === 'large_scissor';
      const initialCmd = isLargeScissor ? 'admin_enter' : 'reset_usage';
      const purpose = device.online
        ? (isLargeScissor ? 'shipping_reset_admin_enter' : 'shipping_reset')
        : 'shipping_reset_deferred';
      const extra = isLargeScissor
        ? { purpose, password: process.env.LIFT_IOT_ADMIN_PASSWORD || '123456', deferIfOffline: !device.online }
        : { purpose, deferIfOffline: !device.online };
      initializeShippingResetWebsite(db, deviceId, req.user.id, msgId);
      const queued = enqueueAndSendCommand(db, deviceId, initialCmd, msgId, extra, req.user);
      logOperation(req.user.id, '发货前清除', deviceId,
        `设备 ${device.name}; ${device.online ? (isLargeScissor ? '先进入管理员模式，再清除 Flash 台账' : '等待设备清除 Flash 后回执') : '网站已初始化，等待设备上线自动清除 Flash 台账'}`,
        queued.sent ? '已下发' : (queued.deferred ? '等待设备上线' : '下发失败'));
      results.push({
        device_id: deviceId,
        msg_id: queued.msg_id || msgId,
        status: queued.status,
        error: queued.error || ''
      });
    }

    res.json({ results });
  } catch (e) {
    console.error('[admin] 启动发货前清理失败:', e);
    res.status(500).json({ error: '启动失败: ' + e.message });
  }
});

// 管理员强制解除指定用户的绑定(支持多对多)
// POST /api/admin/bindings/:deviceId/unbind  Body: { user_id? }
// 不传 user_id 则解除该设备的全部绑定
router.post('/bindings/:deviceId/unbind', (req, res) => {
  try {
    const deviceId = req.params.deviceId;
    const targetUserId = req.body && req.body.user_id ? parseInt(req.body.user_id) : null;
    const reason = (req.body && req.body.reason) || '管理员强制解绑';

    const db = getDb();
    const device = db.prepare('SELECT device_id, name, uid FROM devices WHERE device_id = ?').get(deviceId);
    if (!device) {
      return res.status(404).json({ error: '设备不存在' });
    }

    const uid = device.uid || '';

    // 查询将被解除的绑定
    let bindingsToRevoke;
    if (targetUserId) {
      bindingsToRevoke = db.prepare(
        "SELECT id, user_id, status FROM device_bindings WHERE device_id = ? AND user_id = ? AND status IN ('active', 'pending')"
      ).all(deviceId, targetUserId);
    } else {
      bindingsToRevoke = db.prepare(
        "SELECT id, user_id, status FROM device_bindings WHERE device_id = ? AND status IN ('active', 'pending')"
      ).all(deviceId);
    }

    if (bindingsToRevoke.length === 0) {
      return res.status(400).json({ error: '该设备当前没有有效绑定' });
    }

    const tx = db.transaction(() => {
      const now = nowISO();
      for (const b of bindingsToRevoke) {
        db.prepare("UPDATE device_bindings SET status = 'revoked', revoked_at = ?, revoked_by = ?, revoke_reason = ? WHERE id = ?")
          .run(now, req.user.id, reason, b.id);

        if (b.status === 'pending') {
          db.prepare("UPDATE binding_requests SET status = 'cancelled', reviewed_at = ?, reviewed_by = ?, review_detail = ? WHERE device_id = ? AND user_id = ? AND status = 'pending'")
            .run(now, req.user.id, '管理员强制解绑时取消', deviceId, b.user_id);
        }

        db.prepare(`
          INSERT INTO binding_logs (uid, serial, device_id, user_id, action, ip, detail, created_at)
          VALUES (?, ?, ?, ?, 'unbind', ?, ?, ?)
        `).run(uid, '', deviceId, b.user_id, getClientIp(req),
          `管理员强制解绑(目标用户ID: ${b.user_id}, 原因: ${reason})`, now);
      }

      // 检查是否还有有效绑定,如果没有则更新设备状态
      const remaining = db.prepare(
        "SELECT COUNT(*) as cnt FROM device_bindings WHERE device_id = ? AND status = 'active'"
      ).get(deviceId).cnt;
      if (remaining === 0) {
        db.prepare("UPDATE devices SET bind_status = 'unbound', bound_at = NULL WHERE device_id = ?").run(deviceId);
        if (uid) {
          db.prepare("UPDATE device_registry SET status = 'unbound', bound_device_id = '' WHERE uid = ?").run(uid);
        }
      }
    });
    tx();

    logOperation(req.user.id, '强制解绑设备', deviceId,
      `设备: ${device.name}, UID: ${uid}, 解除绑定数: ${bindingsToRevoke.length}, 原因: ${reason}`, '成功');
    res.json({
      message: '解绑成功',
      revoked_count: bindingsToRevoke.length
    });
  } catch (e) {
    console.error('[admin] 强制解绑失败:', e);
    res.status(500).json({ error: '解绑失败: ' + e.message });
  }
});

// ============ 用户管理 ============

router.get('/users', (req, res) => {
  try {
    const db = getDb();
    const users = db.prepare(`
      SELECT id, username, role, real_name, phone, created_at, last_login, enabled, must_change_password
      FROM users ORDER BY id ASC
    `).all();
    res.json(users);
  } catch (e) {
    console.error('[admin] 查询用户列表失败:', e);
    res.status(500).json({ error: '查询失败: ' + e.message });
  }
});

// 创建用户(admin 可创建 admin 或 user)
router.post('/users', (req, res) => {
  try {
    const { username, password, role, real_name, phone } = req.body;
    if (!username || !password) {
      return res.status(400).json({ error: '用户名和密码不能为空' });
    }
    if (!/^[a-zA-Z0-9_]{3,20}$/.test(username)) {
      return res.status(400).json({ error: '用户名只能包含字母、数字、下划线，3-20位' });
    }
    if (password.length < 6) {
      return res.status(400).json({ error: '密码至少6位' });
    }
    const finalRole = VALID_ROLES.includes(role) ? role : 'user';

    const db = getDb();
    const existing = db.prepare('SELECT id FROM users WHERE username = ?').get(username);
    if (existing) {
      return res.status(409).json({ error: '用户名已存在' });
    }

    const hash = bcrypt.hashSync(password, 10);
    const result = db.prepare(`
      INSERT INTO users (username, password_hash, role, real_name, phone, created_at, must_change_password)
      VALUES (?, ?, ?, ?, ?, ?, 1)
    `).run(username, hash, finalRole, real_name || '', phone || '', nowISO());

    logOperation(req.user.id, '创建用户', '', `用户: ${username}, 角色: ${finalRole}`, '成功');
    res.json({ id: result.lastInsertRowid, message: '创建成功' });
  } catch (e) {
    console.error('[admin] 创建用户失败:', e);
    res.status(500).json({ error: '创建失败: ' + e.message });
  }
});

// 修改用户角色(仅 admin/user 两层,且不能改自己)
// 按计划 P3:改角色后立即失效旧 JWT
router.put('/users/:id/role', (req, res) => {
  try {
    const id = parseInt(req.params.id);
    const { role } = req.body;
    if (!VALID_ROLES.includes(role)) {
      return res.status(400).json({ error: '角色无效,只能是 admin 或 user' });
    }
    if (id === req.user.id) {
      return res.status(400).json({ error: '不能修改自己的角色' });
    }
    const db = getDb();
    const user = db.prepare('SELECT username, role FROM users WHERE id = ?').get(id);
    if (!user) {
      return res.status(404).json({ error: '用户不存在' });
    }
    db.prepare('UPDATE users SET role = ? WHERE id = ?').run(role, id);
    // 角色变更后立即失效旧 JWT
    bumpAuthVersion(id);
    logOperation(req.user.id, '修改用户角色', '',
      `用户: ${user.username}, 角色: ${user.role} -> ${role}`, '成功');
    res.json({ message: '角色已更新' });
  } catch (e) {
    console.error('[admin] 修改角色失败:', e);
    res.status(500).json({ error: '修改失败: ' + e.message });
  }
});

// 重置用户密码
// 按计划 P3:重置密码后立即失效旧 JWT
// 不再静默回退到默认弱密码,必须显式传入合法新密码
router.put('/users/:id/reset-password', (req, res) => {
  try {
    const id = parseInt(req.params.id);
    const { new_password } = req.body;
    if (!new_password || new_password.length < 6) {
      return res.status(400).json({ error: '新密码不能为空且至少6位,请显式传入 new_password' });
    }
    const db = getDb();
    const user = db.prepare('SELECT username FROM users WHERE id = ?').get(id);
    if (!user) {
      return res.status(404).json({ error: '用户不存在' });
    }
    const hash = bcrypt.hashSync(new_password, 10);
    db.prepare('UPDATE users SET password_hash = ?, must_change_password = 1 WHERE id = ?').run(hash, id);
    // 密码重置后立即失效旧 JWT
    bumpAuthVersion(id);
    logOperation(req.user.id, '重置用户密码', '', `用户: ${user.username}`, '成功');
    res.json({ message: '密码已重置,请通知用户使用新密码登录' });
  } catch (e) {
    console.error('[admin] 重置密码失败:', e);
    res.status(500).json({ error: '重置失败: ' + e.message });
  }
});

// 启用/禁用用户
// 按计划 P3:禁用用户后立即失效旧 JWT
router.put('/users/:id/toggle', (req, res) => {
  try {
    const id = parseInt(req.params.id);
    const db = getDb();
    const user = db.prepare('SELECT username, enabled FROM users WHERE id = ?').get(id);
    if (!user) {
      return res.status(404).json({ error: '用户不存在' });
    }
    if (id === req.user.id) {
      return res.status(400).json({ error: '不能禁用自己' });
    }
    const newEnabled = user.enabled ? 0 : 1;
    db.prepare('UPDATE users SET enabled = ? WHERE id = ?').run(newEnabled, id);
    // 状态变更后立即失效旧 JWT
    bumpAuthVersion(id);
    logOperation(req.user.id, newEnabled ? '启用用户' : '禁用用户', '',
      `用户: ${user.username}`, '成功');
    res.json({ enabled: newEnabled });
  } catch (e) {
    console.error('[admin] 切换用户状态失败:', e);
    res.status(500).json({ error: '操作失败: ' + e.message });
  }
});

// 修改用户资料(按计划 P3:"资料编辑")
// 按计划 P3:改密码后立即失效旧 JWT
router.put('/users/:id/profile', (req, res) => {
  try {
    const id = parseInt(req.params.id);
    const { real_name, phone } = req.body || {};
    const db = getDb();
    const user = db.prepare('SELECT username FROM users WHERE id = ?').get(id);
    if (!user) {
      return res.status(404).json({ error: '用户不存在' });
    }
    db.prepare('UPDATE users SET real_name = ?, phone = ? WHERE id = ?')
      .run(real_name || '', phone || '', id);
    logOperation(req.user.id, '编辑用户资料', '', `用户: ${user.username}`, '成功');
    res.json({ message: '资料已更新' });
  } catch (e) {
    console.error('[admin] 编辑用户资料失败:', e);
    res.status(500).json({ error: '修改失败: ' + e.message });
  }
});

// 删除用户(不能删自己)
// 按计划 P3:使用事务解除全部绑定 + 取消待审批申请,再删除账号
router.delete('/users/:id', (req, res) => {
  try {
    const id = parseInt(req.params.id);
    if (id === req.user.id) {
      return res.status(400).json({ error: '不能删除自己' });
    }
    const db = getDb();
    const user = db.prepare('SELECT username FROM users WHERE id = ?').get(id);
    if (!user) {
      return res.status(404).json({ error: '用户不存在' });
    }

    const tx = db.transaction(() => {
      const now = nowISO();
      // 1. 解除该用户的全部绑定
      const userBindings = db.prepare(
        "SELECT id, device_id, status FROM device_bindings WHERE user_id = ? AND status IN ('active', 'pending')"
      ).all(id);
      for (const b of userBindings) {
        db.prepare("UPDATE device_bindings SET status = 'revoked', revoked_at = ?, revoked_by = ?, revoke_reason = ? WHERE id = ?")
          .run(now, req.user.id, '用户被删除', b.id);

        // 若设备没有其他有效绑定,则更新设备状态
        const remaining = db.prepare(
          "SELECT COUNT(*) as cnt FROM device_bindings WHERE device_id = ? AND status = 'active'"
        ).get(b.device_id).cnt;
        if (remaining === 0) {
          db.prepare("UPDATE devices SET bind_status = 'unbound', bound_at = NULL WHERE device_id = ?").run(b.device_id);
          const dev = db.prepare('SELECT uid FROM devices WHERE device_id = ?').get(b.device_id);
          if (dev && dev.uid) {
            db.prepare("UPDATE device_registry SET status = 'unbound', bound_device_id = '' WHERE uid = ?").run(dev.uid);
          }
        }
      }

      // 2. 取消该用户的全部待审批申请
      db.prepare("UPDATE binding_requests SET status = 'cancelled', reviewed_at = ?, reviewed_by = ?, review_detail = ? WHERE user_id = ? AND status = 'pending'")
        .run(now, req.user.id, '用户被删除', id);

      // 3. 删除用户(bumpAuthVersion 无意义因为账号已不存在,但保持调用一致性)
      bumpAuthVersion(id);
      db.prepare('DELETE FROM users WHERE id = ?').run(id);
    });
    tx();

    logOperation(req.user.id, '删除用户', '', `用户: ${user.username}`, '成功');
    res.json({ message: '删除成功' });
  } catch (e) {
    console.error('[admin] 删除用户失败:', e);
    res.status(500).json({ error: '删除失败: ' + e.message });
  }
});

// 用户详情(按计划 P3:"用户详情"接口)
// GET /api/admin/users/:id
router.get('/users/:id', (req, res) => {
  try {
    const id = parseInt(req.params.id);
    const db = getDb();
    const user = db.prepare(`
      SELECT id, username, role, real_name, phone, created_at, last_login, enabled, must_change_password
      FROM users WHERE id = ?
    `).get(id);
    if (!user) {
      return res.status(404).json({ error: '用户不存在' });
    }
    // 绑定设备数量统计
    const stats = db.prepare(`
      SELECT
        (SELECT COUNT(*) FROM device_bindings WHERE user_id = ? AND status = 'active') AS active_count,
        (SELECT COUNT(*) FROM device_bindings WHERE user_id = ? AND status = 'pending') AS pending_count,
        (SELECT COUNT(*) FROM device_bindings WHERE user_id = ? AND status = 'revoked') AS revoked_count
    `).get(id, id, id);
    res.json({ ...user, binding_stats: stats });
  } catch (e) {
    console.error('[admin] 查询用户详情失败:', e);
    res.status(500).json({ error: '查询失败: ' + e.message });
  }
});

// 查询指定用户绑定的设备(按计划 P3:"用户绑定设备"接口)
// GET /api/admin/users/:id/devices
router.get('/users/:id/devices', (req, res) => {
  try {
    const id = parseInt(req.params.id);
    const db = getDb();
    const user = db.prepare('SELECT id, username FROM users WHERE id = ?').get(id);
    if (!user) {
      return res.status(404).json({ error: '用户不存在' });
    }
    const rows = db.prepare(`
      SELECT b.device_id, b.status, b.bind_type, b.bound_at, b.revoked_at, b.revoke_reason,
             d.name, d.uid, d.product_type, d.bind_status AS device_bind_status,
             r.serial, r.display_name,
             s.online
      FROM device_bindings b
      LEFT JOIN devices d ON b.device_id = d.device_id
      LEFT JOIN device_registry r ON d.uid = r.uid
      LEFT JOIN device_status s ON d.device_id = s.device_id
      WHERE b.user_id = ?
      ORDER BY b.status DESC, b.bound_at DESC
    `).all(id);
    res.json({
      user_id: id,
      username: user.username,
      devices: rows.map(r => ({
        device_id: r.device_id,
        name: r.name,
        uid: r.uid,
        product_type: r.product_type || 'double_post',
        product_type_name: PRODUCT_TYPE_MAP[r.product_type] || r.product_type || '两柱举升机',
        serial: r.serial,
        display_name: r.display_name,
        binding_status: r.status,
        bind_type: r.bind_type,
        bound_at: r.bound_at,
        revoked_at: r.revoked_at,
        revoke_reason: r.revoke_reason,
        device_bind_status: r.device_bind_status,
        online: !!r.online
      }))
    });
  } catch (e) {
    console.error('[admin] 查询用户绑定设备失败:', e);
    res.status(500).json({ error: '查询失败: ' + e.message });
  }
});

// 用户审计记录(按计划 P3:"用户审计记录"接口)
// GET /api/admin/users/:id/audit
router.get('/users/:id/audit', (req, res) => {
  try {
    const id = parseInt(req.params.id);
    const db = getDb();
    const user = db.prepare('SELECT id, username FROM users WHERE id = ?').get(id);
    if (!user) {
      return res.status(404).json({ error: '用户不存在' });
    }

    // 该用户的绑定日志 + 操作日志(按时间倒序合并,SQL 层加 LIMIT 避免全表加载)
    const bindingLogs = db.prepare(`
      SELECT 'binding' AS source, id, created_at, action, device_id, detail, '' AS result, uid, serial, ip
      FROM binding_logs WHERE user_id = ?
      ORDER BY created_at DESC
      LIMIT 500
    `).all(id);
    const opLogs = db.prepare(`
      SELECT 'operation' AS source, id, created_at, action, device_id, detail, result, '' AS uid, '' AS serial, '' AS ip
      FROM operation_logs WHERE user_id = ?
      ORDER BY created_at DESC
      LIMIT 500
    `).all(id);

    const combined = [...bindingLogs, ...opLogs]
      .sort((a, b) => (b.created_at || '').localeCompare(a.created_at || ''));

    res.json({
      user_id: id,
      username: user.username,
      records: combined.slice(0, 500)
    });
  } catch (e) {
    console.error('[admin] 查询用户审计失败:', e);
    res.status(500).json({ error: '查询失败: ' + e.message });
  }
});

// ============ 日志查询(平台操作 + 绑定 + 设备端操作) ============

router.get('/logs', (req, res) => {
  try {
    const db = getDb();
    const { device_id, user_id, action, source, start_date, end_date, page, pageSize } = req.query;
    const pageNum = Math.max(parseInt(page) || 1, 1);
    const size = Math.min(Math.max(parseInt(pageSize) || 20, 1), 200);
    const offset = (pageNum - 1) * size;

    let outer = 'WHERE 1=1';
    const params = [];
    if (device_id) {
      outer += ' AND device_id LIKE ?';
      params.push(`%${device_id}%`);
    }
    if (user_id) {
      outer += ' AND user_id = ?';
      params.push(parseInt(user_id));
    }
    if (action) {
      outer += ' AND action LIKE ?';
      params.push(`%${action}%`);
    }
    if (source) {
      outer += ' AND source = ?';
      params.push(source);
    }
    if (start_date) {
      outer += ' AND created_at >= ?';
      params.push(start_date.replace('T', ' '));
    }
    if (end_date) {
      outer += ' AND created_at <= ?';
      const endVal = end_date.includes('T') ? end_date.replace('T', ' ') : end_date + ' 23:59:59';
      params.push(endVal);
    }

    const baseSql = `
      SELECT * FROM (
        SELECT 'operation' AS source, l.id, l.created_at, l.action, l.device_id, l.user_id,
               u.username, u.real_name, l.detail, l.result, '' AS uid, '' AS serial, '' AS ip
        FROM operation_logs l
        LEFT JOIN users u ON l.user_id = u.id
        UNION ALL
        SELECT 'binding' AS source, b.id, b.created_at, b.action, b.device_id, b.user_id,
               u2.username, u2.real_name, b.detail, '' AS result, b.uid, b.serial, b.ip
        FROM binding_logs b
        LEFT JOIN users u2 ON b.user_id = u2.id
      ) AS combined
      ${outer}
      ORDER BY created_at DESC
    `;

    const total = db.prepare(`SELECT COUNT(*) AS cnt FROM (${baseSql})`).get(...params).cnt;
    const rows = db.prepare(`${baseSql} LIMIT ? OFFSET ?`).all(...params, size, offset);

    res.json({ total, page: pageNum, pageSize: size, list: rows });
  } catch (e) {
    console.error('[admin] 查询日志失败:', e);
    res.status(500).json({ error: '查询失败: ' + e.message });
  }
});

// ============ 绑定审批中心(按计划 P3) ============

// 待审批绑定申请列表
// GET /api/admin/binding-requests?status=pending&device_id=&user_id=&page=&pageSize=
router.get('/binding-requests', (req, res) => {
  try {
    const db = getDb();
    const { status, device_id, user_id, page, pageSize } = req.query;
    const pageNum = Math.max(parseInt(page) || 1, 1);
    const size = Math.min(Math.max(parseInt(pageSize) || 20, 1), 200);
    const offset = (pageNum - 1) * size;

    let where = 'WHERE 1=1';
    const params = [];
    if (status) {
      where += ' AND br.status = ?';
      params.push(status);
    } else {
      // 默认只看 pending
      where += " AND br.status = 'pending'";
    }
    if (device_id) {
      where += ' AND br.device_id = ?';
      params.push(device_id);
    }
    if (user_id) {
      where += ' AND br.user_id = ?';
      params.push(parseInt(user_id));
    }

    const total = db.prepare(`SELECT COUNT(*) AS cnt FROM binding_requests br ${where}`).get(...params).cnt;
    const rows = db.prepare(`
      SELECT br.id, br.device_id, br.user_id, br.serial, br.status, br.request_detail,
             br.requested_at, br.reviewed_at, br.reviewed_by, br.review_detail,
             u.username, u.real_name, u.phone,
             d.name AS device_name, d.uid, d.product_type,
             r.display_name AS registry_display_name,
             (SELECT COUNT(*) FROM device_bindings b WHERE b.device_id = br.device_id AND b.status = 'active') AS current_active_count
      FROM binding_requests br
      LEFT JOIN users u ON br.user_id = u.id
      LEFT JOIN devices d ON br.device_id = d.device_id
      LEFT JOIN device_registry r ON d.uid = r.uid
      ${where}
      ORDER BY br.requested_at DESC
      LIMIT ? OFFSET ?
    `).all(...params, size, offset);

    res.json({
      total, page: pageNum, pageSize: size,
      list: rows.map(r => ({
        id: r.id,
        device_id: r.device_id,
        device_name: r.device_name,
        uid: r.uid,
        product_type: r.product_type || 'double_post',
        product_type_name: PRODUCT_TYPE_MAP[r.product_type] || r.product_type || '两柱举升机',
        registry_display_name: r.registry_display_name,
        user_id: r.user_id,
        username: r.username,
        real_name: r.real_name,
        phone: r.phone,
        serial: r.serial,
        status: r.status,
        request_detail: r.request_detail,
        requested_at: r.requested_at,
        reviewed_at: r.reviewed_at,
        reviewer_id: r.reviewed_by,
        review_note: r.review_detail,
        current_active_count: r.current_active_count || 0
      }))
    });
  } catch (e) {
    console.error('[admin] 查询绑定审批失败:', e);
    res.status(500).json({ error: '查询失败: ' + e.message });
  }
});

// 审批历史(包含 approved/rejected/cancelled)
// GET /api/admin/binding-requests/history?device_id=&user_id=&page=&pageSize=
router.get('/binding-requests/history', (req, res) => {
  try {
    const db = getDb();
    const { device_id, user_id, page, pageSize } = req.query;
    const pageNum = Math.max(parseInt(page) || 1, 1);
    const size = Math.min(Math.max(parseInt(pageSize) || 20, 1), 200);
    const offset = (pageNum - 1) * size;

    let where = "WHERE br.status IN ('approved', 'rejected', 'cancelled')";
    const params = [];
    if (device_id) {
      where += ' AND br.device_id = ?';
      params.push(device_id);
    }
    if (user_id) {
      where += ' AND br.user_id = ?';
      params.push(parseInt(user_id));
    }

    const total = db.prepare(`SELECT COUNT(*) AS cnt FROM binding_requests br ${where}`).get(...params).cnt;
    const rows = db.prepare(`
      SELECT br.id, br.device_id, br.user_id, br.serial, br.status, br.request_detail,
             br.requested_at, br.reviewed_at, br.reviewed_by, br.review_detail,
             u.username, u.real_name,
             d.name AS device_name, d.uid,
             reviewer.username AS reviewer_name
      FROM binding_requests br
      LEFT JOIN users u ON br.user_id = u.id
      LEFT JOIN devices d ON br.device_id = d.device_id
      LEFT JOIN users reviewer ON br.reviewed_by = reviewer.id
      ${where}
      ORDER BY br.reviewed_at DESC
      LIMIT ? OFFSET ?
    `).all(...params, size, offset);

    res.json({
      total, page: pageNum, pageSize: size, list: rows
    });
  } catch (e) {
    console.error('[admin] 查询审批历史失败:', e);
    res.status(500).json({ error: '查询失败: ' + e.message });
  }
});

// 批准绑定申请(按计划 P3:"管理员批准后可以继续增加账号,不设置特批后的硬上限")
// POST /api/admin/binding-requests/:id/approve  Body: { note? }
router.post('/binding-requests/:id/approve', (req, res) => {
  try {
    const requestId = parseInt(req.params.id);
    const note = (req.body && req.body.note) || '管理员批准';
    const db = getDb();
    const now = nowISO();

    const req_ = db.prepare('SELECT * FROM binding_requests WHERE id = ?').get(requestId);
    if (!req_) {
      return res.status(404).json({ error: '审批申请不存在' });
    }
    if (req_.status !== 'pending') {
      return res.status(400).json({ error: `该申请当前状态为 ${req_.status},不能批准` });
    }

    const tx = db.transaction(() => {
      // 更新申请状态
      db.prepare("UPDATE binding_requests SET status = 'approved', reviewed_at = ?, reviewed_by = ?, review_detail = ? WHERE id = ?")
        .run(now, req.user.id, note, requestId);

      // 将对应的 device_bindings 从 pending 改为 active
      db.prepare("UPDATE device_bindings SET status = 'active' WHERE device_id = ? AND user_id = ? AND status = 'pending'")
        .run(req_.device_id, req_.user_id);

      // 更新设备状态为 bound
      db.prepare("UPDATE devices SET bind_status = 'bound', bound_at = COALESCE(bound_at, ?) WHERE device_id = ?")
        .run(now, req_.device_id);

      // 记录日志
      db.prepare(`
        INSERT INTO binding_logs (uid, serial, device_id, user_id, action, ip, detail, created_at)
        VALUES (?, ?, ?, ?, 'bind_approve', ?, ?, ?)
      `).run(
        '', req_.serial || '', req_.device_id, req_.user_id,
        getClientIp(req),
        `管理员批准绑定申请(ID: ${requestId}, 备注: ${note})`, now
      );

      db.prepare(`
        INSERT INTO operation_logs (user_id, action, device_id, detail, result, created_at)
        VALUES (?, ?, ?, ?, ?, ?)
      `).run(req.user.id, '批准绑定申请', req_.device_id,
        `批准用户 ${req_.user_id} 绑定设备 ${req_.device_id}, 备注: ${note}`, '成功', now);
    });
    tx();

    logOperation(req.user.id, '批准绑定申请', req_.device_id,
      `申请ID: ${requestId}, 用户ID: ${req_.user_id}, 备注: ${note}`, '成功');
    res.json({ message: '已批准绑定申请' });
  } catch (e) {
    console.error('[admin] 批准绑定申请失败:', e);
    res.status(500).json({ error: '操作失败: ' + e.message });
  }
});

// 拒绝绑定申请
// POST /api/admin/binding-requests/:id/reject  Body: { note? }
router.post('/binding-requests/:id/reject', (req, res) => {
  try {
    const requestId = parseInt(req.params.id);
    const note = (req.body && req.body.note) || '管理员拒绝';
    const db = getDb();
    const now = nowISO();

    const req_ = db.prepare('SELECT * FROM binding_requests WHERE id = ?').get(requestId);
    if (!req_) {
      return res.status(404).json({ error: '审批申请不存在' });
    }
    if (req_.status !== 'pending') {
      return res.status(400).json({ error: `该申请当前状态为 ${req_.status},不能拒绝` });
    }

    const tx = db.transaction(() => {
      db.prepare("UPDATE binding_requests SET status = 'rejected', reviewed_at = ?, reviewed_by = ?, review_detail = ? WHERE id = ?")
        .run(now, req.user.id, note, requestId);

      // 对应的 device_bindings 改为 rejected
      db.prepare("UPDATE device_bindings SET status = 'revoked', revoked_at = ?, revoked_by = ?, revoke_reason = ? WHERE device_id = ? AND user_id = ? AND status = 'pending'")
        .run(now, req.user.id, '审批被拒绝: ' + note, req_.device_id, req_.user_id);

      db.prepare(`
        INSERT INTO binding_logs (uid, serial, device_id, user_id, action, ip, detail, created_at)
        VALUES (?, ?, ?, ?, 'bind_reject', ?, ?, ?)
      `).run(
        '', req_.serial || '', req_.device_id, req_.user_id,
        getClientIp(req),
        `管理员拒绝绑定申请(ID: ${requestId}, 备注: ${note})`, now
      );

      db.prepare(`
        INSERT INTO operation_logs (user_id, action, device_id, detail, result, created_at)
        VALUES (?, ?, ?, ?, ?, ?)
      `).run(req.user.id, '拒绝绑定申请', req_.device_id,
        `拒绝用户 ${req_.user_id} 绑定设备 ${req_.device_id}, 备注: ${note}`, '成功', now);
    });
    tx();

    logOperation(req.user.id, '拒绝绑定申请', req_.device_id,
      `申请ID: ${requestId}, 用户ID: ${req_.user_id}, 备注: ${note}`, '成功');
    res.json({ message: '已拒绝绑定申请' });
  } catch (e) {
    console.error('[admin] 拒绝绑定申请失败:', e);
    res.status(500).json({ error: '操作失败: ' + e.message });
  }
});

// 管理员强制绑定设备到指定用户(按计划 P3:"强制绑定")
// POST /api/admin/bindings/force-bind  Body: { device_id, user_id, note? }
router.post('/bindings/force-bind', (req, res) => {
  try {
    const { device_id, user_id, note } = req.body || {};
    if (!device_id || !user_id) {
      return res.status(400).json({ error: '缺少 device_id 或 user_id' });
    }
    const db = getDb();
    const now = nowISO();

    const device = db.prepare('SELECT device_id, name, uid FROM devices WHERE device_id = ?').get(device_id);
    if (!device) return res.status(404).json({ error: '设备不存在' });
    const user = db.prepare('SELECT id, username FROM users WHERE id = ?').get(user_id);
    if (!user) return res.status(404).json({ error: '用户不存在' });

    // 检查是否已绑定
    const existing = db.prepare(
      "SELECT id, status FROM device_bindings WHERE device_id = ? AND user_id = ?"
    ).get(device_id, user_id);
    if (existing && existing.status === 'active') {
      return res.status(409).json({ error: '该用户已绑定此设备' });
    }

    const tx = db.transaction(() => {
      if (existing && existing.status === 'revoked') {
        db.prepare("UPDATE device_bindings SET status = 'active', bind_type = 'special', bound_at = ?, revoked_at = NULL, revoked_by = NULL, revoke_reason = '' WHERE id = ?")
          .run(now, existing.id);
      } else if (existing && existing.status === 'pending') {
        db.prepare("UPDATE device_bindings SET status = 'active', bind_type = 'special' WHERE id = ?")
          .run(existing.id);
      } else {
        db.prepare(`
          INSERT INTO device_bindings (device_id, user_id, status, bind_type, bound_at)
          VALUES (?, ?, 'active', 'special', ?)
        `).run(device_id, user_id, now);
      }

      // 同步更新设备绑定状态
      db.prepare("UPDATE devices SET bind_status = 'bound', bound_at = COALESCE(bound_at, ?) WHERE device_id = ?")
        .run(now, device_id);

      db.prepare(`
        INSERT INTO binding_logs (uid, serial, device_id, user_id, action, ip, detail, created_at)
        VALUES (?, ?, ?, ?, 'bind_force', ?, ?, ?)
      `).run(
        device.uid || '', '', device_id, user_id, getClientIp(req),
        `管理员强制绑定(用户: ${user.username}, 备注: ${note || '无'})`, now
      );
    });
    tx();

    logOperation(req.user.id, '强制绑定设备', device_id,
      `设备: ${device.name}, 用户: ${user.username}`, '成功');
    res.json({ message: '强制绑定成功' });
  } catch (e) {
    console.error('[admin] 强制绑定失败:', e);
    res.status(500).json({ error: '操作失败: ' + e.message });
  }
});

// ============ 仪表盘统计 ============

router.get('/stats', (req, res) => {
  try {
    const db = getDb();
    const today = new Date().toISOString().substring(0, 10);

    const totalDevices = db.prepare('SELECT COUNT(*) AS c FROM devices').get().c;
    const boundDevices = db.prepare("SELECT COUNT(*) AS c FROM devices WHERE bind_status = 'bound'").get().c;
    const onlineDevices = db.prepare('SELECT COUNT(*) AS c FROM device_status WHERE online = 1').get().c;
    const totalUsers = db.prepare('SELECT COUNT(*) AS c FROM users').get().c;
    const enabledUsers = db.prepare('SELECT COUNT(*) AS c FROM users WHERE enabled = 1').get().c;

    const totalRegistry = db.prepare('SELECT COUNT(*) AS c FROM device_registry').get().c;
    const unboundRegistry = db.prepare("SELECT COUNT(*) AS c FROM device_registry WHERE status = 'unbound'").get().c;
    const boundRegistry = db.prepare("SELECT COUNT(*) AS c FROM device_registry WHERE status = 'bound'").get().c;

    const todayBindings = db.prepare(`
      SELECT COUNT(*) AS c FROM binding_logs
      WHERE action = 'bind' AND created_at >= ?
    `).get(today + ' 00:00:00').c;

    const todayUnbindings = db.prepare(`
      SELECT COUNT(*) AS c FROM binding_logs
      WHERE action = 'unbind' AND created_at >= ?
    `).get(today + ' 00:00:00').c;

    const todayOperations = db.prepare(`
      SELECT COUNT(*) AS c FROM operation_logs
      WHERE created_at >= ?
    `).get(today + ' 00:00:00').c;

    const todayDeviceOps = db.prepare(`
      SELECT COUNT(*) AS c FROM device_operation_logs
      WHERE occurred_at >= ?
    `).get(today + ' 00:00:00').c;

    // 最近 7 天绑定趋势
    const trend = db.prepare(`
      SELECT DATE(created_at) AS day, action, COUNT(*) AS c
      FROM binding_logs
      WHERE created_at >= ?
      GROUP BY DATE(created_at), action
      ORDER BY day ASC
    `).all(getDateNDaysAgo(6) + ' 00:00:00');

    // 按产品型号分布
    const productDist = db.prepare(`
      SELECT d.product_type,
        COUNT(*) AS total,
        SUM(CASE WHEN s.online = 1 THEN 1 ELSE 0 END) AS online_count,
        SUM(CASE WHEN d.bind_status = 'bound' THEN 1 ELSE 0 END) AS bound_count
      FROM devices d
      LEFT JOIN device_status s ON d.device_id = s.device_id
      GROUP BY d.product_type
    `).all();

    // 设备端操作统计(今日)
    const deviceOpStats = db.prepare(`
      SELECT op_type, COUNT(*) AS c
      FROM device_operation_logs
      WHERE occurred_at >= ?
      GROUP BY op_type
    `).all(today + ' 00:00:00');

    res.json({
      devices: {
        total: totalDevices,
        bound: boundDevices,
        unbound: totalDevices - boundDevices,
        online: onlineDevices,
        offline: totalDevices - onlineDevices
      },
      users: {
        total: totalUsers,
        enabled: enabledUsers,
        disabled: totalUsers - enabledUsers
      },
      registry: {
        total: totalRegistry,
        unbound: unboundRegistry,
        bound: boundRegistry
      },
      today: {
        bindings: todayBindings,
        unbindings: todayUnbindings,
        operations: todayOperations,
        device_ops: todayDeviceOps
      },
      trend,
      product_distribution: productDist.map(r => ({
        product_type: r.product_type || 'double_post',
        product_type_name: PRODUCT_TYPE_MAP[r.product_type] || r.product_type || '两柱举升机',
        total: r.total || 0,
        online: r.online_count || 0,
        bound: r.bound_count || 0
      })),
      device_op_stats: deviceOpStats.map(r => ({
        op_type: r.op_type,
        count: r.c || 0
      }))
    });
  } catch (e) {
    console.error('[admin] 查询统计失败:', e);
    res.status(500).json({ error: '查询失败: ' + e.message });
  }
});

function getDateNDaysAgo(n) {
  const d = new Date();
  d.setDate(d.getDate() - n);
  return d.toISOString().substring(0, 10);
}

module.exports = router;
module.exports.initializeShippingResetWebsite = initializeShippingResetWebsite;
