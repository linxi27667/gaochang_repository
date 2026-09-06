const mqtt = require('mqtt');
const { getDb } = require('./database');
const { nowISO } = require('./utils');

let mqttClient = null;
let topicPrefix = process.env.MQTT_TOPIC_PREFIX || 'gaochang/lift';
let v1Prefix = process.env.MQTT_V1_PREFIX || 'gaochang/lift/v1';
let defaultGatewayId = process.env.MQTT_GATEWAY_ID || 'f407zet6';
let defaultDeviceId = process.env.MQTT_DEVICE_ID || 'gaochang_lift_f407zet6';
const wssClients = new Set();

const OFFLINE_TIMEOUT_MS = 30000;   // 30s 未收到遥测则标记离线
const CHECK_INTERVAL_MS = 5000;     // 每 5 秒检查一次
const CMD_TIMEOUT_MS = parseInt(process.env.CMD_TIMEOUT_MS || '15000', 10);  // 命令 15s 未响应则超时
const CMD_MAX_ATTEMPTS = parseInt(process.env.CMD_MAX_ATTEMPTS || '2', 10);  // 命令最大重试次数
const CMD_RETRY_DELAY_MS = parseInt(process.env.CMD_RETRY_DELAY_MS || '2000', 10);
const MQTT_QOS = parseInt(process.env.MQTT_QOS || '1', 10);
const CMD_REDUNDANT_DELAYS_MS = (process.env.CMD_REDUNDANT_DELAYS_MS || '1300,2900')
  .split(',')
  .map((value) => parseInt(value.trim(), 10))
  .filter((value) => Number.isFinite(value) && value > 0);
const CMD_REDUNDANT_JITTER_MS = Math.max(0, parseInt(process.env.CMD_REDUNDANT_JITTER_MS || '350', 10));
const LEGACY_ENABLED = (process.env.MQTT_LEGACY_ENABLED || 'true').toLowerCase() === 'true';
const LEGACY_MAX_DEVICES = parseInt(process.env.MQTT_LEGACY_MAX_DEVICES || '1', 10);
const VALID_PRODUCT_TYPES = ['double_post', 'small_scissor', 'thin_scissor', 'large_scissor'];

// v1 协议 msg_id 去重缓存:5 分钟窗口,最多 1000 条
const DEDUP_WINDOW_MS = parseInt(process.env.OFFLINE_DEDUP_WINDOW_MS || '300000', 10);
const DEDUP_MAX = parseInt(process.env.OFFLINE_DEDUP_MAX || '1000', 10);
const seenMsgIds = new Map();  // msg_id -> timestamp

// 旧主题兼容:已接入的旧固件设备 UID 集合(限制最多 LEGACY_MAX_DEVICES 台)
const legacyDeviceUids = new Set();

let offlineCheckTimer = null;
let cmdTimeoutTimer = null;
let dedupCleanTimer = null;
const redundantCommandTimers = new Map();
const REDUNDANT_COMMANDS = new Set(['lock', 'unlock']);
const MQTT_STREAM_MAX_CARRY_BYTES = parseInt(process.env.MQTT_STREAM_MAX_CARRY_BYTES || '16384', 10);
const MQTT_STREAM_CARRY_TTL_MS = parseInt(process.env.MQTT_STREAM_CARRY_TTL_MS || '30000', 10);
const mqttStreamCarryByTopic = new Map();
// Prevent duplicate shipping-reset publishes when an online device emits
// several packets in the same event-loop window.
const shippingResetDispatchLocks = new Set();

const ALARM_MESSAGES = {
  collision: '碰撞报警',
  // The firmware reports an operation timeout as "stall"; without encoder feedback
  // this is not enough evidence to call it a mechanical stall.
  stall: '运行超时',
  fault: '设备故障',
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
  reset_usage_ok: { cmd: 'reset_usage', result: 'usage_reset' },
  reboot_dtu: { cmd: 'reboot_dtu', result: 'rebooting' },
  // 多产品新增命令回执
  clear_alarm_ok: { cmd: 'clear_alarm', result: 'alarm_cleared' },
  clear_alarm_denied: { cmd: 'clear_alarm', result: 'alarm_clear_denied' },
  set_config_ok: { cmd: 'set_config', result: 'config_set' },
  set_config_denied: { cmd: 'set_config', result: 'config_set_denied' },
  // set_product_type 已移除,不再支持远程切换型号
  config_report: { cmd: 'get_config', result: 'config_reported' },
  buzzer_on_ok: { cmd: 'buzzer_on', result: 'buzzer_on' },
  buzzer_off_ok: { cmd: 'buzzer_off', result: 'buzzer_off' },
  buzzer_on_denied: { cmd: 'buzzer_on', result: 'buzzer_denied' },
  buzzer_off_denied: { cmd: 'buzzer_off', result: 'buzzer_denied' }
};

