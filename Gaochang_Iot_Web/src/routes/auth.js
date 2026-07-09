const express = require('express');
const bcrypt = require('bcryptjs');
const jwt = require('jsonwebtoken');
const { getDb } = require('../database');
const { nowISO } = require('../utils');
const { rateLimit } = require('../rateLimit');

const router = express.Router();
const JWT_SECRET = process.env.JWT_SECRET || 'lift-monitor-secret-key-2024';
const JWT_EXPIRES_IN = '24h';

// 角色简化为两层:admin(网站管理员,最高权限) / user(普通用户,只能看自己绑定的设备)
const VALID_ROLES = ['admin', 'user'];

function generateToken(user) {
  return jwt.sign(
    { id: user.id, username: user.username, role: user.role },
    JWT_SECRET,
    { expiresIn: JWT_EXPIRES_IN }
  );
}

// 通用登录验证
function authMiddleware(req, res, next) {
  const header = req.headers.authorization;
  if (!header || !header.startsWith('Bearer ')) {
    return res.status(401).json({ error: '未登录' });
  }
  try {
    const decoded = jwt.verify(header.slice(7), JWT_SECRET);
    req.user = decoded;
    next();
  } catch (e) {
    return res.status(401).json({ error: '登录已过期，请重新登录' });
  }
}

// 角色权限校验:admin 拥有所有权限,user 只能访问自己的资源
function roleMiddleware(...roles) {
  return (req, res, next) => {
    if (!roles.includes(req.user.role)) {
      return res.status(403).json({ error: '权限不足' });
    }
    next();
  };
}

// 仅 admin(网站管理员)可访问
function adminOnly(req, res, next) {
  if (req.user.role !== 'admin') {
    return res.status(403).json({ error: '需要网站管理员权限' });
  }
  next();
}

// 登录
router.post('/login', (req, res) => {
  const { username, password } = req.body;
  if (!username || !password) {
    return res.status(400).json({ error: '用户名和密码不能为空' });
  }

  const db = getDb();
  const user = db.prepare('SELECT * FROM users WHERE username = ? AND enabled = 1').get(username);
  if (!user) {
    return res.status(401).json({ error: '用户名或密码错误' });
  }

  if (!bcrypt.compareSync(password, user.password_hash)) {
    return res.status(401).json({ error: '用户名或密码错误' });
  }

  db.prepare('UPDATE users SET last_login = ? WHERE id = ?').run(nowISO(), user.id);

  const token = generateToken(user);
  res.json({
    token,
    user: {
      id: user.id,
      username: user.username,
      role: user.role,
      real_name: user.real_name,
      must_change_password: user.must_change_password || 0
    }
  });
});

