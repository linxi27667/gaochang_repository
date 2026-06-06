const mqtt = require('mqtt');
const { getDb } = require('./database');

function nowISO(date) {
  return (date || new Date()).toISOString().replace('T', ' ').substring(0, 19);
}

let mqttClient = null;
let topicPrefix = process.env.MQTT_TOPIC_PREFIX || 'gaochang/lift';
let defaultGatewayId = process.env.MQTT_GATEWAY_ID || 'f407zet6';
let defaultDeviceId = process.env.MQTT_DEVICE_ID || 'gaochang_lift_f407zet6';
const wssClients = new Set();

const OFFLINE_TIMEOUT_MS = 30000;   // 30s 未收到遥测则标记离线
const CHECK_INTERVAL_MS = 5000;     // 每 5 秒检查一次
const CMD_TIMEOUT_MS = 10000;       // 命令 10s 未响应则超时

let offlineCheckTimer = null;
let cmdTimeoutTimer = null;

const ALARM_MESSAGES = {
  collision: '碰撞报警',
  stall: '失速报警',
  balance_timeout: '平衡超时报警',
  safety_bar: '安全杆触发',
  overheight: '超高报警',
  Emergency: '急停触发'
};

const EVENT_RESULT_MAP = {
  pong: { cmd: 'ping', result: 'pong' },
  report_ok: { cmd: 'get_status', result: 'reported' },
  lock_ok: { cmd: 'lock', result: 'locked' },
  unlock_ok: { cmd: 'unlock', result: 'unlocked' },
  admin_enter_ok: { cmd: 'admin_enter', result: 'admin_entered' },
  admin_enter_denied: { cmd: 'admin_enter', result: 'admin_denied' },
  admin_exit_ok: { cmd: 'admin_exit', result: 'admin_exited' },
  fault_clear_ok: { cmd: 'fault_clear', result: 'fault_cleared' },
  fault_clear_denied: { cmd: 'fault_clear', result: 'fault_clear_denied' },
  admin_jog_ok: { cmd: 'admin_jog', result: 'admin_jog_ok' },
  admin_jog_denied: { cmd: 'admin_jog', result: 'admin_jog_denied' },
  maintenance_done_ok: { cmd: 'maintenance_done', result: 'maintenance_done' },
  reboot_dtu: { cmd: 'reboot_dtu', result: 'rebooting' }
};

function connect(brokerUrl, options = {}) {
  if (mqttClient) {
    mqttClient.end();
  }

  topicPrefix = options.topicPrefix || process.env.MQTT_TOPIC_PREFIX || 'gaochang/lift';
  defaultGatewayId = options.gatewayId || process.env.MQTT_GATEWAY_ID || defaultGatewayId;
  defaultDeviceId = options.deviceId || process.env.MQTT_DEVICE_ID || defaultDeviceId;

  const connOptions = {
    clientId: 'lift-server-' + Date.now().toString(36),
    clean: true,
    reconnectPeriod: 5000,
    username: options.username || undefined,
    password: options.password || undefined
  };

  console.log(`[MQTT] Connecting to ${brokerUrl}...`);
  mqttClient = mqtt.connect(brokerUrl, connOptions);

  mqttClient.on('connect', () => {
    console.log('[MQTT] Connected');
    mqttClient.subscribe(`${topicPrefix}/+/telemetry`);
    mqttClient.subscribe(`${topicPrefix}/+/status`);
    mqttClient.subscribe(`${topicPrefix}/+/response`);
    mqttClient.subscribe(`${topicPrefix}/alarm/+`);
    console.log(`[MQTT] Subscribed: ${topicPrefix}/+/telemetry, ${topicPrefix}/+/status, ${topicPrefix}/+/response, ${topicPrefix}/alarm/+`);
  });

  mqttClient.on('message', (topic, message) => {
    const payload = message.toString();
    try {
      const messages = parseMqttPayload(payload);
      for (const data of messages) {
        handleMessage(topic, data);
      }
    } catch (e) {
      console.warn('[MQTT] Parse error:', e.message, 'payload=', payload.slice(0, 160));
    }
  });

  mqttClient.on('error', (err) => {
    console.error('[MQTT] Error:', err.message);
  });

  mqttClient.on('reconnect', () => {
    console.log('[MQTT] Reconnecting...');
  });

  mqttClient.on('close', () => {
    console.log('[MQTT] Disconnected');
  });

  startOfflineCheck();
  startCommandTimeoutCheck();

  return mqttClient;
}

