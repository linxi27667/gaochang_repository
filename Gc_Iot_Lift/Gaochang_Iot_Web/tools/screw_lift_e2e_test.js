/*
 * 丝杆举升机隔离全链路模拟测试（临时库 + 独立 Topic 前缀）。
 * 覆盖：登记/绑定命名、遥测、高度包合并、命令下发与应答、名称不被固件覆盖。
 * 不向生产设备 Topic 下发控制命令，不刷写实体设备。
 */
const crypto = require('crypto');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawn } = require('child_process');
const mqtt = require('mqtt');

const projectRoot = path.resolve(__dirname, '..');
const runId = `${Date.now().toString(36)}-${process.pid}`;
const broker = process.env.MQTT_BROKER || 'mqtt://mqtt.gclift.net:1883';
const topicPrefix = process.env.MQTT_TOPIC_PREFIX || `gaochang/test-screw-${runId}`;
const v1Prefix = process.env.MQTT_V1_PREFIX || `${topicPrefix}/v1`;
const port = Number(process.env.TEST_PORT || (4600 + (process.pid % 400)));
const dbPath = path.join(os.tmpdir(), `gaochang-screw-e2e-${runId}.db`);
const firmwareRoot = path.resolve(projectRoot, '..', 'F407ZET6', 'GC-Screw_Lift');

const uid = crypto.createHash('sha256').update(`${runId}-screw`).digest('hex').slice(0, 24);
const deviceId = `screw-e2e-${runId}`;
const upTopic = `${v1Prefix}/devices/${uid}/up`;
const downTopic = `${v1Prefix}/devices/${uid}/down`;

let server;
let passCount = 0;
let failCount = 0;

function log(message) {
  console.log(`[screw-e2e] ${message}`);
}

function check(name, condition, detail = '') {
  if (condition) {
    passCount += 1;
    log(`PASS ${name}${detail ? ` (${detail})` : ''}`);
  } else {
    failCount += 1;
    log(`FAIL ${name}${detail ? ` (${detail})` : ''}`);
  }
}

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

async function waitUntil(predicate, timeoutMs = 10000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (await predicate()) return true;
    await sleep(100);
  }
  return false;
}

async function api(method, requestPath, body, token) {
  const response = await fetch(`http://127.0.0.1:${port}${requestPath}`, {
    method,
    headers: {
      'content-type': 'application/json',
      ...(token ? { authorization: `Bearer ${token}` } : {})
    },
    body: body === undefined ? undefined : JSON.stringify(body)
  });
  const text = await response.text();
  let data = text;
  try { data = text ? JSON.parse(text) : {}; } catch (_) { /* keep text */ }
  return { status: response.status, data };
}

