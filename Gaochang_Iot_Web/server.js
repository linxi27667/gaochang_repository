const express = require('express');
const http = require('http');
const { WebSocketServer } = require('ws');
const path = require('path');
const cors = require('cors');
const jwt = require('jsonwebtoken');
require('dotenv').config();
const { init: initDb, getDb } = require('./src/database');
const { connect: mqttConnect, addWsClient, getStatus: getMqttStatus } = require('./src/mqtt-bridge');
const { router: authRouter, authMiddleware, roleMiddleware, JWT_SECRET } = require('./src/routes/auth');
const devicesRouter = require('./src/routes/devices');
const commandsRouter = require('./src/routes/commands');
const logsRouter = require('./src/routes/logs');
const alarmsRouter = require('./src/routes/alarms');
const maintenanceRouter = require('./src/routes/maintenance');
const bindingRouter = require('./src/routes/binding');
const adminRouter = require('./src/routes/admin');
const deviceOpsRouter = require('./src/routes/device_ops');
const { getPlatformSnapshot, buildAiContextText } = require('./src/ai-context');

const app = express();
const server = http.createServer(app);
const PORT = process.env.PORT || 3000;

const MIMO_API_BASE = process.env.MIMO_API_BASE || 'https://token-plan-cn.xiaomimimo.com/v1';
const MIMO_API_URL = process.env.MIMO_API_URL || `${MIMO_API_BASE.replace(/\/+$/, '')}/chat/completions`;
const MIMO_API_KEY = process.env.MIMO_API_KEY || '';
const MIMO_MODEL = process.env.MIMO_MODEL || 'mimo-v2.5-pro';

app.use(cors());
app.use(express.json());
app.use(express.static(path.join(__dirname, 'public')));
app.get('/vendor/chart.umd.js', (req, res) => {
  res.sendFile(path.join(__dirname, 'node_modules', 'chart.js', 'dist', 'chart.umd.js'));
});

app.use('/api/auth', authRouter);
app.use('/api/devices', devicesRouter);
app.use('/api/commands', commandsRouter);
app.use('/api/logs', logsRouter);
app.use('/api/alarms', alarmsRouter);
app.use('/api/maintenance', maintenanceRouter);
app.use('/api/binding', bindingRouter);
app.use('/api/admin', adminRouter);
app.use('/api/device-ops', deviceOpsRouter);

// 管理后台页面
app.get('/admin', (req, res) => {
  res.sendFile(path.join(__dirname, 'public', 'admin.html'));
});

app.get('/api/mqtt-status', authMiddleware, (req, res) => {
  res.json(getMqttStatus());
});

app.get('/api/stats/runtime', authMiddleware, (req, res) => {
  const db = getDb();
  const isAdmin = req.user && req.user.role === 'admin';
  // 基于 device_bindings 多对多绑定鉴权,替代已废弃的 owner_id
  const where = isAdmin ? '' : `WHERE EXISTS (
    SELECT 1 FROM device_bindings b
    WHERE b.device_id = s.device_id AND b.user_id = ? AND b.status = 'active'
  )`;
  const params = isAdmin ? [] : [req.user.id];
  const last24h = db.prepare(`
    SELECT s.device_id,
           SUM(run_count) AS daily_run_count,
           SUM(run_time_s) AS daily_run_time_s
    FROM device_status s
    ${where}
    GROUP BY s.device_id
  `).all(...params);
  res.json(last24h);
});

app.get('/api/ai/context', authMiddleware, (req, res) => {
  res.json(getPlatformSnapshot(req.user));
});

