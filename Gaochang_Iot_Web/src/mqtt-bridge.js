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
const VALID_PRODUCT_TYPES = ['double_post', 'small_scissor', 'thin_scissor', 'large_scissor'];

let offlineCheckTimer = null;
let cmdTimeoutTimer = null;

const ALARM_MESSAGES = {
  collision: '碰撞报警',
  stall: '失速报警',
  balance_timeout: '平衡超时报警',
  safety_bar: '安全杆触发',
  overheight: '超高报警',
  Emergency: '急停触发',
  estop: '急停触发',
  photo_alarm: '光电报警'
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
  reboot_dtu: { cmd: 'reboot_dtu', result: 'rebooting' },
  // 多产品新增命令回执
  clear_alarm_ok: { cmd: 'clear_alarm', result: 'alarm_cleared' },
  clear_alarm_denied: { cmd: 'clear_alarm', result: 'alarm_clear_denied' },
  set_config_ok: { cmd: 'set_config', result: 'config_set' },
  set_config_denied: { cmd: 'set_config', result: 'config_set_denied' },
  set_product_type_ok: { cmd: 'set_product_type', result: 'product_type_set' },
  set_product_type_denied: { cmd: 'set_product_type', result: 'product_type_set_denied' },
  config_report: { cmd: 'get_config', result: 'config_reported' },
  buzzer_on_ok: { cmd: 'buzzer_on', result: 'buzzer_on' },
  buzzer_off_ok: { cmd: 'buzzer_off', result: 'buzzer_off' },
  buzzer_on_denied: { cmd: 'buzzer_on', result: 'buzzer_denied' },
  buzzer_off_denied: { cmd: 'buzzer_off', result: 'buzzer_denied' }
};

function normalizeUid(raw) {
  if (!raw) return '';
  let s = String(raw).trim().toUpperCase();
  if (s.startsWith('0X')) s = s.slice(2);
  return s.replace(/[\s:\-]/g, '');
}

function normalizeSerial(raw) {
  return raw ? String(raw).trim().slice(0, 64) : '';
}

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
    mqttClient.subscribe(`${topicPrefix}/+/op_log`);
    mqttClient.subscribe(`${topicPrefix}/+/event`);
    mqttClient.subscribe(`${topicPrefix}/alarm/+`);
    console.log(`[MQTT] Subscribed: ${topicPrefix}/+/{telemetry,status,response,op_log,event}, ${topicPrefix}/alarm/+`);
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
  const deviceId = resolveInboundDeviceId(data, gatewayId);
  data._gateway_id = gatewayId;

  // 运动中轻量高度帧（type:height）走轻量路径：仅更新前端展示，不写数据库
  if (topicType === 'telemetry' && data.type === 'height') {
    handleHeightUpdate(deviceId, data);
    return;
  }

  if (topicType === 'telemetry' || topicType === 'status') {
    handleStatusUpdate(deviceId, normalizeTelemetry(data));
    if (data.event || data.result || data.msg_id) {
      handleCommandResponse(deviceId, data);
    }
  } else if (topicType === 'response') {
    handleCommandResponse(deviceId, data);
  } else if (topicType === 'op_log') {
    // 设备端操作日志(工人在设备上的物理操作)
    handleOpLog(deviceId, data);
  } else if (topicType === 'event') {
    // 设备事件(开机/关机/配置变更等)
    handleEvent(deviceId, data);
  } else if (topic.startsWith(`${topicPrefix}/alarm/`)) {
    handleAlarm(parts[parts.length - 1] || parts[2], data);
  }
}

function resolveInboundDeviceId(data, gatewayId) {
  const db = getDb();
  const uid = normalizeUid(data.uid || data.chip_uid || data.mcu_uid);
  const serial = normalizeSerial(data.serial || data.sn);
  const reportedDevice = data.device || data.device_id || defaultDeviceId || gatewayId;

  if (uid) {
    const registry = db.prepare('SELECT serial, bound_device_id FROM device_registry WHERE uid = ?').get(uid);
    if (registry) {
      if (registry.bound_device_id) return registry.bound_device_id;
      if (registry.serial) return registry.serial;
    }

    const device = db.prepare('SELECT device_id FROM devices WHERE uid = ?').get(uid);
    if (device && device.device_id) return device.device_id;

    if (serial) {
      const productType = VALID_PRODUCT_TYPES.includes(data.product_type) ? data.product_type : 'double_post';
      db.prepare(`
        INSERT OR IGNORE INTO device_registry
          (serial, uid, product_type, display_name, model, status, bound_device_id, created_at)
        VALUES (?, ?, ?, ?, ?, 'unbound', '', ?)
      `).run(
        serial,
        uid,
        productType,
        typeof data.name === 'string' ? data.name.slice(0, 64) : '',
        typeof data.model === 'string' ? data.model.slice(0, 32) : '',
        nowISO()
      );
      return serial;
    }
  }

  return serial || reportedDevice;
}