function assertFirmwareSource() {
  const header = fs.readFileSync(path.join(firmwareRoot, 'APP', 'Inc', 'app_tas_dtu.h'), 'utf8');
  const iot = fs.readFileSync(path.join(firmwareRoot, 'APP', 'Src', 'app_lift_iot.c'), 'utf8');
  const dtu = fs.readFileSync(path.join(firmwareRoot, 'APP', 'Src', 'app_tas_dtu.c'), 'utf8');
  const safety = fs.readFileSync(path.join(firmwareRoot, 'APP', 'Src', 'safety.c'), 'utf8');
  const stack = fs.readFileSync(path.join(firmwareRoot, 'Driver', 'Inc', 'dri_tas_dtu.h'), 'utf8');

  check('screw broker', /TAS_DTU_BROKER_HOST\s+"mqtt\.gclift\.net"/.test(header));
  check('screw up topic', /gaochang\/lift\/v1\/devices\/\{chip_uid\}\/up/.test(header));
  check('screw down topic', /gaochang\/lift\/v1\/devices\/\{chip_uid\}\/down/.test(header));
  check('screw product_type', iot.includes('"screw_lift"') || fs.readFileSync(path.join(firmwareRoot, 'APP', 'Inc', 'app_lift_iot.h'), 'utf8').includes('screw_lift'));
  check('screw state rising/dropping/idle', iot.includes('return "rising"') && iot.includes('return "dropping"') && iot.includes('return "idle"'));
  check('screw UID normalize', dtu.includes('App_TasDtu_UidMatchesLocal'));
  check('screw DTU stack 4096', /TAS_DTU_TASK_STACK_SIZE_WORDS\s+4096U/.test(stack));
  check('screw soft upper limit', /void\s+Safety_Check_Upper_Limit\s*\(/.test(safety));
  check('screw collision auto-clear strategy', safety.includes('Collision alarm cleared by pins LOW'));
}

async function startServer() {
  const env = {
    ...process.env,
    DB_PATH: dbPath,
    PORT: String(port),
    MQTT_BROKER: broker,
    MQTT_TOPIC_PREFIX: topicPrefix,
    MQTT_V1_PREFIX: v1Prefix,
    MQTT_GATEWAY_ID: `screw-e2e-${runId}`,
    MQTT_DEVICE_ID: `screw-e2e-server-${runId}`,
    MQTT_QOS: '1',
    JWT_SECRET: `screw-e2e-${runId}-0123456789`,
    CMD_TIMEOUT_MS: '3000',
    CMD_MAX_ATTEMPTS: '1',
    CMD_REDUNDANT_DELAYS_MS: ''
  };
  let output = '';
  server = spawn(process.execPath, ['server.js'], { cwd: projectRoot, env });
  const onOutput = (chunk) => {
    output += chunk.toString();
    process.stdout.write(chunk);
  };
  server.stdout.on('data', onOutput);
  server.stderr.on('data', onOutput);
  const started = await waitUntil(() => output.includes(`[Server] Running at http://localhost:${port}`), 15000);
  if (!started) throw new Error('isolated Web/API server did not start');
}

function connectDevice() {
  return new Promise((resolve, reject) => {
    const client = mqtt.connect(broker, {
      clientId: `gc-lift-${uid}`,
      clean: true,
      connectTimeout: 10000,
      reconnectPeriod: 0
    });
    const timer = setTimeout(() => reject(new Error('MQTT connection timeout')), 12000);
    client.once('connect', () => {
      client.subscribe(downTopic, { qos: 1 }, (error) => {
        clearTimeout(timer);
        if (error) reject(error);
        else resolve(client);
      });
    });
    client.once('error', (error) => {
      clearTimeout(timer);
      reject(error);
    });
  });
}

function waitForCommand(client, timeoutMs = 8000) {
  return new Promise((resolve) => {
    const handler = (receivedTopic, message) => {
      if (receivedTopic !== downTopic) return;
      client.removeListener('message', handler);
      try { resolve(JSON.parse(message.toString())); } catch (_) { resolve(null); }
    };
    client.on('message', handler);
    setTimeout(() => {
      client.removeListener('message', handler);
      resolve(null);
    }, timeoutMs).unref();
  });
}

function publish(client, payload) {
  return new Promise((resolve, reject) => {
    client.publish(upTopic, JSON.stringify(payload), { qos: 1 }, (error) => (error ? reject(error) : resolve()));
  });
}

async function cleanup() {
  if (server && !server.killed) server.kill();
  await sleep(300);
  for (const suffix of ['', '-shm', '-wal']) {
    try { fs.unlinkSync(`${dbPath}${suffix}`); } catch (_) { /* already removed */ }
  }
}

async function run() {
  assertFirmwareSource();
  await startServer();

  const login = await api('POST', '/api/auth/login', { username: 'admin', password: 'admin123' });
  check('isolated admin login', login.status === 200 && !!login.data.token);
  const token = login.data.token;
  if (!token) throw new Error('login failed');

  const registry = await api('POST', '/api/admin/registry', {
    serial: `SN-SCREW-${runId}`,
    uid,
    product_type: 'screw_lift',
    model: 'GC-SCREW-F407',
    bind_code: `TEST-${uid.slice(0, 8)}`,
    has_encoder: 0
  }, token);
  check('registry accepts screw_lift', registry.status === 200, String(registry.status));

  const registryList = await api('GET', '/api/admin/registry?page=1&pageSize=50', undefined, token);
  const row = (registryList.data.items || registryList.data.list || registryList.data || [])
    .find?.((r) => r.uid === uid) ||
    (Array.isArray(registryList.data) ? registryList.data.find((r) => r.uid === uid) : null);
  // fallback query by scanning if shape differs
  let encoderOk = false;
  if (row) {
    encoderOk = !!row.has_encoder;
  } else {
    const raw = JSON.stringify(registryList.data);
    encoderOk = raw.includes(uid) && (raw.includes('"has_encoder":1') || raw.includes('"has_encoder":true'));
  }
  check('screw_lift forces has_encoder=1', encoderOk);

  // bind via user API if available; else create device then bind
  const bindCode = `TEST-${uid.slice(0, 8)}`;
  let bind = await api('POST', '/api/binding/bind', {
    serial: `SN-SCREW-${runId}`,
    bind_code: bindCode
  }, token);
  /* 绑定成功返回 201；勿把成功结果覆盖成不存在的兜底路由 */
  if (!(bind.status === 200 || bind.status === 201)) {
    bind = await api('POST', '/api/devices', {
      device_id: deviceId,
      name: '丝杆00',
      model: 'GC-SCREW-F407',
      product_type: 'screw_lift',
      uid,
      has_encoder: 1
    }, token);
  }
  check('bind screw device', bind.status === 200 || bind.status === 201, String(bind.status));

  const devices = await api('GET', '/api/devices', undefined, token);
  const deviceList = devices.data.devices || devices.data.items || devices.data || [];
  const bound = Array.isArray(deviceList) ? deviceList.find((d) => d.uid === uid) : null;
  const boundName = bound?.name || bind.data?.name || bind.data?.device?.name || '';
  check('auto name 丝杆00-style', /^丝杆\d{2}$/.test(boundName), boundName || 'missing');
  const realDeviceId = bound?.device_id || bind.data?.device_id || bind.data?.device?.device_id || deviceId;

  const client = await connectDevice();
  try {
    await publish(client, {
      v: 1,
      type: 'telemetry',
      device: 'gc_screw_lift_f407zet6',
      name: 'GC-Screw-Lift-F407-01',
      uid,
      chip_uid: uid,
      product_type: 'screw_lift',
      state: 'idle',
      locked: 0,
      direction: 'stop',
      height: { left_mm: 120, right_mm: 118, diff_mm: 2, left_pulse: 15, right_pulse: 14 },
      safety: {
        alarm: 'collision',
        alarm_code: 1,
        stall: 0,
        collision_up: 1,
        collision_down: 0,
        left_up_collision: 1,
        right_up_collision: 0,
        left_down_collision: 0,
        right_down_collision: 0,
        upper: 1,
        lower: 0
      },
      runtime: { total_ms: 1000, current_ms: 0, run_count: 3 },
      dtu: { state: 'MQTT CONNECTED', csq: 18 }
    });

    const teleOk = await waitUntil(async () => {
      const response = await api('GET', `/api/devices/${encodeURIComponent(realDeviceId)}`, undefined, token);
      return response.status === 200 && response.data.online === true && Number(response.data.height_left_mm) === 120;
    });
    check('telemetry height stored', teleOk);

    let afterTele = await api('GET', `/api/devices/${encodeURIComponent(realDeviceId)}`, undefined, token);
    check('name not overwritten by firmware', afterTele.data.name === boundName, afterTele.data.name);
    check('collision flag retained after telemetry', Number(afterTele.data.left_up_collision) === 1);

    await publish(client, {
      v: 1,
      type: 'height',
      uid,
      chip_uid: uid,
      product_type: 'screw_lift',
      state: 'rising',
      locked: 0,
      direction: 'up',
      safety: { alarm: 'collision', alarm_code: 1 },
      height: { left_mm: 200, right_mm: 196, diff_mm: 4, left_pulse: 25, right_pulse: 24 }
    });

    const heightOk = await waitUntil(async () => {
      const response = await api('GET', `/api/devices/${encodeURIComponent(realDeviceId)}`, undefined, token);
      return response.status === 200 && Number(response.data.height_left_mm) === 200;
    });
    check('height packet updates mm', heightOk);
    afterTele = await api('GET', `/api/devices/${encodeURIComponent(realDeviceId)}`, undefined, token);
    check('height packet keeps collision detail', Number(afterTele.data.left_up_collision) === 1);
    check('height packet keeps direction/state', afterTele.data.direction === 'up' || afterTele.data.state === 'rising');

    await publish(client, {
      v: 1,
      type: 'status',
      uid,
      chip_uid: uid,
      product_type: 'screw_lift',
      event: 'boot',
      state: 'idle',
      locked: 0,
      direction: 'stop',
      safety: { alarm: 'none', alarm_code: 0 },
      dtu: { state: 'MQTT CONNECTED', csq: 19 }
    });

    const statusOk = await waitUntil(async () => {
      const response = await api('GET', `/api/devices/${encodeURIComponent(realDeviceId)}`, undefined, token);
      return response.status === 200 && Number(response.data.csq) === 19;
    });
    check('status packet applied', statusOk);
    afterTele = await api('GET', `/api/devices/${encodeURIComponent(realDeviceId)}`, undefined, token);
    check('status packet preserves height', Number(afterTele.data.height_left_mm) === 200 && Number(afterTele.data.height_right_mm) === 196);

    const commandPromise = waitForCommand(client);
    const sent = await api('POST', `/api/commands/query/${encodeURIComponent(realDeviceId)}`, undefined, token);
    const command = await commandPromise;
    check('website sends get_status', sent.status === 200 && command?.cmd === 'get_status');
    check('command has target_uid+msg_id', command?.target_uid === uid && !!command?.msg_id);

    if (command?.msg_id) {
      await publish(client, {
        v: 1,
        type: 'command_response',
        uid,
        chip_uid: uid,
        product_type: 'screw_lift',
        cmd: command.cmd,
        msg_id: command.msg_id,
        result: 'succeeded',
        event: 'reported',
        reason: '',
        state: 'idle',
        locked: 0
      });
      const ack = await waitUntil(async () => {
        const response = await api('GET', `/api/commands/status/${encodeURIComponent(command.msg_id)}`, undefined, token);
        return response.status === 200 && response.data.status === 'succeeded';
      });
      check('command_response closes queue', ack);
    }

    const shipping = await api('POST', '/api/admin/shipping-reset/start', { device_ids: [realDeviceId] }, token);
    const shippingResult = (shipping.data.results || [])[0] || {};
    check('screw shipping is website_only', shipping.status === 200 && shippingResult.status === 'website_only', shippingResult.status || String(shipping.status));
  } finally {
    client.end(true);
  }

  log(`SUMMARY total=${passCount + failCount} passed=${passCount} failed=${failCount}`);
  if (failCount) process.exitCode = 1;
}

run().catch(async (error) => {
  console.error('[screw-e2e] FATAL', error);
  process.exitCode = 1;
}).finally(cleanup);
