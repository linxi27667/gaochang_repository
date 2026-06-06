const express = require('express');
const http = require('http');
const { WebSocketServer } = require('ws');
const path = require('path');
const cors = require('cors');
require('dotenv').config();
const { init: initDb, getDb } = require('./database');
const { connect: mqttConnect, addWsClient, getStatus: getMqttStatus } = require('./mqtt-bridge');
const { router: authRouter, authMiddleware } = require('./auth');
const devicesRouter = require('./devices');
const commandsRouter = require('./commands');
const logsRouter = require('./logs');
const alarmsRouter = require('./alarms');
const maintenanceRouter = require('./maintenance');

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

app.use('/api/auth', authRouter);
app.use('/api/devices', devicesRouter);
app.use('/api/commands', commandsRouter);
app.use('/api/logs', logsRouter);
app.use('/api/alarms', alarmsRouter);
app.use('/api/maintenance', maintenanceRouter);

app.get('/api/mqtt-status', (req, res) => {
  res.json(getMqttStatus());
});

app.get('/api/stats/runtime', (req, res) => {
  const db = getDb();
  const last24h = db.prepare(`
    SELECT device_id,
           SUM(run_count) AS daily_run_count,
           SUM(run_time_s) AS daily_run_time_s
    FROM device_status
    GROUP BY device_id
  `).all();
  res.json(last24h);
});

// AI Chat proxy to MiMo API (avoids CORS issues from browser)
app.post('/api/ai/chat', authMiddleware, async (req, res) => {
  try {
    const { messages } = req.body;
    if (!messages || !Array.isArray(messages)) {
      return res.status(400).json({ error: 'messages required' });
    }
    if (!MIMO_API_KEY) {
      return res.status(503).json({ error: 'AI服务未配置 MIMO_API_KEY' });
    }

    const systemPrompt = '你是高昌举升机（GaoChang Lift）的专业AI助手。你精通：1.举升机的操作规程和安全标准 2.常见故障码和故障排查方法 3.维修保养流程和周期 4.IoT物联网平台的设备管理 5.举升机的锁机/解锁/远程控制。回答应当专业、简洁、实用。';

    // 只允许 MiMo/OpenAI 兼容接口支持的角色，避免前端 UI 角色污染请求。
    const cleanMessages = [{ role: 'system', content: systemPrompt }];
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

wss.on('connection', (ws) => {
  console.log('[WS] Client connected');
  addWsClient(ws);

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

const MQTT_BROKER = process.env.MQTT_BROKER || 'mqtt://8.134.167.240:1883';
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