function parseMqttPayload(payload) {
  const text = String(payload || '').trim();
  const messages = [];

  if (!text) return messages;

  try {
    return [JSON.parse(text)];
  } catch (e) {
    // TAS DTU 会把连续串口 JSON 合并成一个 MQTT payload，用换行或大括号边界拆包。
  }

  for (const line of text.split(/\r?\n/)) {
    const item = line.trim();
    if (!item) continue;
    try {
      messages.push(JSON.parse(item));
    } catch (e) {
      for (const objText of splitJsonObjects(item)) {
        messages.push(JSON.parse(objText));
      }
    }
  }

  return messages;
}

function splitJsonObjects(text) {
  const objects = [];
  let depth = 0;
  let start = -1;
  let inString = false;
  let escaped = false;

  for (let i = 0; i < text.length; i++) {
    const ch = text[i];

    if (inString) {
      if (escaped) escaped = false;
      else if (ch === '\\') escaped = true;
      else if (ch === '"') inString = false;
      continue;
    }

    if (ch === '"') {
      inString = true;
    } else if (ch === '{') {
      if (depth === 0) start = i;
      depth++;
    } else if (ch === '}') {
      depth--;
      if (depth === 0 && start >= 0) {
        objects.push(text.slice(start, i + 1));
        start = -1;
      }
    }
  }

  if (depth !== 0 || objects.length === 0) {
    throw new Error('invalid concatenated JSON payload');
  }

  return objects;
}

function handleMessage(topic, data) {
  const parts = topic.split('/');
  const prefixParts = topicPrefix.split('/');
  const tail = parts.slice(prefixParts.length);
  const gatewayId = tail[0];
  const topicType = tail[1];
  const deviceId = data.device || data.device_id || defaultDeviceId || gatewayId;

  if (topicType === 'telemetry' || topicType === 'status') {
    handleStatusUpdate(deviceId, normalizeTelemetry(data));
    if (data.event || data.result || data.msg_id) {
      handleCommandResponse(deviceId, data);
    }
  } else if (topicType === 'response') {
    handleCommandResponse(deviceId, data);
  } else if (topic.startsWith(`${topicPrefix}/alarm/`)) {
    handleAlarm(parts[parts.length - 1] || parts[2], data);
  }
}

function normalizeTelemetry(data) {
  const height = data.height || {};
  const runtime = data.runtime || {};
  const safety = data.safety || {};
  const dtu = data.dtu || {};
  const alarm = data.alarm || safety.alarm || 'none';
  const totalRunMs = Number(runtime.total_ms || 0);
  const currentRunMs = Number(runtime.current_ms || 0);
  const effectiveRunTimeS = Math.floor((totalRunMs + currentRunMs) / 1000);

  return {
    ...data,
    online: true,
    state: data.state || data.direction || 'idle',
    alarm,
    name: data.name,
    model: data.model,
    group: data.group,
    locked: !!data.locked,
    height_left_mm: data.height_left_mm ?? height.left_mm ?? 0,
    height_right_mm: data.height_right_mm ?? height.right_mm ?? 0,
    height_diff_mm: data.height_diff_mm ?? height.diff_mm ?? 0,
    run_count: data.run_count ?? runtime.run_count ?? 0,
    run_time_s: data.run_time_s ?? effectiveRunTimeS,
    ts_ms: data.ts_ms ?? data.tick ?? Date.now(),
    dtu_state: dtu.state,
    csq: dtu.csq
  };
}