// 处理设备端操作日志(工人物理操作,如上升/下降/锁定/补油/急停等)
function handleOpLog(deviceId, data) {
  const db = getDb();
  const now = nowISO();

  // 从 devices 表查 uid 和 serial(设备可能尚未注册,此时用 deviceId 兜底)
  const device = db.prepare('SELECT device_id, uid FROM devices WHERE device_id = ?').get(deviceId);
  const deviceUid = device?.uid || data.uid || '';
  const deviceSerial = data.serial || device?.device_id || deviceId;

  const opType = typeof data.op_type === 'string' ? data.op_type.slice(0, 32) : 'unknown';
  const opResult = typeof data.op_result === 'string' ? data.op_result.slice(0, 16) : 'ok';
  const durationMs = clampInt(data.duration_ms, 0, 2147483647, 0);
  const detail = typeof data.detail === 'string' ? data.detail.slice(0, 512) : '';
  const deviceState = typeof data.device_state === 'string' ? data.device_state.slice(0, 32) : '';
  // occurred_at:设备本地时间(ISO 字符串或 YYYY-MM-DD HH:MM:SS),若无则用服务器当前时间
  const occurredAt = data.occurred_at ? String(data.occurred_at).slice(0, 32) : now;

  try {
    db.prepare(`
      INSERT INTO device_operation_logs
        (device_uid, device_serial, op_type, op_result, duration_ms, detail, device_state, occurred_at, received_at)
      VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
    `).run(deviceUid, deviceSerial, opType, opResult, durationMs, detail, deviceState, occurredAt, now);

    broadcastToClients({
      type: 'device_op_log',
      device_id: deviceId,
      data: {
        device_uid: deviceUid,
        device_serial: deviceSerial,
        op_type: opType,
        op_result: opResult,
        duration_ms: durationMs,
        detail,
        device_state: deviceState,
        occurred_at: occurredAt,
        received_at: now
      }
    });
  } catch (e) {
    console.error('[MQTT] handleOpLog insert error:', e.message);
  }
}

// 处理设备事件(开机/关机/配置变更/旋转开关切换等)
function handleEvent(deviceId, data) {
  const db = getDb();
  const eventType = typeof data.event_type === 'string' ? data.event_type : (data.event || 'unknown');
  const now = nowISO();

  // 旋转开关切换事件:更新 device_status.rotary_switch
  if (eventType === 'rotary_switch' && data.position) {
    const pos = ['main', 'sub'].includes(data.position) ? data.position : 'main';
    db.prepare('UPDATE device_status SET rotary_switch = ? WHERE device_id = ?').run(pos, deviceId);
  }

  // 把事件也作为操作日志记录下来,便于追溯
  const device = db.prepare('SELECT device_id, uid FROM devices WHERE device_id = ?').get(deviceId);
  const deviceUid = device?.uid || data.uid || '';
  const deviceSerial = data.serial || device?.device_id || deviceId;

  try {
    db.prepare(`
      INSERT INTO device_operation_logs
        (device_uid, device_serial, op_type, op_result, duration_ms, detail, device_state, occurred_at, received_at)
      VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
    `).run(
      deviceUid, deviceSerial,
      eventType.slice(0, 32),
      typeof data.result === 'string' ? data.result.slice(0, 16) : 'ok',
      0,
      typeof data.detail === 'string' ? data.detail.slice(0, 512) : JSON.stringify(data).slice(0, 512),
      typeof data.device_state === 'string' ? data.device_state.slice(0, 32) : '',
      data.occurred_at ? String(data.occurred_at).slice(0, 32) : now,
      now
    );
  } catch (e) {
    console.error('[MQTT] handleEvent insert error:', e.message);
  }

  broadcastToClients({
    type: 'device_event',
    device_id: deviceId,
    data: { event_type: eventType, ...data }
  });
}

