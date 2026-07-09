const express = require('express');
const bcrypt = require('bcryptjs');
const { getDb, PRODUCT_CONFIGS } = require('../database');
const { nowISO } = require('../utils');
const { authMiddleware, adminOnly } = require('./auth');

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

function normalizeUid(raw) {
  if (!raw) return '';
  let s = String(raw).trim().toUpperCase();
  if (s.startsWith('0X')) s = s.slice(2);
  return s.replace(/[\s:\-]/g, '');
}

function isValidUid(uid) {
  return /^[0-9A-F]{24}$/.test(uid);
}

function boolInt(v) {
  return (v === true || v === 1 || v === '1' || v === 'true') ? 1 : 0;
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
      LEFT JOIN unbound_device_status us ON us.uid = r.uid
      ORDER BY r.id DESC
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
      FROM device_registry
      r
      LEFT JOIN unbound_device_status us ON us.uid = r.uid
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
    const { serial, product_type, display_name, model, batch, produced_at } = req.body;
    const uid = normalizeUid(req.body && req.body.uid);
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

    const dupSerial = db.prepare('SELECT id FROM device_registry WHERE serial = ?').get(serial);
    if (dupSerial) return res.status(409).json({ error: '出厂编号已存在' });
    const dupUid = db.prepare('SELECT id FROM device_registry WHERE uid = ?').get(uid);
    if (dupUid) return res.status(409).json({ error: '芯片UID已存在' });

    const result = db.prepare(`
      INSERT INTO device_registry (serial, uid, product_type, display_name, model, batch, produced_at,
        has_encoder, has_buzzer, has_pressure_sensor, has_display, status, bound_device_id, created_at)
      VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 'unbound', '', ?)
    `).run(
      serial, uid, product_type || 'double_post', display_name || '', model || '', batch || '', produced_at || '',
      boolInt(req.body.has_encoder), boolInt(req.body.has_buzzer),
      boolInt(req.body.has_pressure_sensor), boolInt(req.body.has_display),
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
        has_encoder, has_buzzer, has_pressure_sensor, has_display, status, bound_device_id, created_at)
      VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 'unbound', '', ?)
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
        const display_name = item && item.display_name ? String(item.display_name).trim() : '';
        const model = item && item.model ? String(item.model).trim() : '';
        const batch = item && item.batch ? String(item.batch).trim() : '';
        const produced_at = item && item.produced_at ? String(item.produced_at).trim() : '';

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
            serial, uid, product_type, display_name, model, batch, produced_at,
            boolInt(item.has_encoder), boolInt(item.has_buzzer),
            boolInt(item.has_pressure_sensor), boolInt(item.has_display),
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
    const { serial, product_type, display_name, model, batch, produced_at } = req.body;
    const uid = normalizeUid(req.body && req.body.uid);
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

    db.prepare(`
      UPDATE device_registry
      SET serial = ?, uid = ?, product_type = ?, display_name = ?, model = ?, batch = ?, produced_at = ?,
          has_encoder = ?, has_buzzer = ?, has_pressure_sensor = ?, has_display = ?
      WHERE id = ?
    `).run(
      serial, uid, product_type || 'double_post', display_name || '', model || '', batch || '', produced_at || '',
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

// ============ 绑定看板 ============

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
      where += ' AND (d.device_id LIKE ? OR d.name LIKE ? OR d.uid LIKE ? OR r.serial LIKE ? OR u.username LIKE ? OR u.real_name LIKE ?)';
      const kw = `%${search}%`;
      params.push(kw, kw, kw, kw, kw, kw);
    }

    const total = db.prepare(`
      SELECT COUNT(*) AS cnt
      FROM devices d
      LEFT JOIN device_registry r ON d.uid = r.uid
      LEFT JOIN users u ON d.owner_id = u.id
      ${where}
    `).get(...params).cnt;

    const rows = db.prepare(`
      SELECT d.device_id, d.name, d.model, d.gateway_id, d.uid, d.product_type, d.bind_status, d.bound_at, d.group_name, d.location,
             d.has_encoder, d.has_buzzer, d.has_pressure_sensor, d.has_display,
             r.serial, r.batch, r.display_name,
             u.id AS owner_id, u.username AS owner_username, u.real_name AS owner_real_name,
             s.online
      FROM devices d
      LEFT JOIN device_registry r ON d.uid = r.uid
      LEFT JOIN users u ON d.owner_id = u.id
      LEFT JOIN device_status s ON d.device_id = s.device_id
      ${where}
      ORDER BY d.bind_status DESC, d.bound_at DESC
      LIMIT ? OFFSET ?
    `).all(...params, size, offset);

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
        owner_id: r.owner_id,
        owner_username: r.owner_username,
        owner_real_name: r.owner_real_name
      }))
    });
  } catch (e) {
    console.error('[admin] 查询绑定看板失败:', e);
    res.status(500).json({ error: '查询失败: ' + e.message });
  }
});