// 管理员创建用户(admin 可创建 user 或 admin)
router.post('/register', authMiddleware, adminOnly, (req, res) => {
  const { username, password, role, real_name, phone } = req.body;
  if (!username || !password) {
    return res.status(400).json({ error: '用户名和密码不能为空' });
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
  try {
    const result = db.prepare(
      'INSERT INTO users (username, password_hash, role, real_name, phone, created_at) VALUES (?, ?, ?, ?, ?, ?)'
    ).run(username, hash, finalRole, real_name || '', phone || '', nowISO());

    db.prepare('INSERT INTO operation_logs (user_id, action, detail, result, created_at) VALUES (?, ?, ?, ?, ?)')
      .run(req.user.id, '创建用户', `用户: ${username}, 角色: ${finalRole}`, '成功', nowISO());

    res.json({ id: result.lastInsertRowid, username, role: finalRole });
  } catch (e) {
    res.status(500).json({ error: '创建用户失败' });
  }
});

// 公开注册(客户自助注册,只能注册 user 角色)
router.post('/register-public', rateLimit({ windowMs: 60000, max: 3 }), (req, res) => {
  const { username, password, real_name, phone } = req.body;

  if (!username || !password) {
    return res.status(400).json({ error: '用户名和密码不能为空' });
  }
  if (!/^[a-zA-Z0-9_]{3,20}$/.test(username)) {
    return res.status(400).json({ error: '用户名只能包含字母、数字、下划线，3-20位' });
  }
  if (password.length < 6) {
    return res.status(400).json({ error: '密码至少6位' });
  }

  const db = getDb();
  const existing = db.prepare('SELECT id FROM users WHERE username = ?').get(username);
  if (existing) {
    return res.status(409).json({ error: '用户名已存在' });
  }

  const hash = bcrypt.hashSync(password, 10);
  try {
    const result = db.prepare(
      'INSERT INTO users (username, password_hash, role, real_name, phone, created_at) VALUES (?, ?, ?, ?, ?, ?)'
    ).run(username, hash, 'user', real_name || '', phone || '', nowISO());

    db.prepare('INSERT INTO operation_logs (user_id, action, detail, result, created_at) VALUES (?, ?, ?, ?, ?)')
      .run(result.lastInsertRowid, '用户注册', `新用户注册: ${username}`, '成功', nowISO());

    res.json({ message: '注册成功', username });
  } catch (e) {
    res.status(500).json({ error: '注册失败' });
  }
});

// 当前用户信息
router.get('/me', authMiddleware, (req, res) => {
  const db = getDb();
  const user = db.prepare('SELECT id, username, role, real_name, phone, created_at, last_login FROM users WHERE id = ?').get(req.user.id);
  if (!user) {
    return res.status(404).json({ error: '用户不存在' });
  }
  res.json(user);
});

// 修改自己的密码
router.put('/password', authMiddleware, (req, res) => {
  const { old_password, new_password } = req.body;
  if (!old_password || !new_password) {
    return res.status(400).json({ error: '请输入旧密码和新密码' });
  }
  if (new_password.length < 6) {
    return res.status(400).json({ error: '新密码至少6位' });
  }

  const db = getDb();
  const user = db.prepare('SELECT password_hash FROM users WHERE id = ?').get(req.user.id);
  if (!bcrypt.compareSync(old_password, user.password_hash)) {
    return res.status(401).json({ error: '旧密码错误' });
  }

  const hash = bcrypt.hashSync(new_password, 10);
  db.prepare('UPDATE users SET password_hash = ?, must_change_password = 0 WHERE id = ?').run(hash, req.user.id);
  res.json({ message: '密码修改成功' });
});

// 用户列表(仅 admin)
router.get('/users', authMiddleware, adminOnly, (req, res) => {
  const db = getDb();
  const users = db.prepare('SELECT id, username, role, real_name, phone, created_at, last_login, enabled FROM users ORDER BY id').all();
  res.json(users);
});

// 修改用户角色(仅 admin,且不能修改自己的角色)
router.put('/users/:id/role', authMiddleware, adminOnly, (req, res) => {
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
  db.prepare('INSERT INTO operation_logs (user_id, action, detail, result, created_at) VALUES (?, ?, ?, ?, ?)')
    .run(req.user.id, '修改用户角色', `用户: ${user.username}, ${user.role} -> ${role}`, '成功', nowISO());
  res.json({ message: '角色已更新' });
});

// 重置用户密码(仅 admin)
router.put('/users/:id/reset-password', authMiddleware, adminOnly, (req, res) => {
  const id = parseInt(req.params.id);
  const { new_password } = req.body;
  const pwd = new_password && new_password.length >= 6 ? new_password : '123456';

  const db = getDb();
  const user = db.prepare('SELECT username FROM users WHERE id = ?').get(id);
  if (!user) {
    return res.status(404).json({ error: '用户不存在' });
  }
  const hash = bcrypt.hashSync(pwd, 10);
  db.prepare('UPDATE users SET password_hash = ?, must_change_password = 1 WHERE id = ?').run(hash, id);
  db.prepare('INSERT INTO operation_logs (user_id, action, detail, result, created_at) VALUES (?, ?, ?, ?, ?)')
    .run(req.user.id, '重置用户密码', `用户: ${user.username}`, '成功', nowISO());
  res.json({ message: '密码已重置', default_password: pwd === '123456' ? '123456' : undefined });
});

// 启用/禁用用户(仅 admin)
router.put('/users/:id/toggle', authMiddleware, adminOnly, (req, res) => {
  const id = parseInt(req.params.id);
  if (id === req.user.id) {
    return res.status(400).json({ error: '不能禁用自己' });
  }

  const db = getDb();
  const user = db.prepare('SELECT username, enabled FROM users WHERE id = ?').get(id);
  if (!user) return res.status(404).json({ error: '用户不存在' });

  const newEnabled = user.enabled ? 0 : 1;
  db.prepare('UPDATE users SET enabled = ? WHERE id = ?').run(newEnabled, id);
  db.prepare('INSERT INTO operation_logs (user_id, action, detail, result, created_at) VALUES (?, ?, ?, ?, ?)')
    .run(req.user.id, newEnabled ? '启用用户' : '禁用用户', `用户: ${user.username}`, '成功', nowISO());
  res.json({ enabled: newEnabled });
});

// 删除用户(仅 admin,不能删自己)
router.delete('/users/:id', authMiddleware, adminOnly, (req, res) => {
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
  db.prepare('INSERT INTO operation_logs (user_id, action, detail, result, created_at) VALUES (?, ?, ?, ?, ?)')
    .run(req.user.id, '删除用户', `用户: ${user.username}`, '成功', nowISO());
  res.json({ message: '删除成功' });
});

module.exports = { router, authMiddleware, roleMiddleware, adminOnly, JWT_SECRET };