function handleStatusUpdate(deviceId, data) {
  const db = getDb();

  let status = 'normal';
  if (!data.online) status = 'offline';
  else if (data.locked) status = 'locked';
  else if (data.alarm && data.alarm !== 'none') status = 'fault';

  const device = db.prepare('SELECT device_id FROM devices WHERE device_id = ?').get(deviceId);

  if (!device) {
    db.prepare('INSERT OR IGNORE INTO devices (device_id, name, model, group_name, created_at) VALUES (?, ?, ?, ?, ?)')
      .run(deviceId, data.name || `举升机${deviceId}`, data.model || 'TL-5000', data.group || '默认分组', nowISO());
  } else if (data.name) {
    db.prepare('UPDATE devices SET name = ? WHERE device_id = ?').run(data.name, deviceId);
  }

  db.prepare(`
    INSERT INTO device_status (device_id, online, locked, state, alarm,
      height_left_mm, height_right_mm, height_diff_mm, run_count, run_time_s, ts_ms, updated_at)
    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    ON CONFLICT(device_id) DO UPDATE SET
      online=excluded.online, locked=excluded.locked, state=excluded.state, alarm=excluded.alarm,
      height_left_mm=excluded.height_left_mm, height_right_mm=excluded.height_right_mm,
      height_diff_mm=excluded.height_diff_mm, run_count=excluded.run_count,
      run_time_s=excluded.run_time_s, ts_ms=excluded.ts_ms,
      updated_at=excluded.updated_at
  `).run(
    deviceId,
    data.online ? 1 : 0,
    data.locked ? 1 : 0,
    data.state || 'idle',
    data.alarm || 'none',
    data.height_left_mm || 0,
    data.height_right_mm || 0,
    data.height_diff_mm || 0,
    data.run_count || 0,
    data.run_time_s || 0,
    data.ts_ms || Date.now(),
    nowISO()
  );

  if (data.alarm && data.alarm !== 'none') {
    const existing = db.prepare(
      'SELECT id FROM alarms WHERE device_id = ? AND alarm_type = ? AND acknowledged = 0'
    ).get(deviceId, data.alarm);

    if (!existing) {
      db.prepare(
        'INSERT INTO alarms (device_id, alarm_type, message, level, created_at) VALUES (?, ?, ?, ?, ?)'
      ).run(deviceId, data.alarm, ALARM_MESSAGES[data.alarm] || data.alarm, 'warning', nowISO());
    }
  }

  broadcastToClients({
    type: 'device_status',
    device_id: deviceId,
    data: {
      ...data,
      status
    }
  });
}

function handleCommandResponse(deviceId, data) {
  const db = getDb();
  const response = normalizeCommandResponse(data);

  if (response.msg_id) {
    db.prepare(
      "UPDATE command_queue SET status = 'responded', result = ?, responded_at = ? WHERE msg_id = ?"
    ).run(response.result || 'ok', nowISO(), response.msg_id);
  }

  // If device confirmed a rename, update database
  if (response.cmd === 'rename' && response.result === 'renamed' && response.name) {
    db.prepare('UPDATE devices SET name = ? WHERE device_id = ?').run(response.name, deviceId);
  }

  broadcastToClients({
    type: 'command_response',
    device_id: deviceId,
    data: response
  });
}

function normalizeCommandResponse(data) {
  const response = { ...data };
  const mapped = response.event ? EVENT_RESULT_MAP[response.event] : null;

  if (!response.cmd && mapped) {
    response.cmd = mapped.cmd;
  }

  if (!response.result && mapped) {
    response.result = mapped.result;
  }

  return response;
}

function handleAlarm(deviceId, data) {
  const db = getDb();

  db.prepare(
    'INSERT INTO alarms (device_id, alarm_type, message, level, created_at) VALUES (?, ?, ?, ?, ?)'
  ).run(deviceId, data.type || data.alarm_type || 'unknown', data.message || '', data.level || 'warning', nowISO());

  broadcastToClients({
    type: 'alarm',
    device_id: deviceId,
    data
  });
}