function normalizeUid(raw) {
  if (!raw) return '';
  let s = String(raw).trim().toLowerCase();
  if (s.startsWith('0x')) s = s.slice(2);
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
    console.log(`[MQTT] Connected (client=${connOptions.clientId}, qos=${MQTT_QOS})`);
    // v1 协议主题:每台设备使用 chip_uid 作为唯一标识
    const v1UpTopic = `${v1Prefix}/devices/+/up`;
    mqttClient.subscribe(v1UpTopic, { qos: MQTT_QOS }, (err, granted) => {
      if (err) {
        console.error(`[MQTT] Subscribe failed topic=${v1UpTopic}: ${err.message}`);
        return;
      }
      const grantedQos = granted && granted[0] ? granted[0].qos : MQTT_QOS;
      console.log(`[MQTT] Subscribed topic=${v1UpTopic} qos=${grantedQos}`);
    });

    // 旧主题兼容(过渡期,限制最多 LEGACY_MAX_DEVICES 台设备)
    if (LEGACY_ENABLED) {
      mqttClient.subscribe(`${topicPrefix}/+/telemetry`, { qos: MQTT_QOS });
      mqttClient.subscribe(`${topicPrefix}/+/status`, { qos: MQTT_QOS });
      mqttClient.subscribe(`${topicPrefix}/+/response`, { qos: MQTT_QOS });
      mqttClient.subscribe(`${topicPrefix}/+/op_log`, { qos: MQTT_QOS });
      mqttClient.subscribe(`${topicPrefix}/+/event`, { qos: MQTT_QOS });
      mqttClient.subscribe(`${topicPrefix}/alarm/+`, { qos: MQTT_QOS });
      console.log(`[mqtt] legacy listener enabled (max ${LEGACY_MAX_DEVICES} device, qos=${MQTT_QOS})`);
    } else {
      console.log('[mqtt] legacy listener disabled');
    }
  });

  mqttClient.on('message', (topic, message) => {
    try {
      const messages = parseMqttStreamChunk(topic, message);
      console.log(`[MQTT] RX topic=${topic} objects=${messages.length} bytes=${message.length}`);
      for (const data of messages) {
        // 区分 v1 主题和旧主题
        if (topic.startsWith(`${v1Prefix}/devices/`)) {
          handleV1Message(topic, data);
        } else if (LEGACY_ENABLED) {
          handleMessage(topic, data);
        } else {
          // 旧主题已禁用,忽略
        }
      }
    } catch (e) {
      const preview = message.toString('utf8').replace(/[\x00-\x1f\x7f]/g, '?').slice(0, 160);
      console.warn('[MQTT] Parse error:', e.message, 'payload=', preview);
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
  startDedupClean();

  return mqttClient;
}

// v1 协议 msg_id 去重:返回 true 表示是重复消息(应忽略),false 表示是新消息
function isDuplicateMsgId(msgId) {
  if (!msgId) return false;
  const now = Date.now();
  const seen = seenMsgIds.get(msgId);
  if (seen && (now - seen) < DEDUP_WINDOW_MS) {
    return true;  // 重复消息
  }
  seenMsgIds.set(msgId, now);
  if (seenMsgIds.size > DEDUP_MAX) {
    // 超过上限,清理最旧的一半
    const entries = [...seenMsgIds.entries()].sort((a, b) => a[1] - b[1]);
    const toRemove = Math.floor(entries.length / 2);
    for (let i = 0; i < toRemove; i++) {
      seenMsgIds.delete(entries[i][0]);
    }
  }
  return false;
}

function startDedupClean() {
  if (dedupCleanTimer) clearInterval(dedupCleanTimer);
  dedupCleanTimer = setInterval(() => {
    const now = Date.now();
    for (const [msgId, ts] of seenMsgIds) {
      if (now - ts > DEDUP_WINDOW_MS) seenMsgIds.delete(msgId);
    }
  }, 60000).unref();
}

// v1 协议消息处理入口
// Topic 格式:gaochang/lift/v1/devices/{chip_uid}/up
// 消息通过 type 字段分流:telemetry/event/operation_log/command_response/offline_batch
function handleV1Message(topic, data) {
  const parts = topic.split('/');
  // parts: ['gaochang','lift','v1','devices','{chip_uid}','up']
  const topicUid = parts[parts.length - 2];
  const jsonUid = normalizeUid(data.chip_uid || data.uid);
  const v1Data = flattenV1Payload(data);
  console.log(`[MQTT] v1 RX topic_uid=${topicUid} json_uid=${jsonUid || '(empty)'} type=${data.type || 'telemetry'} msg_id=${v1Data.msg_id || '-'}`);

  // UID 一致性校验:Topic UID 必须与 JSON UID 一致
  // JSON UID 必须存在且一致,不允许省略 JSON UID 绕过校验
  if (!jsonUid) {
    console.warn(`[mqtt] missing chip_uid/uid in JSON payload from topic uid=${topicUid}, rejecting`);
    auditUidMismatch(topicUid, '(empty)', data);
    return;
  }
  if (topicUid !== jsonUid) {
    console.warn(`[mqtt] UID mismatch: topic=${topicUid} json=${jsonUid}, rejecting and auditing`);
    auditUidMismatch(topicUid, jsonUid, data);
    return;
  }

  // 校验设备是否已登记
  const db = getDb();
  const registry = db.prepare('SELECT serial, product_type, isolation_status, isolation_reason FROM device_registry WHERE uid = ?').get(topicUid);
  if (!registry) {
    console.warn(`[mqtt] unknown device uid=${topicUid}, not in registry, ignoring`);
    auditUnknownDevice(topicUid, data);
    return;
  }

  // 隔离状态检查
  if (registry.isolation_status && registry.isolation_status !== 'ok') {
    console.warn(`[mqtt] device uid=${topicUid} is isolated: ${registry.isolation_reason}, ignoring command responses but still tracking telemetry`);
    // 隔离设备仍记录遥测用于诊断,但不处理命令响应
    if (data.type === 'command_response') return;
  }

  // 产品型号校验
  const reportedType = v1Data.product_type;
  if (reportedType && registry.product_type && reportedType !== registry.product_type) {
    console.warn(`[mqtt] product_type mismatch: uid=${topicUid} registry=${registry.product_type} reported=${reportedType}, isolating device`);
    isolateDevice(topicUid, `product_type mismatch: registry=${registry.product_type} reported=${reportedType}`);
    return;
  }

  // 更新设备最后在线时间
  db.prepare("UPDATE device_registry SET last_seen_at = ?, firmware_version = COALESCE(NULLIF(?, ''), firmware_version), boot_id = COALESCE(NULLIF(?, ''), boot_id) WHERE uid = ?")
    .run(nowISO(), data.firmware_version || '', data.boot_id || '', topicUid);

  // 任何上行消息都刷新 device_status.updated_at,防止只发事件不发遥测的设备被误判离线
  const devRow = db.prepare('SELECT device_id FROM devices WHERE uid = ? OR device_id = ?').get(topicUid, registry.serial);
  if (devRow) {
    try {
      const recovered = db.prepare('SELECT online FROM device_status WHERE device_id = ?').get(devRow.device_id);
      db.prepare('UPDATE device_status SET online = 1, updated_at = ? WHERE device_id = ?').run(nowISO(), devRow.device_id);
      if (recovered && !recovered.online) broadcastToClients({
        type: 'device_status', device_id: devRow.device_id, data: { online: true }
      });
      dispatchPendingShippingReset(devRow.device_id);
    } catch (e) { /* ignore */ }
  }

  // msg_id 去重
  // 注意:offline_batch 类型不走这里的去重,因为 ACK 可能丢失导致设备重发
  // offline_batch 内部对每条记录单独去重,且无论是否重复都会补发 ACK
  const msgId = v1Data.msg_id;
  const messageType = data.type || 'telemetry';
  if (messageType !== 'offline_batch' && messageType !== 'command_response' && msgId && isDuplicateMsgId(msgId)) {
    console.log(`[mqtt] duplicate msg_id=${msgId}, ignoring`);
    return;
  }

  const deviceId = registry.serial || topicUid;
  const type = messageType;
  console.log(`[MQTT] v1 accepted uid=${topicUid} device=${deviceId} product=${registry.product_type || '-'} type=${type}`);

  switch (type) {
    case 'telemetry':
      handleV1Telemetry(deviceId, topicUid, v1Data, registry);
      break;
    case 'height':
      handleV1Telemetry(deviceId, topicUid, v1Data, registry, true);
      break;
    case 'status':
      handleV1Telemetry(deviceId, topicUid, v1Data, registry, true);
      break;
    case 'motion':
      handleV1Telemetry(deviceId, topicUid, v1Data, registry, true);
      break;
    case 'rise_count':
      handleV1Event(deviceId, topicUid, { ...v1Data, event_type: 'rise_count' });
      break;
    case 'rise_counter':
    case 'usage':
      handleV1Telemetry(deviceId, topicUid, v1Data, registry, true);
      break;
    case 'event':
      handleV1Event(deviceId, topicUid, v1Data);
      break;
    case 'operation_log':
      handleV1OpLog(deviceId, topicUid, v1Data);
      break;
    case 'logs_batch':
      handleV1LogsBatch(deviceId, topicUid, v1Data);
      break;
    case 'command_response':
      handleV1CommandResponse(deviceId, topicUid, v1Data);
      break;
    case 'offline_batch':
      handleV1OfflineBatch(deviceId, topicUid, v1Data);
      break;
    default:
      console.warn(`[mqtt] unknown v1 message type: ${type} from uid=${topicUid}`);
  }
}

function flattenV1Payload(data) {
  const body = data && typeof data.data === 'object' && data.data && !Array.isArray(data.data)
    ? data.data
    : {};
  return {
    ...data,
    ...body,
    type: data.type
  };
}

// v1 遥测处理:复用 normalizeTelemetry,但加上 UID 一致性保证
const telemetryEventState = new Map();
const lockTelemetryGuards = new Map();
const LOCK_TELEMETRY_GUARD_MS = 30000;

const PRODUCT_LOG_CAPABILITIES = {
  double_post: { photo: false, lower: false, rotary: false },
  small_scissor: { photo: true, lower: true, rotary: false },
  thin_scissor: { photo: true, lower: true, rotary: false },
  large_scissor: { photo: true, lower: true, rotary: true }
};

function adaptFirmwarePayload(productType, data) {
  const adapted = { ...data, product_type: productType };
  const ioInput = { ...objectOrEmpty(data.io_input) };
  const ioOutput = { ...objectOrEmpty(data.io_output || data.output) };
  if (data.estop != null) ioInput.estop = data.estop;
  if (data.upper_limit != null) ioInput.upper_limit = data.upper_limit;
  if (data.lower_limit != null) ioInput.lower_limit = data.lower_limit;
  if (Object.keys(ioInput).length) adapted.io_input = ioInput;
  if (Object.keys(ioOutput).length) adapted.io_output = ioOutput;
  if (['usage', 'rise_counter', 'rise_count'].includes(data.type) && productType === 'double_post') {
    if (data.rise_total_ms != null) adapted.total_run_ms = data.rise_total_ms;
    if (data.rise_count != null) adapted.run_count = data.rise_count;
  }
  return adapted;
}

function deriveOperationLogsFromTelemetry(deviceId, chipUid, data) {
  const input = data.io_input || {};
  const caps = PRODUCT_LOG_CAPABILITIES[data.product_type] || PRODUCT_LOG_CAPABILITIES.double_post;
  const role = data.rotary_switch === 'sub' ? 'sub' : 'main';
  const current = {
    state: String(data.state || 'idle'), role, tick: Number(data.tick || 0), seq: Number(data.seq || 0),
    estop: Number(input.estop || 0), photo: Number(input.photoelectric || 0),
    upper: Number(input.upper_limit || 0), subUpper: Number(input.sub_upper_limit || 0),
    lower: Number(input.lower_limit || 0)
  };
  const previous = telemetryEventState.get(chipUid);
  if (!caps.photo) current.photo = 0;
  if (!caps.lower) current.lower = 0;
  if (!caps.rotary) current.role = 'main';
  if (!previous) {
    current.motionStartTick = ['rising', 'dropping', 'up', 'down'].includes(current.state) ? current.tick : 0;
    telemetryEventState.set(chipUid, current);
    return;
  }

  const emit = (opType, detail, durationMs = 0, opResult = 'ok') => handleOpLog(deviceId, {
    uid: chipUid, chip_uid: chipUid, op_type: opType, op_result: opResult, role,
    duration_ms: durationMs, device_tick_ms: current.tick, device_state: current.state,
    detail, msg_id: `telemetry:${current.seq}:${opType}`
  });
  const wasMoving = ['rising', 'dropping', 'up', 'down'].includes(previous.state);
  const isRising = ['rising', 'up'].includes(current.state);
  const isDropping = ['dropping', 'down'].includes(current.state);
  const sameMotion = (isRising && ['rising', 'up'].includes(previous.state)) ||
    (isDropping && ['dropping', 'down'].includes(previous.state));
  current.motionStartTick = (isRising || isDropping) ? (sameMotion ? previous.motionStartTick : current.tick) : 0;
  if (isRising && !['rising', 'up'].includes(previous.state)) emit('up_start', `${role === 'sub' ? '子机' : '主机'}开始上升`);
  if (isDropping && !['dropping', 'down'].includes(previous.state)) emit('down_start', `${role === 'sub' ? '子机' : '主机'}开始下降`);
  if (wasMoving && !isRising && !isDropping) {
    const duration = Math.max(0, current.tick - (previous.motionStartTick || previous.tick));
    emit(['rising', 'up'].includes(previous.state) ? 'up_stop_release' : 'down_stop_release', '动作结束', duration);
  }
  if (!previous.estop && current.estop) emit('estop', '急停按钮按下', 0, 'interrupted');
  if (!previous.photo && current.photo) emit('photo_alarm', '光电传感器触发', 0, 'interrupted');
  if (!previous.upper && current.upper) emit('upper_limit', '主机上限位触发');
  if (!previous.subUpper && current.subUpper) emit('sub_upper_limit', '子机上限位触发');
  if (!previous.lower && current.lower) emit('lower_limit', '下限位触发');
  if (previous.role !== current.role) emit('rotary_switch', `旋转开关切换到${role === 'sub' ? '子机' : '主机'}`);
  telemetryEventState.set(chipUid, current);
}

function handleV1Telemetry(deviceId, chipUid, data, registry, partial = false) {
  const adapted = adaptFirmwarePayload(registry.product_type, data);
  applyLockTelemetryGuard(deviceId, adapted);
  if (['telemetry', 'motion', 'height'].includes(data.type)) deriveOperationLogsFromTelemetry(deviceId, chipUid, adapted);
  const normalized = normalizeTelemetry(adapted);
  normalized.uid = chipUid;
  normalized.chip_uid = chipUid;
  if (registry.product_type) normalized.product_type = registry.product_type;
  if (partial) mergePartialStatus(deviceId, normalized, adapted);
  handleStatusUpdate(deviceId, normalized);
}

function applyLockTelemetryGuard(deviceId, telemetry) {
  const guard = lockTelemetryGuards.get(deviceId);
  if (!guard || telemetry.locked == null) return;
  const reportedLocked = !!telemetry.locked;
  if (reportedLocked === guard.locked) {
    lockTelemetryGuards.delete(deviceId);
    console.log(`[CMD] lock telemetry confirmed device=${deviceId} locked=${Number(guard.locked)} msg_id=${guard.msgId} at_ms=${Date.now()}`);
    return;
  }
  if (Date.now() >= guard.expiresAt) {
    lockTelemetryGuards.delete(deviceId);
    console.warn(`[CMD] lock telemetry guard expired device=${deviceId} expected=${Number(guard.locked)} reported=${Number(reportedLocked)} msg_id=${guard.msgId} at_ms=${Date.now()}`);
    return;
  }
  telemetry.locked = guard.locked;
  telemetry.state = guard.locked ? 'locked' : (telemetry.state === 'locked' ? 'idle' : telemetry.state);
  console.warn(`[CMD] stale lock telemetry suppressed device=${deviceId} expected=${Number(guard.locked)} reported=${Number(reportedLocked)} msg_id=${guard.msgId} at_ms=${Date.now()}`);
}

function mergePartialStatus(deviceId, normalized, raw = {}) {
  const current = getDb().prepare('SELECT * FROM device_status WHERE device_id = ?').get(deviceId);
  if (!current) return normalized;
  const preserve = ['run_count', 'run_time_s', 'total_run_ms', 'up_count', 'down_count', 'lock_count',
    'refill_count', 'estop_count', 'photo_alarm_count', 'last_run_at'];
  for (const key of preserve) {
    if (normalized[key] == null || normalized[key] === 0) normalized[key] = current[key];
  }
  for (const key of ['io_input_json', 'io_output_json']) {
    let oldValue = {};
    let newValue = {};
    try { oldValue = objectOrEmpty(JSON.parse(current[key] || '{}')); } catch (e) { /* invalid legacy JSON */ }
    try { newValue = objectOrEmpty(JSON.parse(normalized[key] || '{}')); } catch (e) { /* invalid payload JSON */ }
    normalized[key] = JSON.stringify({ ...oldValue, ...newValue });
  }

  const safety = objectOrEmpty(raw.safety);
  const io = objectOrEmpty(raw.io);
  const ioInput = objectOrEmpty(raw.io_input || io.input);
  const hasOwn = (obj, key) => Object.prototype.hasOwnProperty.call(obj, key);
  const lowerReported = hasOwn(raw, 'lower_limit') || hasOwn(safety, 'lower') ||
    hasOwn(safety, 'lower_limit') || hasOwn(ioInput, 'lower_limit') || hasOwn(ioInput, 'limit_down');
  const upperReported = hasOwn(raw, 'upper_limit') || hasOwn(safety, 'upper') ||
    hasOwn(safety, 'upper_limit') || hasOwn(ioInput, 'upper_limit') || hasOwn(ioInput, 'limit_up');

  // status/command_response packets often omit IO. Treat omitted limits as unchanged,
  // rather than overwriting the last telemetry value with normalizeTelemetry's default 0.
  if (!lowerReported) normalized.lower_limit = current.lower_limit;
  if (!upperReported) normalized.upper_limit = current.upper_limit;
  return normalized;
}

// v1 事件处理
function handleV1Event(deviceId, chipUid, data) {
  // 复用现有 handleEvent,但传入 chip_uid
  const enriched = { ...data, uid: chipUid, chip_uid: chipUid };
  handleEvent(deviceId, enriched);
}

// v1 操作日志处理
function handleV1OpLog(deviceId, chipUid, data) {
  const enriched = { ...data, uid: chipUid, chip_uid: chipUid };
  handleOpLog(deviceId, enriched);
}

function handleV1LogsBatch(deviceId, chipUid, data) {
  const logs = Array.isArray(data.logs) ? data.logs : [];
  console.log(`[MQTT] logs_batch device=${deviceId} uid=${chipUid} count=${logs.length} start=${data.start_index || 0}`);
  logs.forEach((item, index) => handleOpLog(deviceId, {
    ...item,
    uid: chipUid,
    chip_uid: chipUid,
    log_index: Number(data.start_index || 0) + index,
    batch_seq: data.seq,
    device_tick_ms: item.device_tick_ms ?? item.ts
  }));
}

// v1 命令响应处理:使用新的命令状态机
function handleV1CommandResponse(deviceId, chipUid, data) {
  const db = getDb();
  const msgId = data.msg_id;
  const result = data.result || 'succeeded';  // succeeded / rejected / failed
  const cmd = data.cmd || '';
  const reason = data.reason || '';

  if (!msgId || !cmd) {
    console.warn(`[mqtt] v1 command_response missing msg_id/cmd from uid=${chipUid}`);
    return;
  }

  // 更新命令状态:只有 pending/sent 状态的命令才能被响应(防止终态被覆盖)
  // 同时校验 chip_uid 归属,防止 msg_id 碰撞导致跨设备误匹配
  const cmdRow = db.prepare(
    'SELECT id, status, cmd, device_id, attempts, chip_uid, purpose FROM command_queue WHERE msg_id = ?'
  ).get(msgId);
  if (!cmdRow) {
    console.warn(`[MQTT] Response rejected reason=unknown_msg_id uid=${chipUid} cmd=${cmd || '-'} msg_id=${msgId}`);
    return;
  }

  // 校验响应来源 UID 与命令归属 UID 一致
  if (cmdRow.chip_uid && chipUid && normalizeUid(cmdRow.chip_uid) !== normalizeUid(chipUid)) {
    console.warn(`[mqtt] command_response uid mismatch: cmd belongs to ${cmdRow.chip_uid} but response from ${chipUid}`);
    auditUidMismatch(chipUid, cmdRow.chip_uid, data);
    return;
  }
  if (!cmdRow.cmd || normalizeCommand(cmd) !== normalizeCommand(cmdRow.cmd)) {
    console.warn(`[MQTT] Response rejected reason=cmd_mismatch uid=${chipUid} expected=${cmdRow.cmd} actual=${cmd} msg_id=${msgId}`);
    return;
  }

  // 终态命令不再处理(幂等:重复响应只记录日志,不重复驱动硬件)
  if (cmdRow.status && !['pending', 'sent'].includes(cmdRow.status)) {
    console.log(`[mqtt] command ${msgId} already in terminal state ${cmdRow.status}, ignoring duplicate response`);
    return;
  }

  // 终态状态:succeeded / rejected / failed
  let finalStatus = ['succeeded', 'rejected', 'failed'].includes(result)
    ? result
    : mapLegacyResultToStatus(cmd, result);
  if (finalStatus === 'succeeded' && cmdRow.cmd === 'reset_usage' && cmdRow.purpose === 'shipping_reset'
      && !normalizeTelemetry(data || {}).maintenance_reported) {
    finalStatus = 'failed';
    console.warn(`[MQTT] shipping reset rejected reason=missing_device_ledger device=${cmdRow.device_id} msg_id=${msgId}`);
  }
  // WHERE 加状态约束,确保只有 pending/sent 的命令被更新(并发安全)
  const storedResult = finalStatus === 'succeeded' && (cmdRow.cmd || cmd) === 'reset_usage'
    ? 'usage_reset'
    : (finalStatus === 'succeeded' && (cmdRow.cmd || cmd) === 'maintenance_done' ? 'maintenance_done' : (reason || result));
  const updateResult = db.prepare(
    "UPDATE command_queue SET status = ?, result = ?, responded_at = ?, last_attempt_at = ? WHERE msg_id = ? AND status IN ('pending', 'sent')"
  ).run(finalStatus, storedResult, nowISO(), nowISO(), msgId);
  if (updateResult.changes === 0) {
    console.log(`[mqtt] command ${msgId} state changed concurrently, skipping`);
    return;
  }

  cancelRedundantCommandPublishes(msgId, `response_${finalStatus}`);

  if (finalStatus === 'succeeded' && ['lock', 'unlock'].includes(cmdRow.cmd || cmd)) {
    lockTelemetryGuards.set(cmdRow.device_id, {
      locked: (cmdRow.cmd || cmd) === 'lock',
      msgId,
      expiresAt: Date.now() + LOCK_TELEMETRY_GUARD_MS
    });
  }

  // 只在收到 command_response 后才更新设备状态(不再在命令发送时立即修改)
  applyCommandResultToDevice(cmdRow.device_id, cmdRow.cmd || cmd, finalStatus, data, msgId);
  advanceShippingResetChain(db, cmdRow, finalStatus);

  console.log(`[MQTT] Response matched device=${cmdRow.device_id} uid=${chipUid} msg_id=${msgId} cmd=${cmdRow.cmd || cmd} wire_result=${result} status=${finalStatus} at_ms=${Date.now()}`);

  broadcastToClients({
    type: 'command_response',
    device_id: cmdRow.device_id,
    data: {
      msg_id: msgId,
      cmd: cmd,
      result: finalStatus,
      reason: reason,
      chip_uid: chipUid
    }
  });
}

// v1 离线补传处理
function handleV1OfflineBatch(deviceId, chipUid, data) {
  const batch = data && typeof data.data === 'object' && data.data ? data.data : data;
  const batchId = batch.batch_id || data.batch_id || '';
  const records = Array.isArray(batch.records)
    ? batch.records
    : (Array.isArray(data.records) ? data.records : []);
  console.log(`[mqtt] v1 offline_batch from uid=${chipUid}: ${records.length} records`);

  let processedCount = 0;
  for (const record of records) {
    const recordType = record.type || 'telemetry';
    const recordWithUid = {
      ...flattenV1Payload(record),
      chip_uid: chipUid,
      uid: chipUid
    };

    // 离线补传的 msg_id 也要去重(跳过已处理的记录)
    if (recordWithUid.msg_id && isDuplicateMsgId(recordWithUid.msg_id)) {
      continue;
    }

    processedCount++;
    switch (recordType) {
      case 'telemetry':
        handleV1Telemetry(deviceId, chipUid, recordWithUid, null);
        break;
      case 'event':
        handleV1Event(deviceId, chipUid, recordWithUid);
        break;
      case 'operation_log':
        handleV1OpLog(deviceId, chipUid, recordWithUid);
        break;
      case 'command_response':
        handleV1CommandResponse(deviceId, chipUid, recordWithUid);
        break;
    }
  }

  // 始终发送离线补传确认(即使所有记录都是重复的),防止 ACK 丢失导致设备死循环重发
  // received_count 报告实际新处理的记录数,帮助设备判断是否需要重发未确认的记录
  if (batchId) {
    const recordIds = records
      .map((record) => record.record_id || record.msg_id || '')
      .filter(Boolean);
    publishV1Down(chipUid, {
      v: 1,
      type: 'command',
      cmd: 'offline_batch_ack',
      target_uid: chipUid,
      msg_id: `offline_ack_${batchId}`,
      operator: 'system',
      created_at: new Date().toISOString(),
      args: {
        batch_id: batchId,
        accepted_record_ids: recordIds,
        rejected_record_ids: [],
        received_count: processedCount,
        total_count: records.length
      }
    });
  }
}

// 只在收到 command_response 后才更新设备状态
// 这是 P2 阶段的核心改动:不再在命令发送时立即修改设备状态
function applyCommandResultToDevice(deviceId, cmd, result, responseData, msgId = '') {
  const db = getDb();
  const now = nowISO();

  if (result !== 'succeeded') return;

  switch (cmd) {
    case 'lock':
      db.prepare('UPDATE device_status SET locked = 1 WHERE device_id = ?').run(deviceId);
      break;
    case 'unlock':
      db.prepare('UPDATE device_status SET locked = 0 WHERE device_id = ?').run(deviceId);
      break;
    case 'clear_alarm':
      db.prepare("UPDATE device_status SET alarm = 'none', alarm_code = 0 WHERE device_id = ?").run(deviceId);
      db.prepare('UPDATE alarms SET acknowledged = 1, resolved_at = ? WHERE device_id = ? AND resolved_at IS NULL')
        .run(now, deviceId);
      break;
    case 'fault_clear':
      // 清故障:重置故障相关状态,与 clear_alarm 区分
      db.prepare("UPDATE device_status SET alarm = 'none', alarm_code = 0, stall = 0, collision_up = 0, collision_down = 0 WHERE device_id = ?").run(deviceId);
      db.prepare('UPDATE alarms SET acknowledged = 1, resolved_at = ? WHERE device_id = ? AND resolved_at IS NULL')
        .run(now, deviceId);
      break;
    case 'buzzer_on':
      db.prepare('UPDATE device_status SET buzzer_on = 1 WHERE device_id = ?').run(deviceId);
      break;
    case 'buzzer_off':
      db.prepare('UPDATE device_status SET buzzer_on = 0 WHERE device_id = ?').run(deviceId);
      break;
    case 'rename':
      if (responseData.name) {
        db.prepare('UPDATE devices SET name = ? WHERE device_id = ?').run(responseData.name, deviceId);
      }
      break;
    case 'maintenance_done': {
      const current = db.prepare(`SELECT total_lift_count, maintenance_lift_count, maintenance_threshold,
        maintenance_count, last_maintenance_total, maintenance_due, usage_epoch, maintenance_revision FROM device_status WHERE device_id = ?`).get(deviceId);
      const cycleBefore = clampInt(current?.maintenance_lift_count, 0, 4294967295, 0);
      const reported = normalizeTelemetry(responseData || {});
      // A successful maintenance command is not enough to mutate the cache.
      // The device Flash ledger is authoritative, so require its maintenance
      // snapshot in the command response before updating SQLite.
      if (!reported.maintenance_reported) {
        console.warn(`[MQTT] maintenance_done response missing device ledger; keeping cached counters device=${deviceId} msg_id=${msgId || '-'}`);
        return;
      }
      const next = resolveMaintenanceStatus(current, reported);
      const command = msgId ? db.prepare('SELECT operator_name FROM command_queue WHERE msg_id = ?').get(msgId) : null;
      db.transaction(() => {
        db.prepare(`UPDATE device_status SET total_lift_count=?, maintenance_lift_count=?, maintenance_threshold=?,
          maintenance_count=?, last_maintenance_total=?, maintenance_due=?, usage_epoch=?, maintenance_revision=? WHERE device_id=?`).run(
          next.total_lift_count, next.maintenance_lift_count, next.maintenance_threshold, next.maintenance_count,
          next.last_maintenance_total, next.maintenance_due, next.usage_epoch, next.maintenance_revision, deviceId
        );
        if (msgId) db.prepare(`INSERT OR IGNORE INTO maintenance_records
          (device_id, type, description, handler, result, command_msg_id, total_lift_count,
           maintenance_lift_count, maintenance_count, created_at)
          VALUES (?, '保养', ?, ?, 'maintenance_done', ?, ?, ?, ?, ?)`)
          .run(deviceId, `maintenance_done msg_id=${msgId}`, command?.operator_name || '', msgId,
              next.total_lift_count, cycleBefore, next.maintenance_count, now);
      })();
      break;
    }
    case 'reset_usage': {
      const current = db.prepare(`SELECT total_lift_count, maintenance_lift_count, maintenance_threshold,
        maintenance_count, last_maintenance_total, maintenance_due, usage_epoch, maintenance_revision FROM device_status WHERE device_id = ?`).get(deviceId);
      const before = current ? {
        total_lift_count: current.total_lift_count,
        maintenance_lift_count: current.maintenance_lift_count,
        maintenance_count: current.maintenance_count,
        usage_epoch: current.usage_epoch
      } : {};
      const reported = normalizeTelemetry(responseData || {});
      if (!reported.maintenance_reported) {
        console.warn(`[MQTT] reset_usage response missing device ledger; keeping cached counters device=${deviceId} msg_id=${msgId || '-'}`);
        return;
      }
      const next = resolveMaintenanceStatus(current, reported);
      const command = msgId ? db.prepare('SELECT operator_id, purpose FROM command_queue WHERE msg_id = ?').get(msgId) : null;
      if (command && command.purpose === 'shipping_reset') {
        applyShippingResetCleanup(db, deviceId, msgId, command.operator_id, current, next, now);
        break;
      }
      db.prepare(`UPDATE device_status SET total_lift_count=?, maintenance_lift_count=?, maintenance_threshold=?,
        maintenance_count=?, last_maintenance_total=?, maintenance_due=?, usage_epoch=?, maintenance_revision=? WHERE device_id=?`).run(
        next.total_lift_count, next.maintenance_lift_count, next.maintenance_threshold, next.maintenance_count,
        next.last_maintenance_total, next.maintenance_due, next.usage_epoch, next.maintenance_revision, deviceId
      );
      if (command) {
        db.prepare('INSERT INTO operation_logs (user_id, action, device_id, detail, result, created_at) VALUES (?, ?, ?, ?, ?, ?)')
          .run(command.operator_id || null, 'reset_usage', deviceId, JSON.stringify({ msg_id: msgId, before, after: next }), 'usage_reset', now);
      }
      break;
    }
    // set_product_type 已移除,不再支持远程切换型号
  }
}

function applyShippingResetCleanup(db, deviceId, msgId, operatorId, before, next, now) {
  const device = db.prepare(`
    SELECT d.uid, r.serial FROM devices d
    LEFT JOIN device_registry r ON r.uid = d.uid
    WHERE d.device_id = ?
  `).get(deviceId) || {};

  db.transaction(() => {
    db.prepare(`UPDATE device_status SET
      run_count=0, run_time_s=0, total_run_ms=0, up_count=0, down_count=0,
      lock_count=0, refill_count=0, estop_count=0, photo_alarm_count=0,
      total_lift_count=?, maintenance_lift_count=?, maintenance_count=?,
      last_maintenance_total=?, maintenance_due=?, usage_epoch=?, maintenance_revision=?,
      last_run_at=NULL
      WHERE device_id=?`).run(
      next.total_lift_count, next.maintenance_lift_count, next.maintenance_count,
      next.last_maintenance_total, next.maintenance_due, next.usage_epoch,
      next.maintenance_revision, deviceId
    );
    db.prepare('DELETE FROM alarms WHERE device_id = ?').run(deviceId);
    db.prepare('DELETE FROM maintenance_records WHERE device_id = ?').run(deviceId);
    db.prepare('DELETE FROM command_queue WHERE device_id = ? AND msg_id <> ?').run(deviceId, msgId);
    if (device.uid || device.serial) {
      db.prepare(`DELETE FROM device_operation_logs
        WHERE (? <> '' AND device_uid = ?) OR (? <> '' AND device_serial = ?)`)
        .run(device.uid || '', device.uid || '', device.serial || '', device.serial || '');
    }
    db.prepare(`INSERT INTO operation_logs
      (user_id, action, device_id, detail, result, created_at) VALUES (?, ?, ?, ?, ?, ?)`)
      .run(operatorId || null, 'shipping_reset', deviceId, JSON.stringify({
        msg_id: msgId,
        before: before || {},
        after: next,
        preserved: ['device', 'uid', 'product_type', 'registry', 'bindings', 'users']
      }), 'completed', now);
  })();

  broadcastToClients({
    type: 'device_status',
    device_id: deviceId,
    data: {
      run_count: 0,
      run_time_s: 0,
      total_run_ms: 0,
      up_count: 0,
      down_count: 0,
      lock_count: 0,
      refill_count: 0,
      estop_count: 0,
      photo_alarm_count: 0,
      total_lift_count: next.total_lift_count,
      maintenance_lift_count: next.maintenance_lift_count,
      maintenance_count: next.maintenance_count,
      last_maintenance_total: next.last_maintenance_total,
      maintenance_due: next.maintenance_due,
      usage_epoch: next.usage_epoch,
      maintenance_revision: next.maintenance_revision,
      last_run_at: null
    }
  });
}

// 隔离设备:UID 不一致或型号不匹配时
function isolateDevice(chipUid, reason) {
  const db = getDb();
  db.prepare('UPDATE device_registry SET isolation_status = ?, isolation_reason = ? WHERE uid = ?')
    .run('isolated', reason, chipUid);

  // 记录审计日志(user_id 为 NULL 表示系统自动操作)
  try {
    db.prepare('INSERT INTO operation_logs (user_id, action, device_id, detail, result, created_at) VALUES (?, ?, ?, ?, ?, ?)')
      .run(null, 'device_isolated', chipUid, reason, '隔离', nowISO());
  } catch (e) {
    // operation_logs 表可能没有 device_id 列兼容
  }

  broadcastToClients({
    type: 'device_isolated',
    device_id: chipUid,
    data: { chip_uid: chipUid, reason }
  });
}

function auditUidMismatch(topicUid, jsonUid, data) {
  const db = getDb();
  try {
    db.prepare('INSERT INTO operation_logs (user_id, action, device_id, detail, result, created_at) VALUES (?, ?, ?, ?, ?, ?)')
      .run(null, 'uid_mismatch', topicUid, `topic=${topicUid} json=${jsonUid} type=${data.type || ''}`, '拒绝', nowISO());
  } catch (e) {
    // ignore
  }
}

function auditUnknownDevice(chipUid, data) {
  const db = getDb();
  try {
    db.prepare('INSERT INTO operation_logs (user_id, action, device_id, detail, result, created_at) VALUES (?, ?, ?, ?, ?, ?)')
      .run(null, 'unknown_device', chipUid, `type=${data.type || ''} boot_id=${data.boot_id || ''}`, '未登记', nowISO());
  } catch (e) {
    // ignore
  }
}

// v1 下行命令发布:向 {chip_uid}/down 发布,QoS 1,retained=false
function cancelRedundantCommandPublishes(msgId, reason = 'terminal') {
  const timers = redundantCommandTimers.get(msgId);
  if (!timers) return;
  for (const timer of timers) clearTimeout(timer);
  redundantCommandTimers.delete(msgId);
  console.log(`[CMD] redundant publishes cancelled msg_id=${msgId} reason=${reason} at_ms=${Date.now()}`);
}

function isCommandAwaitingResponse(msgId) {
  try {
    const row = getDb().prepare('SELECT status FROM command_queue WHERE msg_id = ?').get(msgId);
    return !!row && ['pending', 'sent'].includes(row.status);
  } catch (error) {
    console.warn(`[CMD] redundant publish state check failed msg_id=${msgId}: ${error.message}`);
    return false;
  }
}

function scheduleRedundantCommandPublishes(chipUid, payload) {
  if (!REDUNDANT_COMMANDS.has(payload.cmd) || CMD_REDUNDANT_DELAYS_MS.length === 0) return;

  cancelRedundantCommandPublishes(payload.msg_id, 'rescheduled');
  const timers = new Set();
  redundantCommandTimers.set(payload.msg_id, timers);

  CMD_REDUNDANT_DELAYS_MS.forEach((baseDelay, index) => {
    const jitter = CMD_REDUNDANT_JITTER_MS > 0
      ? Math.floor(Math.random() * (CMD_REDUNDANT_JITTER_MS + 1))
      : 0;
    const delay = baseDelay + jitter;
    const timer = setTimeout(() => {
      timers.delete(timer);
      if (!isCommandAwaitingResponse(payload.msg_id)) {
        cancelRedundantCommandPublishes(payload.msg_id, 'already_terminal');
        return;
      }
      publishV1Down(chipUid, payload, { trackAttempt: false, redundantIndex: index + 1 });
      if (timers.size === 0) redundantCommandTimers.delete(payload.msg_id);
    }, delay);
    timer.unref?.();
    timers.add(timer);
    console.log(`[CMD] redundant publish scheduled cmd=${payload.cmd} msg_id=${payload.msg_id} copy=${index + 1} delay_ms=${delay}`);
  });
}

function publishV1Down(chipUid, payload, options = {}) {
  const { trackAttempt = true, redundantIndex = 0 } = options;
  if (!mqttClient || !mqttClient.connected) {
    console.warn('[mqtt] not connected, cannot publish v1 down');
    return false;
  }
  const topic = `${v1Prefix}/devices/${chipUid}/down`;
  const message = JSON.stringify(payload);
  mqttClient.publish(topic, message, { qos: MQTT_QOS, retain: false }, (err) => {
    if (err) {
      console.error(`[MQTT] Publish failed topic=${topic} cmd=${payload.cmd || '-'} msg_id=${payload.msg_id || '-'} redundant=${redundantIndex}: ${err.message}`);
      if (!trackAttempt) return;
      try {
        getDb().prepare("UPDATE command_queue SET status = 'failed', result = ? WHERE msg_id = ? AND status IN ('pending','sent')")
          .run(`publish_failed: ${err.message}`.slice(0, 256), payload.msg_id || '');
        cancelRedundantCommandPublishes(payload.msg_id || '', 'publish_failed');
      } catch (dbError) {
        console.error(`[MQTT] Failed to persist publish error msg_id=${payload.msg_id || '-'}: ${dbError.message}`);
      }
      return;
    }
    console.log(`[MQTT] Publish acknowledged topic=${topic} cmd=${payload.cmd || '-'} msg_id=${payload.msg_id || '-'} redundant=${redundantIndex} at_ms=${Date.now()}`);
    if (trackAttempt) updateCommandSent(payload.msg_id || '');
  });
  console.log(`[MQTT] TX queued topic=${topic} cmd=${payload.cmd || '-'} msg_id=${payload.msg_id || '-'} qos=${MQTT_QOS} redundant=${redundantIndex} at_ms=${Date.now()}`);
  return true;
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

function pruneMqttStreamCarry(now = Date.now()) {
  for (const [topic, state] of mqttStreamCarryByTopic) {
    if ((now - state.updatedAt) > MQTT_STREAM_CARRY_TTL_MS) {
      console.warn(`[MQTT] Reassembly carry expired topic=${topic} bytes=${Buffer.byteLength(state.text, 'utf8')}`);
      mqttStreamCarryByTopic.delete(topic);
    }
  }
}

function parseMqttStreamChunk(topic, payload) {
  const now = Date.now();
  pruneMqttStreamCarry(now);

  const previous = mqttStreamCarryByTopic.get(topic)?.text || '';
  const chunk = Buffer.isBuffer(payload) ? payload.toString('utf8') : String(payload || '');
  const text = previous + chunk;
  const messages = [];
  let depth = 0;
  let start = -1;
  let inString = false;
  let escaped = false;
  let ignoredBytes = 0;

  for (let i = 0; i < text.length; i++) {
    const ch = text[i];

    if (start < 0) {
      if (ch === '{') {
        start = i;
        depth = 1;
        inString = false;
        escaped = false;
      } else if (!/\s/.test(ch)) {
        ignoredBytes++;
      }
      continue;
    }

    if (inString) {
      if (escaped) escaped = false;
      else if (ch === '\\') escaped = true;
      else if (ch === '"') inString = false;
      continue;
    }

    if (ch === '"') {
      inString = true;
    } else if (ch === '{') {
      depth++;
    } else if (ch === '}') {
      depth--;
      if (depth === 0) {
        const candidate = text.slice(start, i + 1);
        try {
          messages.push(JSON.parse(candidate));
        } catch (error) {
          const safePreview = candidate.replace(/[\x00-\x1f\x7f]/g, '?').slice(0, 160);
          console.warn(`[MQTT] Reassembly dropped invalid object topic=${topic} bytes=${Buffer.byteLength(candidate, 'utf8')} error=${error.message} payload=${safePreview}`);
        }
        start = -1;
        inString = false;
        escaped = false;
      }
    }
  }

  if (ignoredBytes > 0) {
    console.warn(`[MQTT] Reassembly ignored non-JSON bytes topic=${topic} bytes=${ignoredBytes}`);
  }

  const carry = start >= 0 ? text.slice(start) : '';
  const carryBytes = Buffer.byteLength(carry, 'utf8');
  if (carryBytes > MQTT_STREAM_MAX_CARRY_BYTES) {
    mqttStreamCarryByTopic.delete(topic);
    console.warn(`[MQTT] Reassembly carry overflow topic=${topic} bytes=${carryBytes} limit=${MQTT_STREAM_MAX_CARRY_BYTES}`);
  } else if (carry) {
    mqttStreamCarryByTopic.set(topic, { text: carry, updatedAt: now });
    console.log(`[MQTT] Reassembly buffered topic=${topic} bytes=${carryBytes}`);
  } else {
    mqttStreamCarryByTopic.delete(topic);
  }

  return messages;
}

function resetMqttStreamCarry() {
  mqttStreamCarryByTopic.clear();
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
    } else if (depth === 0 && !/\s/.test(ch)) {
      throw new Error(`unexpected data outside JSON at byte ${i}`);
    }
  }

  if (inString) throw new Error('unterminated JSON string');
  if (depth !== 0) throw new Error(`incomplete JSON object, depth=${depth}`);
  if (objects.length === 0) {
    throw new Error('payload contains no complete JSON object');
  }

  return objects;
}

function handleMessage(topic, data) {
  const parts = topic.split('/');
  const prefixParts = topicPrefix.split('/');
  const tail = parts.slice(prefixParts.length);
  const gatewayId = tail[0];
  const topicType = tail[1];

  // 旧主题设备数限制:跟踪已接入的旧固件设备 UID,超过 LEGACY_MAX_DEVICES 则拒绝
  const legacyUid = normalizeUid(data.uid || data.chip_uid || data.mcu_uid || gatewayId);
  if (legacyUid) {
    if (!legacyDeviceUids.has(legacyUid)) {
      if (legacyDeviceUids.size >= LEGACY_MAX_DEVICES) {
        console.warn(`[mqtt] legacy device limit reached (${LEGACY_MAX_DEVICES}), rejecting uid=${legacyUid}`);
        return;
      }
      legacyDeviceUids.add(legacyUid);
      console.log(`[mqtt] legacy device registered: uid=${legacyUid} (total ${legacyDeviceUids.size}/${LEGACY_MAX_DEVICES})`);
    }
  }

  const deviceId = resolveInboundDeviceId(data, gatewayId);
  data._gateway_id = gatewayId;

  // 旧主题 msg_id 去重(与 v1 协议保持一致)
  if (data.msg_id && isDuplicateMsgId(data.msg_id)) {
    console.log(`[mqtt] legacy duplicate msg_id=${data.msg_id}, ignoring`);
    return;
  }

  // 任何上行消息都刷新 device_status.updated_at,防止只发事件不发遥测的设备被误判离线
  if (deviceId) {
    try {
      db_prepare_keepalive(deviceId);
    } catch (e) {
      // 设备可能不存在,忽略
    }
  }

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

// keepalive:任何上行消息都刷新 device_status.updated_at,防止非遥测设备被误判离线
function db_prepare_keepalive(deviceId) {
  const db = getDb();
  db.prepare('UPDATE device_status SET updated_at = ? WHERE device_id = ?').run(nowISO(), deviceId);
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

    // 旧主题不再自动创建 device_registry 记录,与 v1 协议保持一致
    // 未知设备需通过管理员出厂登记或 CSV 导入后才能接入
    if (serial) {
      console.warn(`[mqtt] legacy device uid=${uid} serial=${serial} not in registry, ignoring (auto-registration disabled)`);
      auditUnknownDevice(uid, data);
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

  const legacyTypes = ['up_start', 'up_stop_release', 'upper_limit', 'down_start',
    'down_stop_release', 'lower_limit', 'lock_start', 'lock_stop', 'refill_start',
    'refill_stop', 'estop', 'photo_alarm', 'rotary_switch', 'clear_alarm',
    'remote_lock', 'remote_unlock', 'config_change', 'product_type_change', 'power_on', 'power_off'];
  const numericType = Number(data.op_type);
  const opType = typeof data.op_type === 'string' && !/^\d+$/.test(data.op_type)
    ? data.op_type.slice(0, 32) : (legacyTypes[numericType] || 'unknown');
  const resultLabels = ['ok', 'interrupted', 'failed'];
  const opResult = typeof data.op_result === 'string' && !/^\d+$/.test(data.op_result)
    ? data.op_result.slice(0, 16) : (resultLabels[Number(data.op_result ?? data.result)] || 'ok');
  const durationMs = clampInt(data.duration_ms, 0, 2147483647, 0);
  const detail = typeof data.detail === 'string' ? data.detail.slice(0, 512) : '';
  const deviceState = typeof data.device_state === 'string' ? data.device_state.slice(0, 32) : '';
  const roleByte = detail.length >= 2 ? parseInt(detail.slice(0, 2), 16) : NaN;
  const role = ['main', 'sub'].includes(data.role) ? data.role : (roleByte === 1 ? 'sub' : 'main');
  const deviceTickMs = clampInt(data.device_tick_ms ?? data.ts ?? data.tick, 0, 2147483647, 0);
  // occurred_at:设备本地时间(ISO 字符串或 YYYY-MM-DD HH:MM:SS),若无则用服务器当前时间
  const occurredAt = now;
  const sourceKey = data.msg_id || (data.log_index != null ? `log:${data.log_index}` : '');
  const messageKey = sourceKey ? `${deviceUid}:${sourceKey}`.slice(0, 160) : '';

  try {
    db.prepare(`
      INSERT INTO device_operation_logs
        (device_uid, device_serial, op_type, op_result, duration_ms, detail, device_state,
         role, device_tick_ms, message_key, occurred_at, received_at)
      VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    `).run(deviceUid, deviceSerial, opType, opResult, durationMs, detail, deviceState,
      role, deviceTickMs, messageKey, occurredAt, now);

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
        role,
        device_tick_ms: deviceTickMs,
        occurred_at: occurredAt,
        received_at: now
      }
    });
  } catch (e) {
    if (!String(e.message).includes('UNIQUE constraint failed')) console.error('[MQTT] handleOpLog insert error:', e.message);
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

  const statement = uid
    ? db.prepare(`
      INSERT INTO unbound_device_status
        (device_id, uid, serial, product_type, gateway_id, online, state, alarm, ts_ms, updated_at, status_json)
      VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
      ON CONFLICT(uid) WHERE uid <> '' DO UPDATE SET
        device_id=excluded.device_id,
        serial=excluded.serial,
        product_type=excluded.product_type,
        gateway_id=excluded.gateway_id,
        online=excluded.online,
        state=excluded.state,
        alarm=excluded.alarm,
        ts_ms=excluded.ts_ms,
        updated_at=excluded.updated_at,
        status_json=excluded.status_json
    `)
    : db.prepare(`
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
    `);
  statement.run(
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
  const valid = ['none', 'collision', 'stall', 'fault', 'balance_timeout', 'safety_bar', 'overheight', 'Emergency', 'estop', 'photo_alarm'];
  return valid.includes(a) ? a : 'none';
}

function objectOrEmpty(value) {
  return value && typeof value === 'object' && !Array.isArray(value) ? value : {};
}

function normalizeState(state, direction) {
  const valid = ['idle', 'up', 'down', 'stop', 'fault', 'offline', 'rising', 'dropping', 'locked', 'refilling', 'estop', 'photo_alarm', 'maintenance_due'];
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
  const maintenance = objectOrEmpty(data.maintenance);
  const hasMaintenanceField = (key) => Object.prototype.hasOwnProperty.call(maintenance, key) || Object.prototype.hasOwnProperty.call(data, key);
  const maintenanceValue = (key) => Object.prototype.hasOwnProperty.call(maintenance, key) ? maintenance[key] : data[key];

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
    io_output_json: ioOutputJson,
    maintenance_reported: ['total_lift_count', 'maintenance_lift_count', 'maintenance_threshold', 'maintenance_count', 'last_maintenance_total', 'maintenance_due', 'usage_epoch', 'maintenance_revision'].some(hasMaintenanceField),
    total_lift_count: hasMaintenanceField('total_lift_count') ? clampInt(maintenanceValue('total_lift_count'), 0, 4294967295, 0) : undefined,
    maintenance_lift_count: hasMaintenanceField('maintenance_lift_count') ? clampInt(maintenanceValue('maintenance_lift_count'), 0, 4294967295, 0) : undefined,
    maintenance_threshold: hasMaintenanceField('maintenance_threshold') ? clampInt(maintenanceValue('maintenance_threshold'), 1, 4294967295, 5000) : undefined,
    maintenance_count: hasMaintenanceField('maintenance_count') ? clampInt(maintenanceValue('maintenance_count'), 0, 4294967295, 0) : undefined,
    last_maintenance_total: hasMaintenanceField('last_maintenance_total') ? clampInt(maintenanceValue('last_maintenance_total'), 0, 4294967295, 0) : undefined,
    maintenance_due: hasMaintenanceField('maintenance_due') ? boolOrZero(maintenanceValue('maintenance_due')) : undefined,
    usage_epoch: hasMaintenanceField('usage_epoch') ? clampInt(maintenanceValue('usage_epoch'), 0, 4294967295, 0) : undefined,
    maintenance_revision: hasMaintenanceField('maintenance_revision') ? clampInt(maintenanceValue('maintenance_revision'), 0, 4294967295, 0) : undefined
  };
}

function resolveMaintenanceStatus(current, incoming) {
  const existing = current || {};
  const previousEpoch = clampInt(existing.usage_epoch, 0, 4294967295, 0);
  const previousRevision = clampInt(existing.maintenance_revision, 0, 4294967295, 0);
  const fallback = {
    total_lift_count: clampInt(existing.total_lift_count, 0, 4294967295, 0),
    maintenance_lift_count: clampInt(existing.maintenance_lift_count, 0, 4294967295, 0),
    maintenance_threshold: clampInt(existing.maintenance_threshold, 1, 4294967295, 5000),
    maintenance_count: clampInt(existing.maintenance_count, 0, 4294967295, 0),
    last_maintenance_total: clampInt(existing.last_maintenance_total, 0, 4294967295, 0),
    maintenance_due: existing.maintenance_due ? 1 : 0,
    usage_epoch: previousEpoch,
    maintenance_revision: previousRevision
  };
  if (!incoming.maintenance_reported) return fallback;

  const incomingEpoch = incoming.usage_epoch == null ? previousEpoch : incoming.usage_epoch;
  const incomingRevision = incoming.maintenance_revision;
  // Delayed telemetry from a previous reset must never roll usage counters back.
  if (incomingEpoch < previousEpoch) return fallback;
  // Firmware ledger revisions are monotonic for every persisted maintenance
  // state. Once a device has supplied one, a delayed equal/older snapshot is
  // not allowed to undo a confirmed maintenance operation.
  if (incomingRevision != null &&
      (previousRevision !== 0 || incomingRevision !== 0) &&
      incomingRevision <= previousRevision) return fallback;
  if (incomingRevision == null && incomingEpoch === previousEpoch) {
    // Legacy firmware does not have a ledger revision. Its persistent
    // maintenance lineage is still monotonic within one reset epoch, so use
    // that lineage to reject the stale packet that formerly re-opened a
    // completed maintenance cycle.
    if ((incoming.maintenance_count != null && incoming.maintenance_count < fallback.maintenance_count) ||
        (incoming.last_maintenance_total != null && incoming.last_maintenance_total < fallback.last_maintenance_total) ||
        (incoming.total_lift_count != null && incoming.total_lift_count < fallback.total_lift_count) ||
        (incoming.maintenance_count === fallback.maintenance_count &&
         incoming.last_maintenance_total === fallback.last_maintenance_total &&
         incoming.total_lift_count === fallback.total_lift_count &&
         incoming.maintenance_lift_count != null &&
         incoming.maintenance_lift_count < fallback.maintenance_lift_count)) return fallback;
  }
  for (const key of Object.keys(fallback)) {
    if (key === 'usage_epoch' || key === 'maintenance_revision') continue;
    if (incoming[key] != null) fallback[key] = incoming[key];
  }
  fallback.usage_epoch = incomingEpoch;
  if (incomingRevision != null) fallback.maintenance_revision = incomingRevision;
  return fallback;
}

function handleStatusUpdate(deviceId, data) {
  const db = getDb();

  let status = 'normal';
  if (!data.online) status = 'offline';
  else if (data.locked) status = 'locked';
  else if (data.alarm && data.alarm !== 'none') status = 'fault';
  else if (data.maintenance_due || data.state === 'maintenance_due') status = 'maintenance';

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
    // 事务:devices 更新 + device_status upsert + 报警插入,保证三表一致性
    const statusTx = db.transaction(() => {
      const currentMaintenance = db.prepare(`SELECT total_lift_count, maintenance_lift_count, maintenance_threshold,
        maintenance_count, last_maintenance_total, maintenance_due, usage_epoch, maintenance_revision,
        run_count, run_time_s, total_run_ms, up_count, down_count, lock_count, refill_count, estop_count, photo_alarm_count
        FROM device_status WHERE device_id = ?`).get(deviceId);
      const resetPending = hasPendingShippingReset(db, deviceId);
      const resetBaseline = resetPending && currentMaintenance ? {
        ...currentMaintenance,
        total_lift_count: 0,
        maintenance_lift_count: 0,
        maintenance_count: 0,
        last_maintenance_total: 0,
        maintenance_due: 0,
        run_count: 0,
        run_time_s: 0,
        total_run_ms: 0,
        up_count: 0,
        down_count: 0,
        lock_count: 0,
        refill_count: 0,
        estop_count: 0,
        photo_alarm_count: 0,
        last_run_at: null
      } : currentMaintenance;
      const maintenanceState = resolveMaintenanceStatus(resetBaseline, resetPending ? {
        ...data,
        total_lift_count: 0,
        maintenance_lift_count: 0,
        maintenance_count: 0,
        last_maintenance_total: 0,
        maintenance_due: 0
      } : data);
      Object.assign(data, maintenanceState);
      if (currentMaintenance && !resetPending) {
        for (const key of ['run_count', 'run_time_s', 'total_run_ms', 'up_count', 'down_count', 'lock_count',
          'refill_count', 'estop_count', 'photo_alarm_count']) {
          data[key] = Math.max(Number(currentMaintenance[key] || 0), Number(data[key] || 0));
        }
      }
      if (resetPending) {
        for (const key of ['run_count', 'run_time_s', 'total_run_ms', 'up_count', 'down_count', 'lock_count',
          'refill_count', 'estop_count', 'photo_alarm_count', 'total_lift_count', 'maintenance_lift_count',
          'maintenance_count', 'last_maintenance_total']) data[key] = 0;
        data.maintenance_due = 0;
        data.last_run_at = null;
      }
      db.prepare(`UPDATE devices SET
        name = COALESCE(NULLIF(?, ''), name),
        model = COALESCE(NULLIF(?, ''), model),
        uid = COALESCE(NULLIF(?, ''), uid),
        gateway_id = COALESCE(NULLIF(?, ''), gateway_id),
        product_type = COALESCE(NULLIF(?, ''), product_type)
        WHERE device_id = ?`)
        .run(data.name || '', data.model || '', data.uid || '', data.gateway_id || '', data.product_type || '', deviceId);

      db.prepare(`
        INSERT INTO device_status (device_id, online, locked, state, alarm,
          height_left_mm, height_right_mm, height_diff_mm, run_count, run_time_s, uptime_s, ts_ms, updated_at,
          direction, upper_limit, lower_limit, stall, collision_up, collision_down, alarm_code, csq, dtu_state,
          left_pulse, right_pulse, left_up_collision, right_up_collision, left_down_collision, right_down_collision,
          buzzer_on,
          rotary_switch, up_count, down_count, lock_count, refill_count, estop_count, photo_alarm_count,
           total_run_ms, last_run_at, io_input_json, io_output_json,
           total_lift_count, maintenance_lift_count, maintenance_threshold, maintenance_count,
           last_maintenance_total, maintenance_due, usage_epoch, maintenance_revision)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?,
                ?, ?, ?, ?, ?, ?, ?, ?, ?,
                ?, ?, ?, ?, ?, ?, ?,
                ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
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
           io_input_json=excluded.io_input_json, io_output_json=excluded.io_output_json,
           total_lift_count=excluded.total_lift_count, maintenance_lift_count=excluded.maintenance_lift_count,
           maintenance_threshold=excluded.maintenance_threshold, maintenance_count=excluded.maintenance_count,
           last_maintenance_total=excluded.last_maintenance_total, maintenance_due=excluded.maintenance_due,
           usage_epoch=excluded.usage_epoch, maintenance_revision=excluded.maintenance_revision
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
         data.io_output_json || '{}',
         data.total_lift_count,
         data.maintenance_lift_count,
         data.maintenance_threshold,
         data.maintenance_count,
         data.last_maintenance_total,
         data.maintenance_due,
         data.usage_epoch,
         data.maintenance_revision
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
    });
    statusTx();
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
    // 使用新的命令状态机:succeeded / rejected / failed
    // 旧固件返回的 event/result 需要映射到新状态
    let finalStatus = mapLegacyResultToStatus(response.cmd, response.result);
    const cmdRow = db.prepare('SELECT device_id, cmd, purpose, operator_id, operator_name FROM command_queue WHERE msg_id = ?').get(response.msg_id);
    const command = cmdRow?.cmd || response.cmd;
    if (finalStatus === 'succeeded' && command === 'reset_usage' && cmdRow?.purpose === 'shipping_reset'
        && !normalizeTelemetry(response || {}).maintenance_reported) {
      finalStatus = 'failed';
    }
    const storedResult = finalStatus === 'succeeded' && command === 'reset_usage'
      ? 'usage_reset'
      : (finalStatus === 'succeeded' && command === 'maintenance_done' ? 'maintenance_done' : (response.result || finalStatus));
    const update = db.prepare(
      "UPDATE command_queue SET status = ?, result = ?, responded_at = ? WHERE msg_id = ? AND status IN ('pending', 'sent')"
    ).run(finalStatus, storedResult, nowISO(), response.msg_id);

    // 只在收到成功响应后才更新设备状态
    if (update.changes > 0) {
      applyCommandResultToDevice(cmdRow?.device_id || deviceId, command, finalStatus, response, response.msg_id);
      if (cmdRow) advanceShippingResetChain(db, cmdRow, finalStatus);
    }
  }

  broadcastToClients({
    type: 'command_response',
    device_id: deviceId,
    data: response
  });
}

// 旧固件 event/result 映射到新命令状态机
function mapLegacyResultToStatus(cmd, result) {
  if (result === 'succeeded') return 'succeeded';
  if (result === 'rejected') return 'rejected';
  if (result === 'failed') return 'failed';
  // 成功类结果
  const successResults = [
    'pong', 'reported', 'locked', 'unlocked', 'admin_entered', 'admin_exited',
    'fault_cleared', 'admin_jog_ok', 'maintenance_done', 'usage_reset', 'rebooting',
    'alarm_cleared', 'config_set', 'config_reported',
    'buzzer_on', 'buzzer_off', 'renamed'
  ];
  // 拒绝类结果
  const rejectedResults = [
    'admin_denied', 'fault_clear_denied', 'admin_jog_denied',
    'alarm_clear_denied', 'config_set_denied', 'buzzer_denied'
  ];

  if (successResults.includes(result)) return 'succeeded';
  if (rejectedResults.includes(result)) return 'rejected';
  return 'failed';
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

// 查找设备的 chip_uid(优先从 device_registry 查,其次从 devices 表查)
function getChipUidForDevice(deviceId) {
  try {
    const db = getDb();
    // 先从 devices 表查 uid
    const device = db.prepare('SELECT uid FROM devices WHERE device_id = ?').get(deviceId);
    if (device && device.uid) return normalizeUid(device.uid);
    // 再从 device_registry 查(按 serial 或 bound_device_id)
    const registry = db.prepare('SELECT uid FROM device_registry WHERE serial = ? OR bound_device_id = ?').get(deviceId, deviceId);
    if (registry && registry.uid) return normalizeUid(registry.uid);
  } catch (e) {
    // ignore
  }
  return '';
}

// 发送命令:优先使用 v1 协议,回退到旧主题
// 关键改动:不再在命令发送时立即修改设备状态,只更新命令队列为 sent
function sendCommand(deviceId, cmd, msgId, extra = {}) {
  if (!mqttClient || !mqttClient.connected) {
    console.warn('[MQTT] Not connected, cannot send command');
    return false;
  }

  const wireCmd = normalizeCommand(cmd);
  const chipUid = getChipUidForDevice(deviceId);

  if (chipUid) {
    // v1 协议:向 {chip_uid}/down 发布,QoS 1,retained=false
    // args 提取:优先用 extra.args,否则用 extra 本身(去掉 operator 等非命令参数)
    const { operator, args, purpose, password, ...cmdArgs } = extra;
    const payload = {
      v: 1,
      type: 'command',
      target_uid: chipUid,
      msg_id: msgId,
      cmd: wireCmd,
      args: args || cmdArgs || {},
      operator: operator || '',
      created_at: nowISO()
    };
    // The large-scissor firmware reads admin_enter.password from the command
    // root. Keep this credential server-side; it is never sent by the browser.
    if (wireCmd === 'admin_enter' && typeof extra.password === 'string') {
      payload.password = extra.password;
    }
    const success = publishV1Down(chipUid, payload);
    if (success) {
      scheduleRedundantCommandPublishes(chipUid, payload);
      // 更新命令队列状态为 sent(不修改设备状态)
      return true;
    }
  }

  // 旧主题回退
  if (LEGACY_ENABLED) {
    const topic = commandTopicFor(deviceId);
    const { operator, args, purpose, password, ...legacyArgs } = extra;
    const payload = JSON.stringify({
      type: 'command',
      device: deviceId,
      cmd: wireCmd,
      command: wireCmd,
      msg_id: msgId,
      ...(args || legacyArgs)
    });
    mqttClient.publish(topic, payload, { qos: MQTT_QOS, retain: false }, (err) => {
      if (err) {
        getDb().prepare("UPDATE command_queue SET status = 'failed', result = ? WHERE msg_id = ? AND status IN ('pending','sent')")
          .run(`publish_failed: ${err.message}`.slice(0, 256), msgId);
      } else updateCommandSent(msgId);
    });
    console.log(`[MQTT] TX (legacy) ${topic}: ${payload}`);
    return true;
  }

  console.warn(`[MQTT] Cannot send command to ${deviceId}: no chip_uid and legacy disabled`);
  return false;
}

function createInternalMsgId(prefix = 'cmd') {
  return `${prefix}_${Date.now().toString(36)}${Math.random().toString(36).slice(2, 10)}`;
}

// A deferred reset is created only for an offline shipping initialization.
// The atomic purpose transition is the durable one-shot guard: later packets
// cannot dispatch it again because it is no longer in the deferred state.
function dispatchPendingShippingReset(deviceId) {
  if (shippingResetDispatchLocks.has(deviceId)) return;
  shippingResetDispatchLocks.add(deviceId);
  try {
    const db = getDb();
    const deferred = db.prepare(`
      SELECT q.msg_id, q.chip_uid, q.operator_id, q.operator_name, d.product_type
      FROM command_queue q
      JOIN devices d ON d.device_id = q.device_id
      WHERE q.device_id = ? AND q.purpose = 'shipping_reset_deferred'
        AND q.status = 'pending'
      ORDER BY q.id DESC LIMIT 1
    `).get(deviceId);
    if (!deferred) return;

    const isLargeScissor = deferred.product_type === 'large_scissor';
    const nextCmd = isLargeScissor ? 'admin_enter' : 'reset_usage';
    const nextPurpose = isLargeScissor ? 'shipping_reset_admin_enter' : 'shipping_reset';
    const claimed = db.prepare(`
      UPDATE command_queue SET cmd = ?, purpose = ?
      WHERE msg_id = ? AND purpose = 'shipping_reset_deferred' AND status = 'pending'
    `).run(nextCmd, nextPurpose, deferred.msg_id);
    if (claimed.changes !== 1) return;

    const extra = isLargeScissor
      ? { password: process.env.LIFT_IOT_ADMIN_PASSWORD || '123456' }
      : {};
    if (!sendCommand(deviceId, nextCmd, deferred.msg_id, extra)) {
      // Keep pending for the next valid device message instead of converting a
      // one-shot deferred command into a terminal failure because of a brief
      // broker publishing outage.
      db.prepare(`UPDATE command_queue SET cmd = ?, purpose = 'shipping_reset_deferred'
        WHERE msg_id = ? AND status = 'pending'`)
        .run(isLargeScissor ? 'admin_enter' : 'reset_usage', deferred.msg_id);
      return;
    }
    console.log(`[CMD] dispatched deferred shipping reset device=${deviceId} msg_id=${deferred.msg_id} cmd=${nextCmd}`);
  } catch (e) {
    console.error(`[CMD] deferred shipping reset dispatch failed device=${deviceId}: ${e.message}`);
  } finally {
    shippingResetDispatchLocks.delete(deviceId);
  }
}

function hasPendingShippingReset(db, deviceId) {
  return !!db.prepare(`
    SELECT 1 FROM command_queue
    WHERE device_id = ?
      AND purpose IN ('shipping_reset_deferred', 'shipping_reset', 'shipping_reset_admin_enter', 'shipping_reset_admin_exit')
      AND status IN ('pending', 'sent')
    LIMIT 1
  `).get(deviceId);
}

// Continue the server-side shipping reset workflow only after the previous
// command has a successful device response. This is intentionally kept out of
// the public API so the MCU admin password never reaches a browser.
function advanceShippingResetChain(db, cmdRow, result) {
  if (!cmdRow) return;
  if (cmdRow.purpose === 'shipping_reset_admin_enter' && cmdRow.cmd === 'admin_enter'
      && result === 'succeeded') {
    const active = db.prepare(`
      SELECT msg_id FROM command_queue
      WHERE device_id = ? AND purpose = 'shipping_reset' AND status IN ('pending', 'sent')
      ORDER BY id DESC LIMIT 1
    `).get(cmdRow.device_id);
    if (active) return;
    const msgId = createInternalMsgId('ship_reset');
    const now = nowISO();
    db.prepare(`
      INSERT INTO command_queue
        (device_id, cmd, msg_id, status, created_at, chip_uid, operator_id, operator_name,
         args_json, attempts, max_attempts, timeout_at, purpose)
      SELECT device_id, 'reset_usage', ?, 'pending', ?, chip_uid, operator_id, operator_name,
             '{}', 0, max_attempts, NULL, 'shipping_reset'
      FROM command_queue WHERE msg_id = ?
    `).run(msgId, now, cmdRow.msg_id);
    if (!sendCommand(cmdRow.device_id, 'reset_usage', msgId, {})) {
      db.prepare("UPDATE command_queue SET status = 'failed', result = 'MQTT not connected' WHERE msg_id = ?").run(msgId);
    }
    return;
  }
  if (cmdRow.purpose === 'shipping_reset' && cmdRow.cmd === 'reset_usage'
      && ['succeeded', 'rejected', 'failed'].includes(result)) {
    // applyCommandResultToDevice has already removed old command history and
    // recorded the completed reset before admin_exit is sent.
    const msgId = createInternalMsgId('ship_exit');
    const now = nowISO();
    db.prepare(`
      INSERT INTO command_queue
        (device_id, cmd, msg_id, status, created_at, chip_uid, operator_id, operator_name,
         args_json, attempts, max_attempts, timeout_at, purpose)
      SELECT device_id, 'admin_exit', ?, 'pending', ?, chip_uid, operator_id, operator_name,
             '{}', 0, max_attempts, NULL, 'shipping_reset_admin_exit'
      FROM command_queue WHERE msg_id = ?
    `).run(msgId, now, cmdRow.msg_id);
    if (!sendCommand(cmdRow.device_id, 'admin_exit', msgId, {})) {
      db.prepare("UPDATE command_queue SET status = 'failed', result = 'MQTT not connected' WHERE msg_id = ?").run(msgId);
    }
  }
}

// 更新命令队列状态为 sent,记录发送时间和尝试次数
function updateCommandSent(msgId) {
  try {
    const db = getDb();
    const now = nowISO();
    const timeoutAt = nowISO(new Date(Date.now() + CMD_TIMEOUT_MS));
    db.prepare('UPDATE command_queue SET status = ?, sent_at = ?, last_attempt_at = ?, timeout_at = ?, attempts = attempts + 1 WHERE msg_id = ? AND status IN (?, ?)')
      .run('sent', now, now, timeoutAt, msgId, 'pending', 'sent');
    console.log(`[CMD] publish window started msg_id=${msgId} sent_at=${now} timeout_at=${timeoutAt} at_ms=${Date.now()}`);
  } catch (e) {
    console.error(`[CMD] failed to persist publish timing msg_id=${msgId}: ${e.message}`);
  }
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
    // 使用 device_bindings 多对多绑定关系鉴权
    const row = getDb().prepare(
      "SELECT 1 FROM device_bindings WHERE device_id = ? AND user_id = ? AND status = 'active'"
    ).get(message.device_id, client.user.id);
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
    v1_prefix: v1Prefix,
    gateway_id: defaultGatewayId,
    device_id: defaultDeviceId,
    qos: MQTT_QOS,
    legacy_enabled: LEGACY_ENABLED,
    legacy_max_devices: LEGACY_MAX_DEVICES,
    v1_up_topic: `${v1Prefix}/devices/+/up`,
    v1_down_topic_template: `${v1Prefix}/devices/{chip_uid}/down`,
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
    const now = nowISO();

    // 查找已超时的 pending 或 sent 命令
    // pending 也纳入检查:防止 retryCommand 把状态置为 pending 后 MQTT 断连导致卡死
    const stale = db.prepare(`
      SELECT msg_id, device_id, cmd, chip_uid, attempts, max_attempts, args_json
      FROM command_queue
      WHERE status = 'sent'
        AND timeout_at IS NOT NULL AND timeout_at < ?
    `).all(now);

    for (const row of stale) {
      if (row.attempts < row.max_attempts) {
        // 重试:重置为 pending,延迟后重新发送
        retryCommand(row);
      } else {
        // 超过最大重试次数,标记为 timeout
        db.prepare("UPDATE command_queue SET status = 'timeout', result = 'timeout' WHERE msg_id = ? AND status IN ('pending','sent')")
          .run(row.msg_id);
        cancelRedundantCommandPublishes(row.msg_id, 'timeout');
        console.log(`[CMD] timeout reason=no_matching_response device=${row.device_id} uid=${row.chip_uid || '-'} cmd=${row.cmd} msg_id=${row.msg_id} attempts=${row.attempts}`);
        broadcastToClients({
          type: 'command_response',
          device_id: row.device_id,
          data: { cmd: row.cmd, msg_id: row.msg_id, result: 'timeout', reason: 'no response after max attempts' }
        });
      }
    }
  }, CHECK_INTERVAL_MS);
}

// 命令重试:重置为 pending,重置 timeout_at,延迟后重新发送(带原始参数)
function retryCommand(cmdRow) {
  const db = getDb();
  // 重置超时窗口:从现在起重新计算 CMD_TIMEOUT_MS
  db.prepare(
    "UPDATE command_queue SET status = 'pending', timeout_at = NULL WHERE msg_id = ? AND status = 'sent'"
  ).run(cmdRow.msg_id);
  console.log(`[CMD] retrying ${cmdRow.cmd} to ${cmdRow.device_id} (msg_id=${cmdRow.msg_id}, attempt ${cmdRow.attempts + 1}/${cmdRow.max_attempts})`);

  // 解析原始命令参数,确保重试时携带完整 args
  let extra = {};
  try {
    if (cmdRow.args_json) extra = JSON.parse(cmdRow.args_json) || {};
  } catch (e) {
    console.warn(`[CMD] failed to parse args_json for msg_id=${cmdRow.msg_id}: ${e.message}`);
  }

  setTimeout(() => {
    if (!sendCommand(cmdRow.device_id, cmdRow.cmd, cmdRow.msg_id, extra)) {
      db.prepare("UPDATE command_queue SET status = 'failed', result = 'MQTT not connected' WHERE msg_id = ? AND status = 'pending'")
        .run(cmdRow.msg_id);
    }
  }, CMD_RETRY_DELAY_MS);
}

module.exports = {
  connect,
  sendCommand,
  addWsClient,
  broadcastToClients,
  isConnected,
  getStatus,
  _test: {
    flattenV1Payload,
    parseMqttPayload,
    parseMqttStreamChunk,
    resetMqttStreamCarry,
    handleV1Message,
    canClientReceive,
    normalizeTelemetry,
    handleStatusUpdate,
    resolveMaintenanceStatus
    , applyCommandResultToDevice
    , dispatchPendingShippingReset
    , hasPendingShippingReset
  }
};
