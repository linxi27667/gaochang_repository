const express = require('express');
const bcrypt = require('bcryptjs');
const jwt = require('jsonwebtoken');
const { getDb } = require('./database');
const { nowISO } = require('./utils');
const { rateLimit } = require('./rateLimit');

const router = express.Router();
const JWT_SECRET = process.env.JWT_SECRET || 'lift-monitor-secret-key-2024';
const JWT_EXPIRES_IN = '24h';

function generateToken(user) {
  return jwt.sign(
    { id: user.id, username: user.username, role: user.role },
    JWT_SECRET,
    { expiresIn: JWT_EXPIRES_IN }
  );
}

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

function roleMiddleware(...roles) {
  return (req, res, next) => {
    if (!roles.includes(req.user.role)) {
      return res.status(403).json({ error: '权限不足' });
    }
    next();
  };
}

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
      real_name: user.real_name
    }
  });
});

router.post('/register', authMiddleware, roleMiddleware('admin'), (req, res) => {
  const { username, password, role, real_name, phone } = req.body;
  if (!username || !password) {
    return res.status(400).json({ error: '用户名和密码不能为空' });
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
      'INSERT INTO users (username, password_hash, role, real_name, phone) VALUES (?, ?, ?, ?, ?)'
    ).run(username, hash, role || 'operator', real_name || '', phone || '');
    res.json({ id: result.lastInsertRowid, username, role: role || 'operator' });
  } catch (e) {
    res.status(500).json({ error: '创建用户失败' });
  }
});

// 公开注册 - 无需登录，带IP限流
router.post('/register-public', rateLimit({ windowMs: 60000, max: 3 }), (req, res) => {
  const { username, password, real_name, phone, captcha_answer, captcha_expected } = req.body;

  if (!username || !password) {
    return res.status(400).json({ error: '用户名和密码不能为空' });
  }
  if (!/^[a-zA-Z0-9_]{3,20}$/.test(username)) {
    return res.status(400).json({ error: '用户名只能包含字母、数字、下划线，3-20位' });
  }
  if (password.length < 6) {
    return res.status(400).json({ error: '密码至少6位' });
  }
  if (Number(captcha_answer) !== Number(captcha_expected)) {
    return res.status(400).json({ error: '验证码错误' });
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
    ).run(username, hash, 'viewer', real_name || '', phone || '', nowISO());

    db.prepare('INSERT INTO operation_logs (user_id, action, detail, result, created_at) VALUES (?, ?, ?, ?, ?)')
      .run(result.lastInsertRowid, '用户注册', `新用户注册: ${username}`, '成功', nowISO());

    res.json({ message: '注册成功', username });
  } catch (e) {
    res.status(500).json({ error: '注册失败' });
  }
});

router.get('/me', authMiddleware, (req, res) => {
  const db = getDb();
  const user = db.prepare('SELECT id, username, role, real_name, phone, created_at, last_login FROM users WHERE id = ?').get(req.user.id);
  if (!user) {
    return res.status(404).json({ error: '用户不存在' });
  }
  res.json(user);
});

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
  db.prepare('UPDATE users SET password_hash = ? WHERE id = ?').run(hash, req.user.id);
  res.json({ message: '密码修改成功' });
});

router.get('/users', authMiddleware, roleMiddleware('admin'), (req, res) => {
  const db = getDb();
  const users = db.prepare('SELECT id, username, role, real_name, phone, created_at, last_login, enabled FROM users ORDER BY id').all();
  res.json(users);
});

router.delete('/users/:id', authMiddleware, roleMiddleware('admin'), (req, res) => {
  const userId = parseInt(req.params.id);
  if (userId === req.user.id) {
    return res.status(400).json({ error: '不能删除自己' });
  }
  const db = getDb();
  db.prepare('DELETE FROM users WHERE id = ?').run(userId);
  res.json({ message: '删除成功' });
});

router.put('/users/:id/toggle', authMiddleware, roleMiddleware('admin'), (req, res) => {
  const db = getDb();
  const user = db.prepare('SELECT enabled FROM users WHERE id = ?').get(req.params.id);
  if (!user) return res.status(404).json({ error: '用户不存在' });
  const newEnabled = user.enabled ? 0 : 1;
  db.prepare('UPDATE users SET enabled = ? WHERE id = ?').run(newEnabled, req.params.id);
  res.json({ enabled: newEnabled });
});

module.exports = { router, authMiddleware, roleMiddleware, JWT_SECRET };