/*
 * Tests the MQTT v1 wire contracts used by all four shipping firmware variants.
 * It starts an isolated Web/API instance with a temporary SQLite database, then
 * simulates each firmware publishing its native identity fields and receiving a
 * website command over MQTT. No production device, database, or topic is used.
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
const topicPrefix = process.env.MQTT_TOPIC_PREFIX || `gaochang/test-firmware-matrix-${runId}`;
const v1Prefix = process.env.MQTT_V1_PREFIX || `${topicPrefix}/v1`;
const port = Number(process.env.TEST_PORT || (4300 + (process.pid % 500)));
const dbPath = path.join(os.tmpdir(), `gaochang-firmware-matrix-${runId}.db`);
const firmwareRoot = path.resolve(projectRoot, '..');

const firmware = [
  { productType: 'double_post', folder: 'GC-Two_Pillars', identity: 'both' },
  { productType: 'small_scissor', folder: 'GC_Small_Scissor', identity: 'uid' },
  { productType: 'thin_scissor', folder: 'GC_Thin_Scissor', identity: 'both' },
  { productType: 'large_scissor', folder: 'GC_Big_Scissor', identity: 'chip_uid' }
].map((item, index) => {
  const uid = crypto.createHash('sha256').update(`${runId}-${item.productType}`).digest('hex').slice(0, 24);
  return {
    ...item,
    uid,
    deviceId: `firmware-matrix-${item.productType}-${runId}`,
    runCount: 100 + index,
    upTopic: `${v1Prefix}/devices/${uid}/up`,
    downTopic: `${v1Prefix}/devices/${uid}/down`
  };
});

let server;
let passCount = 0;
let failCount = 0;

function log(message) {
  console.log(`[firmware-matrix] ${message}`);
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

async function waitUntil(predicate, timeoutMs = 8000) {
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

function assertFirmwareSource(config) {
  const headerPath = path.join(firmwareRoot, config.folder, 'APP', 'Inc', 'app_tas_dtu.h');
  const sourcePath = path.join(firmwareRoot, config.folder, 'APP', 'Src', 'app_tas_dtu.c');
  const header = fs.readFileSync(headerPath, 'utf8');
  const source = fs.readFileSync(sourcePath, 'utf8');
  check(`${config.productType} broker domain`, /#define\s+TAS_DTU_BROKER_HOST\s+"mqtt\.gclift\.net"/.test(header));
  check(`${config.productType} MQTT port`, /#define\s+TAS_DTU_BROKER_PORT\s+1883U/.test(header));
  check(`${config.productType} v1 up topic`, /TAS_DTU_TOPIC_TELEMETRY\s+"gaochang\/lift\/v1\/devices\/\{chip_uid\}\/up"/.test(header));
  check(`${config.productType} v1 down topic`, /TAS_DTU_TOPIC_COMMAND_SUB\s+"gaochang\/lift\/v1\/devices\/\{chip_uid\}\/down"/.test(header));
  check(`${config.productType} command response implementation`, source.includes('\\"type\\":\\"command_response\\"'));
}

function telemetry(config) {
  const payload = {
    type: 'telemetry',
    device: config.deviceId,
    product_type: config.productType,
    seq: config.runCount,
    tick: Date.now(),
    uptime_ms: 720000,
    state: 'idle',
    locked: 0,
    maintenance_due: 0,
    io_input: {
      btn_up: 0, btn_down: 0, btn_lock: 0, estop: 0,
      upper_limit: 0, lower_limit: 0, refill: 0, photoelectric: 0,
      ...(config.productType === 'large_scissor' ? { rotary: 1 } : {})
    },
    io_output: { motor: 0, drop_valve: 0, air_valve: 0 },
    safety: { alarm: 'none', alarm_code: 0, upper: 0, lower: 0, estop: 0, photoelectric: 0 },
    stats: { up: config.runCount, down: config.runCount - 1, lock: 2, refill: 1, estop: 0, photo_alarm: 0, total_run_ms: 600000 },
    runtime: { total_ms: 600000, current_ms: 0, run_count: config.runCount },
    maintenance: {
      total_lift_count: config.runCount,
      maintenance_lift_count: config.runCount,
      maintenance_threshold: 5000,
      maintenance_count: 0,
      last_maintenance_total: 0,
      maintenance_due: 0,
      usage_epoch: 0
    },
    dtu: { state: 'MQTT CONNECTED', csq: 20 }
  };

  if (config.identity === 'uid' || config.identity === 'both') payload.uid = config.uid;
  if (config.identity === 'chip_uid' || config.identity === 'both') payload.chip_uid = config.uid;
  if (config.productType === 'large_scissor') payload.rotary_switch = 'main';
  return payload;
}

function commandResponse(config, command, msgId) {
  const payload = {
    v: 1,
    type: 'command_response',
    cmd: command,
    msg_id: msgId,
    result: 'succeeded',
    event: `${command}_ok`
  };
  if (config.identity === 'uid' || config.identity === 'both') payload.uid = config.uid;
  if (config.identity === 'chip_uid' || config.identity === 'both') payload.chip_uid = config.uid;
  return payload;
}

function connectDevice(config) {
  return new Promise((resolve, reject) => {
    const client = mqtt.connect(broker, {
      clientId: `gc-lift-${config.uid}`,
      clean: true,
      connectTimeout: 10000,
      reconnectPeriod: 0
    });
    const timer = setTimeout(() => reject(new Error(`${config.productType} MQTT connection timeout`)), 12000);
    client.once('connect', () => {
      client.subscribe(config.downTopic, { qos: 1 }, (error) => {
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

function waitForCommand(client, topic, timeoutMs = 8000) {
  return new Promise((resolve) => {
    const handler = (receivedTopic, message) => {
      if (receivedTopic !== topic) return;
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

async function startServer() {
  const env = {
    ...process.env,
    DB_PATH: dbPath,
    PORT: String(port),
    MQTT_BROKER: broker,
    MQTT_TOPIC_PREFIX: topicPrefix,
    MQTT_V1_PREFIX: v1Prefix,
    MQTT_GATEWAY_ID: `firmware-matrix-${runId}`,
    MQTT_DEVICE_ID: `firmware-matrix-server-${runId}`,
    MQTT_QOS: '1',
    JWT_SECRET: `firmware-matrix-${runId}-0123456789`,
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

async function registerDevice(config, token) {
  const registry = await api('POST', '/api/admin/registry', {
    serial: config.deviceId,
    uid: config.uid,
    product_type: config.productType,
    model: `firmware-matrix-${config.productType}`,
    bind_code: `TEST-${config.uid.slice(0, 8)}`
  }, token);
  check(`${config.productType} registration`, registry.status === 200, String(registry.status));

  const created = await api('POST', '/api/devices', {
    device_id: config.deviceId,
    name: `Test ${config.productType}`,
    model: `firmware-matrix-${config.productType}`,
    product_type: config.productType,
    uid: config.uid
  }, token);
  check(`${config.productType} Web device creation`, created.status === 200, String(created.status));
}

async function runFirmwareCase(config, token) {
  const client = await connectDevice(config);
  try {
    await registerDevice(config, token);
    await new Promise((resolve, reject) => client.publish(config.upTopic, JSON.stringify(telemetry(config)), { qos: 1 }, (error) => error ? reject(error) : resolve()));

    const parsed = await waitUntil(async () => {
      const response = await api('GET', `/api/devices/${encodeURIComponent(config.deviceId)}`, undefined, token);
      return response.status === 200 && response.data.online === true && response.data.run_count === config.runCount;
    });
    check(`${config.productType} telemetry parsed by Web`, parsed);

    const current = await api('GET', `/api/devices/${encodeURIComponent(config.deviceId)}`, undefined, token);
    check(`${config.productType} product type retained`, current.data.product_type === config.productType);
    check(`${config.productType} IO fields retained`, typeof current.data.io_input_json === 'string' && current.data.io_input_json.includes('upper_limit'));

    const commandPromise = waitForCommand(client, config.downTopic);
    const sent = await api('POST', `/api/commands/query/${encodeURIComponent(config.deviceId)}`, undefined, token);
    const command = await commandPromise;
    check(`${config.productType} website sends command`, sent.status === 200 && command?.cmd === 'get_status');
    check(`${config.productType} command targets UID`, command?.target_uid === config.uid && !!command?.msg_id);

    if (command?.msg_id) {
      await new Promise((resolve, reject) => client.publish(
        config.upTopic,
        JSON.stringify(commandResponse(config, command.cmd, command.msg_id)),
        { qos: 1 },
        (error) => error ? reject(error) : resolve()
      ));
      const acknowledged = await waitUntil(async () => {
        const response = await api('GET', `/api/commands/status/${encodeURIComponent(command.msg_id)}`, undefined, token);
        return response.status === 200 && response.data.status === 'succeeded';
      });
      check(`${config.productType} firmware ACK reaches Web`, acknowledged);
    }
  } finally {
    client.end(true);
  }
}

async function cleanup() {
  if (server && !server.killed) server.kill();
  await sleep(300);
  for (const suffix of ['', '-shm', '-wal']) {
    try { fs.unlinkSync(`${dbPath}${suffix}`); } catch (_) { /* already removed */ }
  }
}

async function run() {
  firmware.forEach(assertFirmwareSource);
  await startServer();
  const login = await api('POST', '/api/auth/login', { username: 'admin', password: 'admin123' });
  check('isolated admin login', login.status === 200 && !!login.data.token);
  if (!login.data.token) throw new Error('isolated admin login failed');

  for (const config of firmware) await runFirmwareCase(config, login.data.token);
  log(`SUMMARY total=${passCount + failCount} passed=${passCount} failed=${failCount}`);
  if (failCount) process.exitCode = 1;
}

run().catch((error) => {
  console.error('[firmware-matrix] ERROR', error);
  process.exitCode = 1;
}).finally(cleanup);