// 缓存每个设备最后一次完整状态（供 height 帧合并）
const lastFullState = {};

function handleHeightUpdate(deviceId, data) {
  const height = data.height || {};
  const tsMs = clampInt(data.tick, 0, Number.MAX_SAFE_INTEGER, Date.now());
  const leftMm = clampInt(height.left_mm, 0, 5000, 0);
  const rightMm = clampInt(height.right_mm, 0, 5000, 0);
  const merged = {
    ...(lastFullState[deviceId] || {}),
    online: true,
    height_left_mm: leftMm,
    height_right_mm: rightMm,
    height_diff_mm: clampInt(height.diff_mm, -5000, 5000, Math.abs(leftMm - rightMm)),
    left_pulse: clampInt(height.left_pulse, 0, 2147483647, 0),
    right_pulse: clampInt(height.right_pulse, 0, 2147483647, 0),
    ts_ms: tsMs,
    uptime_s: lastFullState[deviceId]?.uptime_s ?? Math.floor(tsMs / 1000)
  };

  broadcastToClients({
    type: 'device_status',
    device_id: deviceId,
    data: merged
  });
}

function saveUnboundDeviceStatus(deviceId, data, registry = null) {
  const db = getDb();
  const now = nowISO();
  const uid = data.uid || '';
  const serial = data.serial || registry?.serial || '';
  const productType = registry?.product_type || data.product_type || 'double_post';
  const statusJson = JSON.stringify(data);

  db.prepare(`
    INSERT INTO unbound_device_status
      (device_id, uid, serial, product_type, gateway_id, online, state, alarm, ts_ms, updated_at, status_json)
    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    ON CONFLICT(device_id) DO UPDATE SET
      uid=excluded.uid,
      serial=excluded.serial,
      product_type=excluded.product_type,
      gateway_id=excluded.gateway_id,
      online=excluded.online,
      state=excluded.state,
      alarm=excluded.alarm,
      ts_ms=excluded.ts_ms,
      updated_at=excluded.updated_at,
      status_json=excluded.status_json
  `).run(
    deviceId,
    uid,
    serial,
    productType,
    data.gateway_id || '',
    data.online ? 1 : 0,
    data.state || 'idle',
    data.alarm || 'none',
    data.ts_ms || Date.now(),
    now,
    statusJson
  );
}

function clampInt(v, min, max, fallback = 0) {
  const n = Number(v);
  if (!Number.isFinite(n)) return fallback;
  return Math.max(min, Math.min(max, Math.round(n)));
}

function sanitizeAlarm(a) {
  const valid = ['none', 'collision', 'stall', 'balance_timeout', 'safety_bar', 'overheight', 'Emergency', 'estop', 'photo_alarm'];
  return valid.includes(a) ? a : 'none';
}

function objectOrEmpty(value) {
  return value && typeof value === 'object' && !Array.isArray(value) ? value : {};
}

function normalizeState(state, direction) {
  const valid = ['idle', 'up', 'down', 'stop', 'fault', 'offline', 'rising', 'dropping', 'locked', 'refilling', 'estop', 'photo_alarm'];
  if (valid.includes(state)) return state;
  return valid.includes(direction) ? direction : 'idle';
}

function directionFromState(state, direction) {
  if (['up', 'down', 'stop'].includes(direction)) return direction;
  if (state === 'rising' || state === 'refilling') return 'up';
  if (state === 'dropping') return 'down';
  return 'stop';
}