function commandTopicFor(deviceId) {
  const gatewayId = deviceId === defaultDeviceId ? defaultGatewayId : deviceId;
  return `${topicPrefix}/${gatewayId}/command`;
}

function normalizeCommand(cmd) {
  if (cmd === 'query') return 'get_status';
  return cmd;
}

function sendCommand(deviceId, cmd, msgId, extra = {}) {
  if (!mqttClient || !mqttClient.connected) {
    console.warn('[MQTT] Not connected, cannot send command');
    return false;
  }

  const wireCmd = normalizeCommand(cmd);
  const topic = commandTopicFor(deviceId);
  const payload = JSON.stringify({
    type: 'command',
    device: deviceId,
    cmd: wireCmd,
    command: wireCmd,
    msg_id: msgId,
    ...extra
  });
  mqttClient.publish(topic, payload);
  console.log(`[MQTT] TX ${topic}: ${payload}`);
  return true;
}

function broadcastToClients(message) {
  const data = JSON.stringify(message);
  for (const ws of wssClients) {
    try {
      if (ws.readyState === 1) {
        ws.send(data);
      }
    } catch (e) {
      wssClients.delete(ws);
    }
  }
}

function addWsClient(ws) {
  wssClients.add(ws);
  ws.on('close', () => wssClients.delete(ws));
  ws.on('error', () => wssClients.delete(ws));
}

function isConnected() {
  return mqttClient && mqttClient.connected;
}

function getStatus() {
  return {
    connected: isConnected(),
    prefix: topicPrefix,
    gateway_id: defaultGatewayId,
    device_id: defaultDeviceId,
    telemetry_topic: `${topicPrefix}/${defaultGatewayId}/telemetry`,
    command_topic: `${topicPrefix}/${defaultGatewayId}/command`,
    status_topic: `${topicPrefix}/${defaultGatewayId}/status`
  };
}

function startOfflineCheck() {
  if (offlineCheckTimer) clearInterval(offlineCheckTimer);
  offlineCheckTimer = setInterval(() => {
    const db = getDb();
    const threshold = nowISO(new Date(Date.now() - OFFLINE_TIMEOUT_MS));
    const stale = db.prepare(
      'SELECT device_id FROM device_status WHERE online = 1 AND updated_at < ?'
    ).all(threshold);

    for (const row of stale) {
      db.prepare('UPDATE device_status SET online = 0 WHERE device_id = ?').run(row.device_id);
      broadcastToClients({
        type: 'device_status',
        device_id: row.device_id,
        data: { online: false, state: 'offline' }
      });
      console.log(`[MQTT] Device ${row.device_id} marked offline (no telemetry for ${OFFLINE_TIMEOUT_MS / 1000}s)`);
    }
  }, CHECK_INTERVAL_MS);
}

function startCommandTimeoutCheck() {
  if (cmdTimeoutTimer) clearInterval(cmdTimeoutTimer);
  cmdTimeoutTimer = setInterval(() => {
    const db = getDb();
    const threshold = nowISO(new Date(Date.now() - CMD_TIMEOUT_MS));
    const stale = db.prepare(
      "SELECT msg_id, device_id, cmd FROM command_queue WHERE status = 'sent' AND created_at < ?"
    ).all(threshold);

    for (const row of stale) {
      db.prepare("UPDATE command_queue SET status = 'timeout' WHERE msg_id = ?").run(row.msg_id);
      console.log(`[CMD] ${row.cmd} to ${row.device_id} (msg_id=${row.msg_id}) timed out`);
      broadcastToClients({
        type: 'command_response',
        device_id: row.device_id,
        data: { cmd: row.cmd, msg_id: row.msg_id, result: 'timeout' }
      });
    }
  }, CHECK_INTERVAL_MS);
}

module.exports = { connect, sendCommand, addWsClient, broadcastToClients, isConnected, getStatus };