// 管理员强制解绑设备
router.post('/bindings/:deviceId/unbind', (req, res) => {
  try {
    const deviceId = req.params.deviceId;
    const db = getDb();
    const device = db.prepare('SELECT * FROM devices WHERE device_id = ?').get(deviceId);
    if (!device) {
      return res.status(404).json({ error: '设备不存在' });
    }
    if (device.bind_status !== 'bound' || !device.owner_id) {
      return res.status(400).json({ error: '该设备当前未绑定' });
    }

    const uid = device.uid || '';
    const ownerId = device.owner_id;
    const ownerUser = db.prepare('SELECT username FROM users WHERE id = ?').get(ownerId);
    const ownerName = ownerUser ? ownerUser.username : String(ownerId);

    const tx = db.transaction(() => {
      db.prepare(`
        UPDATE devices SET owner_id = NULL, bind_status = 'unbound', bound_at = NULL
        WHERE device_id = ?
      `).run(deviceId);

      if (uid) {
        db.prepare(`
          UPDATE device_registry SET status = 'unbound', bound_device_id = ''
          WHERE uid = ?
        `).run(uid);
      }

      db.prepare(`
        INSERT INTO binding_logs (uid, serial, device_id, user_id, action, ip, detail, created_at)
        VALUES (?, ?, ?, ?, 'unbind', ?, ?, ?)
      `).run(
        uid, '', deviceId, req.user.id, getClientIp(req),
        `管理员强制解绑（原绑定用户: ${ownerName}）`, nowISO()
      );
    });
    tx();

    logOperation(req.user.id, '强制解绑设备', deviceId,
      `设备: ${device.name}, UID: ${uid}, 原用户: ${ownerName}`, '成功');
    res.json({ message: '解绑成功' });
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
    logOperation(req.user.id, '修改用户角色', '',
      `用户: ${user.username}, 角色: ${user.role} -> ${role}`, '成功');
    res.json({ message: '角色已更新' });
  } catch (e) {
    console.error('[admin] 修改角色失败:', e);
    res.status(500).json({ error: '修改失败: ' + e.message });
  }
});

// 重置用户密码
router.put('/users/:id/reset-password', (req, res) => {
  try {
    const id = parseInt(req.params.id);
    const { new_password } = req.body;
    const pwd = new_password && new_password.length >= 6 ? new_password : DEFAULT_RESET_PASSWORD;
    const db = getDb();
    const user = db.prepare('SELECT username FROM users WHERE id = ?').get(id);
    if (!user) {
      return res.status(404).json({ error: '用户不存在' });
    }
    const hash = bcrypt.hashSync(pwd, 10);
    db.prepare('UPDATE users SET password_hash = ?, must_change_password = 1 WHERE id = ?').run(hash, id);
    logOperation(req.user.id, '重置用户密码', '', `用户: ${user.username}`, '成功');
    res.json({ message: '密码已重置', default_password: pwd === DEFAULT_RESET_PASSWORD ? DEFAULT_RESET_PASSWORD : undefined });
  } catch (e) {
    console.error('[admin] 重置密码失败:', e);
    res.status(500).json({ error: '重置失败: ' + e.message });
  }
});

// 启用/禁用用户
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
    logOperation(req.user.id, newEnabled ? '启用用户' : '禁用用户', '',
      `用户: ${user.username}`, '成功');
    res.json({ enabled: newEnabled });
  } catch (e) {
    console.error('[admin] 切换用户状态失败:', e);
    res.status(500).json({ error: '操作失败: ' + e.message });
  }
});

// 删除用户（不能删自己）
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
    db.prepare('DELETE FROM users WHERE id = ?').run(id);
    logOperation(req.user.id, '删除用户', '', `用户: ${user.username}`, '成功');
    res.json({ message: '删除成功' });
  } catch (e) {
    console.error('[admin] 删除用户失败:', e);
    res.status(500).json({ error: '删除失败: ' + e.message });
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
      params.push(start_date);
    }
    if (end_date) {
      outer += ' AND created_at <= ?';
      params.push(end_date + ' 23:59:59');
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