// AI Chat proxy to MiMo API (avoids CORS issues from browser)
app.post('/api/ai/chat', authMiddleware, async (req, res) => {
  try {
    const { messages, lang } = req.body;
    if (!messages || !Array.isArray(messages)) {
      return res.status(400).json({ error: 'messages required' });
    }
    if (!MIMO_API_KEY) {
      return res.status(503).json({ error: 'AI服务未配置 MIMO_API_KEY' });
    }

    const answerLang = typeof lang === 'string' && lang.trim() ? lang.trim() : 'zh';
    const systemPrompt = [
      '你是高昌举升机（GaoChang Lift）的 IoT 数据分析助手。',
      '你精通举升机安全标准、故障排查、维修保养、MQTT 物联网平台和设备运行数据分析。',
      '你能读取本系统注入的实时平台数据快照，并据此分析在线状态、报警、锁机、左右高度偏差、运行次数、运行时长、最近命令、保养记录和操作日志。',
      '回答要专业、简洁、可执行。涉及安全风险时优先提示停机检查和现场确认。',
      `请使用用户界面当前语言回答，语言代码：${answerLang}。`
    ].join('\n');
    const platformContext = buildAiContextText(req.user);

    // 只允许 MiMo/OpenAI 兼容接口支持的角色，避免前端 UI 角色污染请求。
    const cleanMessages = [
      { role: 'system', content: systemPrompt },
      { role: 'system', content: platformContext }
    ];
    for (const m of messages) {
      if (m && m.role && m.content && typeof m.content === 'string' && m.content.trim()) {
        const role = m.role === 'bot' ? 'assistant' : m.role;
        if (role === 'system' || role === 'user' || role === 'assistant') {
          cleanMessages.push({ role, content: m.content.trim().slice(0, 2000) });
        }
      }
    }

    const body = {
      model: MIMO_MODEL,
      messages: cleanMessages,
      temperature: 0.7
    };

    console.log('[AI] Sending', cleanMessages.length, 'messages to MiMo API');

    const response = await fetch(MIMO_API_URL, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'Authorization': `Bearer ${MIMO_API_KEY}`
      },
      body: JSON.stringify(body)
    });

    const rawText = await response.text();
    let data = {};
    try {
      data = rawText ? JSON.parse(rawText) : {};
    } catch (parseErr) {
      console.error('[AI] Non-JSON response:', rawText.slice(0, 300));
      return res.status(502).json({ error: 'AI服务返回格式异常' });
    }

    if (!response.ok) {
      console.error('[AI] API error:', response.status, JSON.stringify(data));
      return res.status(response.status).json({ error: data.error?.message || data.message || `AI API error ${response.status}` });
    }

    // MiMo puts reasoning in reasoning_content, actual reply in content
    // If content is empty, fall back to reasoning_content
    const msg = data.choices?.[0]?.message;
    if (msg) {
      if (!msg.content && msg.reasoning_content) {
        msg.content = msg.reasoning_content;
      }
      delete msg.reasoning_content;
    }

    res.json(data);
  } catch (err) {
    console.error('[AI] Error:', err.message);
    res.status(500).json({ error: 'AI服务暂时不可用，请稍后重试' });
  }
});

const wss = new WebSocketServer({ server, path: '/ws' });

wss.on('connection', (ws, req) => {
  const url = new URL(req.url, `http://${req.headers.host || 'localhost'}`);
  const token = url.searchParams.get('token') || '';
  let decoded;
  try {
    decoded = jwt.verify(token, JWT_SECRET);
  } catch (e) {
    ws.close(1008, 'unauthorized');
    return;
  }

  // 实时查数据库校验 auth_version 和 enabled,与 authMiddleware 保持一致
  // 防止禁用/降级/重置密码后旧 JWT 通过已建立的 WebSocket 连接继续接收数据
  const db = getDb();
  const userRow = db.prepare('SELECT id, username, role, auth_version, enabled FROM users WHERE id = ?').get(decoded.id);
  if (!userRow || !userRow.enabled) {
    ws.close(1008, 'account disabled or not found');
    return;
  }
  if ((decoded.auth_version || 0) !== (userRow.auth_version || 0)) {
    ws.close(1008, 'credentials expired, please re-login');
    return;
  }

  // 使用数据库最新角色,避免 JWT 中的旧角色继续生效
  const user = {
    id: userRow.id,
    username: userRow.username,
    role: userRow.role,
    auth_version: userRow.auth_version || 0
  };

  console.log(`[WS] Client connected: ${user.username} (role=${user.role})`);
  addWsClient(ws, user);

  ws.send(JSON.stringify({ type: 'connected', timestamp: Date.now() }));

  ws.on('message', (msg) => {
    try {
      const data = JSON.parse(msg.toString());
      if (data.type === 'ping') {
        ws.send(JSON.stringify({ type: 'pong', timestamp: Date.now() }));
      }
    } catch (e) { /* ignore */ }
  });

  ws.on('close', () => console.log('[WS] Client disconnected'));
});

initDb();

const MQTT_BROKER = process.env.MQTT_BROKER || 'mqtt://127.0.0.1:1883';
const MQTT_OPTIONS = {
  topicPrefix: process.env.MQTT_TOPIC_PREFIX || 'gaochang/lift',
  gatewayId: process.env.MQTT_GATEWAY_ID || 'f407zet6',
  deviceId: process.env.MQTT_DEVICE_ID || 'gaochang_lift_f407zet6',
  username: process.env.MQTT_USERNAME || '',
  password: process.env.MQTT_PASSWORD || ''
};

const mqttOpts = { ...MQTT_OPTIONS };
if (mqttOpts.username) mqttOpts.username = mqttOpts.username;
if (mqttOpts.password) mqttOpts.password = mqttOpts.password;

console.log(`[Server] Starting lift monitor server on port ${PORT}`);
console.log(`[Server] MQTT broker: ${MQTT_BROKER}`);

mqttConnect(MQTT_BROKER, mqttOpts);

server.listen(PORT, () => {
  console.log(`[Server] Running at http://localhost:${PORT}`);
});