function normalizeTelemetry(data) {
  const height = data.height || {};
  const runtime = data.runtime || {};
  const safety = objectOrEmpty(data.safety);
  const dtu = data.dtu || {};
  const counters = objectOrEmpty(data.counters || data.stats);
  const io = objectOrEmpty(data.io);
  const ioInput = objectOrEmpty(data.io_input || io.input);
  const ioOutput = objectOrEmpty(data.io_output || io.output);

  const boolOrZero = (v) => (v === true || v === 1 || v === '1') ? 1 : 0;

  const totalRunMs = clampInt(runtime.total_ms ?? data.total_run_ms, 0, 2147483647, 0);
  const currentRunMs = clampInt(runtime.current_ms, 0, 2147483647, 0);
  const effectiveRunTimeS = Math.floor((totalRunMs + currentRunMs) / 1000);
  const uptimeMs = clampInt(data.uptime_ms ?? data.tick, 0, 2147483647, 0);
  const uptimeS = data.uptime_s != null ? clampInt(data.uptime_s, 0, 2147483647, 0) : Math.floor(uptimeMs / 1000);

  const leftMm = clampInt(data.height_left_mm ?? height.left_mm, 0, 5000, 0);
  const rightMm = clampInt(data.height_right_mm ?? height.right_mm, 0, 5000, 0);
  const diffMm = clampInt(data.height_diff_mm ?? height.diff_mm, -5000, 5000, Math.abs(leftMm - rightMm));

  // 旋转开关位置(大剪主子机切换)
  const rotarySwitch = ['main', 'sub'].includes(data.rotary_switch) ? data.rotary_switch : 'main';

  // IO 状态 JSON(供前端动态渲染各型号的输入输出状态)
  const ioInputJson = Object.keys(ioInput).length > 0 ? JSON.stringify(ioInput) : (typeof data.io_input_json === 'string' ? data.io_input_json.slice(0, 1024) : '{}');
  const ioOutputJson = Object.keys(ioOutput).length > 0 ? JSON.stringify(ioOutput) : (typeof data.io_output_json === 'string' ? data.io_output_json.slice(0, 1024) : '{}');
  const state = normalizeState(data.state, data.direction);
  const alarm = sanitizeAlarm(data.alarm || safety.alarm || (state === 'photo_alarm' ? 'photo_alarm' : (state === 'estop' ? 'estop' : 'none')));

  return {
    online: true,
    state,
    alarm,
    uid: normalizeUid(data.uid || data.chip_uid || data.mcu_uid),
    serial: normalizeSerial(data.serial || data.sn),
    product_type: VALID_PRODUCT_TYPES.includes(data.product_type) ? data.product_type : undefined,
    gateway_id: typeof data._gateway_id === 'string' ? data._gateway_id.slice(0, 64) : '',
    name: typeof data.name === 'string' ? data.name.slice(0, 64) : undefined,
    model: typeof data.model === 'string' ? data.model.slice(0, 32) : undefined,
    group: typeof data.group === 'string' ? data.group.slice(0, 32) : undefined,
    locked: !!data.locked,
    direction: directionFromState(state, data.direction),
    height_left_mm: leftMm,
    height_right_mm: rightMm,
    height_diff_mm: diffMm,
    left_pulse: clampInt(data.left_pulse ?? height.left_pulse, 0, 2147483647, 0),
    right_pulse: clampInt(data.right_pulse ?? height.right_pulse, 0, 2147483647, 0),
    run_count: clampInt(data.run_count ?? runtime.run_count, 0, 2147483647, 0),
    run_time_s: clampInt(data.run_time_s ?? effectiveRunTimeS, 0, 2147483647, 0),
    uptime_s: uptimeS,
    ts_ms: clampInt(data.ts_ms ?? data.tick, 0, Number.MAX_SAFE_INTEGER, Date.now()),
    upper_limit: boolOrZero(data.upper_limit ?? safety.upper ?? safety.upper_limit ?? ioInput.upper_limit ?? ioInput.limit_up),
    lower_limit: boolOrZero(data.lower_limit ?? safety.lower ?? safety.lower_limit ?? ioInput.lower_limit ?? ioInput.limit_down),
    stall: boolOrZero(data.stall ?? safety.stall),
    collision_up: boolOrZero(data.collision_up ?? safety.collision_up),
    collision_down: boolOrZero(data.collision_down ?? safety.collision_down),
    alarm_code: clampInt(data.alarm_code ?? safety.alarm_code, 0, 65535, alarm === 'none' ? 0 : 1),
    left_up_collision: boolOrZero(data.left_up_collision ?? safety.left_up_collision),
    right_up_collision: boolOrZero(data.right_up_collision ?? safety.right_up_collision),
    left_down_collision: boolOrZero(data.left_down_collision ?? safety.left_down_collision),
    right_down_collision: boolOrZero(data.right_down_collision ?? safety.right_down_collision),
    dtu_state: typeof (dtu.state || data.dtu_state) === 'string' ? (dtu.state || data.dtu_state).slice(0, 32) : '',
    csq: clampInt(dtu.csq ?? data.csq, -1, 31, -1),
    buzzer_on: data.buzzer_on ? 1 : 0,
    // 多产品字段
    rotary_switch: rotarySwitch,
    up_count: clampInt(data.up_count ?? counters.up, 0, 2147483647, 0),
    down_count: clampInt(data.down_count ?? counters.down, 0, 2147483647, 0),
    lock_count: clampInt(data.lock_count ?? counters.lock, 0, 2147483647, 0),
    refill_count: clampInt(data.refill_count ?? counters.refill, 0, 2147483647, 0),
    estop_count: clampInt(data.estop_count ?? counters.estop, 0, 2147483647, 0),
    photo_alarm_count: clampInt(data.photo_alarm_count ?? counters.photo_alarm, 0, 2147483647, 0),
    total_run_ms: totalRunMs,
    last_run_at: typeof data.last_run_at === 'string' ? data.last_run_at.slice(0, 32) : (data.last_run_at || null),
    io_input_json: ioInputJson,
    io_output_json: ioOutputJson
  };
}

function handleStatusUpdate(deviceId, data) {
  const db = getDb();

  let status = 'normal';
  if (!data.online) status = 'offline';
  else if (data.locked) status = 'locked';
  else if (data.alarm && data.alarm !== 'none') status = 'fault';

  const registry = data.uid
    ? db.prepare('SELECT serial, product_type, display_name, model, has_encoder, has_buzzer, has_pressure_sensor, has_display, bound_device_id FROM device_registry WHERE uid = ?').get(data.uid)
    : null;
  const device = db.prepare('SELECT device_id FROM devices WHERE device_id = ?').get(deviceId);

  if (!device) {
    saveUnboundDeviceStatus(deviceId, data, registry);
    broadcastToClients({
      type: 'device_status',
      device_id: deviceId,
      data: {
        ...data,
        bind_status: 'unbound',
        status
      }
    });
    lastFullState[deviceId] = data;
    return;
  } else {
    db.prepare(`UPDATE devices SET
      name = COALESCE(NULLIF(?, ''), name),
      model = COALESCE(NULLIF(?, ''), model),
      uid = COALESCE(NULLIF(?, ''), uid),
      gateway_id = COALESCE(NULLIF(?, ''), gateway_id),
      product_type = COALESCE(NULLIF(?, ''), product_type)
      WHERE device_id = ?`)
      .run(data.name || '', data.model || '', data.uid || '', data.gateway_id || '', data.product_type || '', deviceId);
  }

  db.prepare(`
    INSERT INTO device_status (device_id, online, locked, state, alarm,
      height_left_mm, height_right_mm, height_diff_mm, run_count, run_time_s, uptime_s, ts_ms, updated_at,
      direction, upper_limit, lower_limit, stall, collision_up, collision_down, alarm_code, csq, dtu_state,
      left_pulse, right_pulse, left_up_collision, right_up_collision, left_down_collision, right_down_collision,
      buzzer_on,
      rotary_switch, up_count, down_count, lock_count, refill_count, estop_count, photo_alarm_count,
      total_run_ms, last_run_at, io_input_json, io_output_json)
    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?,
            ?, ?, ?, ?, ?, ?, ?, ?, ?,
            ?, ?, ?, ?, ?, ?, ?,
            ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    ON CONFLICT(device_id) DO UPDATE SET
      online=excluded.online, locked=excluded.locked, state=excluded.state, alarm=excluded.alarm,
      height_left_mm=excluded.height_left_mm, height_right_mm=excluded.height_right_mm,
      height_diff_mm=excluded.height_diff_mm, run_count=excluded.run_count,
      run_time_s=excluded.run_time_s, uptime_s=excluded.uptime_s, ts_ms=excluded.ts_ms,
      updated_at=excluded.updated_at,
      direction=excluded.direction, upper_limit=excluded.upper_limit, lower_limit=excluded.lower_limit,
      stall=excluded.stall, collision_up=excluded.collision_up, collision_down=excluded.collision_down,
      alarm_code=excluded.alarm_code, csq=excluded.csq, dtu_state=excluded.dtu_state,
      left_pulse=excluded.left_pulse, right_pulse=excluded.right_pulse,
      left_up_collision=excluded.left_up_collision, right_up_collision=excluded.right_up_collision,
      left_down_collision=excluded.left_down_collision, right_down_collision=excluded.right_down_collision,
      buzzer_on=excluded.buzzer_on,
      rotary_switch=excluded.rotary_switch, up_count=excluded.up_count, down_count=excluded.down_count,
      lock_count=excluded.lock_count, refill_count=excluded.refill_count,
      estop_count=excluded.estop_count, photo_alarm_count=excluded.photo_alarm_count,
      total_run_ms=excluded.total_run_ms, last_run_at=excluded.last_run_at,
      io_input_json=excluded.io_input_json, io_output_json=excluded.io_output_json
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
    data.uptime_s || 0,
    data.ts_ms || Date.now(),
    nowISO(),
    data.direction || 'stop',
    data.upper_limit || 0,
    data.lower_limit || 0,
    data.stall || 0,
    data.collision_up || 0,
    data.collision_down || 0,
    data.alarm_code || 0,
    data.csq ?? -1,
    data.dtu_state || '',
    data.left_pulse || 0,
    data.right_pulse || 0,
    data.left_up_collision || 0,
    data.right_up_collision || 0,
    data.left_down_collision || 0,
    data.right_down_collision || 0,
    data.buzzer_on || 0,
    data.rotary_switch || 'main',
    data.up_count || 0,
    data.down_count || 0,
    data.lock_count || 0,
    data.refill_count || 0,
    data.estop_count || 0,
    data.photo_alarm_count || 0,
    data.total_run_ms || 0,
    data.last_run_at || null,
    data.io_input_json || '{}',
    data.io_output_json || '{}'
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

  lastFullState[deviceId] = data;
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

  // 设备确认产品型号切换:更新本地 product_type
  if (response.cmd === 'set_product_type' && response.result === 'product_type_set' && response.product_type) {
    db.prepare('UPDATE devices SET product_type = ? WHERE device_id = ?').run(response.product_type, deviceId);
  }

  // 设备确认清除报警:更新本地报警状态
  if (response.cmd === 'clear_alarm' && response.result === 'alarm_cleared') {
    db.prepare("UPDATE device_status SET alarm = 'none', alarm_code = 0 WHERE device_id = ?").run(deviceId);
    db.prepare('UPDATE alarms SET acknowledged = 1, resolved_at = ? WHERE device_id = ? AND resolved_at IS NULL')
      .run(nowISO(), deviceId);
  }

  if (response.cmd === 'buzzer_on' && response.result === 'buzzer_on') {
    db.prepare('UPDATE device_status SET buzzer_on = 1 WHERE device_id = ?').run(deviceId);
  }

  if (response.cmd === 'buzzer_off' && response.result === 'buzzer_off') {
    db.prepare('UPDATE device_status SET buzzer_on = 0 WHERE device_id = ?').run(deviceId);
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
  const alarmType = sanitizeAlarm(data.type || data.alarm_type || 'unknown') === 'none' ? 'unknown' : (data.type || data.alarm_type || 'unknown');
  const message = typeof data.message === 'string' ? data.message.slice(0, 256) : '';
  const level = ['warning', 'danger'].includes(data.level) ? data.level : 'warning';

  db.prepare(
    'INSERT INTO alarms (device_id, alarm_type, message, level, created_at) VALUES (?, ?, ?, ?, ?)'
  ).run(deviceId, alarmType, message, level, nowISO());

  broadcastToClients({
    type: 'alarm',
    device_id: deviceId,
    data: { type: alarmType, message, level }
  });
}

function commandTopicFor(deviceId) {
  let gatewayId = deviceId === defaultDeviceId ? defaultGatewayId : deviceId;
  try {
    const row = getDb().prepare('SELECT gateway_id FROM devices WHERE device_id = ?').get(deviceId);
    if (row && row.gateway_id) gatewayId = row.gateway_id;
  } catch (e) {
    // 数据库未初始化时使用兼容旧逻辑
  }
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
  for (const client of wssClients) {
    const ws = client.ws || client;
    try {
      if (ws.readyState === 1 && canClientReceive(client, message)) {
        ws.send(data);
      }
    } catch (e) {
      wssClients.delete(client);
    }
  }
}

function canClientReceive(client, message) {
  if (!client || !client.user) return false;
  if (client.user.role === 'admin') return true;
  if (!message || !message.device_id) return true;

  try {
    const row = getDb().prepare('SELECT 1 FROM devices WHERE device_id = ? AND owner_id = ?')
      .get(message.device_id, client.user.id);
    return !!row;
  } catch (e) {
    return false;
  }
}

function addWsClient(ws, user) {
  const client = { ws, user };
  wssClients.add(client);
  ws.on('close', () => wssClients.delete(client));
  ws.on('error', () => wssClients.delete(client));
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
