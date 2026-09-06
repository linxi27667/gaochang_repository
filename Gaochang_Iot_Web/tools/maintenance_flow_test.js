/*
 * Simulates the final firmware maintenance flow without a physical lift:
 * Web command -> MQTT downlink -> firmware command_response -> SQLite cache.
 *
 * The test uses an isolated database and a unique MQTT v1 prefix/device UID.
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
const topicPrefix = process.env.MQTT_TOPIC_PREFIX || `gaochang/test-maint-${runId}`;
const v1Prefix = process.env.MQTT_V1_PREFIX || `${topicPrefix}/v1`;
const port = Number(process.env.TEST_PORT || (3300 + (process.pid % 500)));
const dbPath = path.join(os.tmpdir(), `gaochang-maintenance-${runId}.db`);
const deviceId = `maintenance-test-${runId}`;
const chipUid = crypto.randomBytes(12).toString('hex');
const upTopic = `${v1Prefix}/devices/${chipUid}/up`;
const downTopic = `${v1Prefix}/devices/${chipUid}/down`;

let server;
let deviceClient;
let passCount = 0;
let failCount = 0;

function log(message) {
  console.log(`[maintenance-test] ${message}`);
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

async function waitUntil(name, predicate, timeoutMs = 8000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    try {
      if (await predicate()) return true;
    } catch (error) {
      // The server can still be starting; keep polling until the deadline.
    }
    await sleep(100);
  }
  check(name, false, 'timeout');
  return false;
}

async function request(method, requestPath, body, token) {
  const headers = { 'content-type': 'application/json' };
  if (token) headers.authorization = `Bearer ${token}`;
  const response = await fetch(`http://127.0.0.1:${port}${requestPath}`, {
    method,
    headers,
    body: body === undefined ? undefined : JSON.stringify(body)
  });
  const text = await response.text();
  let data = text;
  try { data = text ? JSON.parse(text) : {}; } catch (_) { /* keep text */ }
  return { status: response.status, data };
}

function publishUp(payload) {
  return new Promise((resolve, reject) => {
    deviceClient.publish(upTopic, JSON.stringify(payload), { qos: 1, retain: false }, (error) => {
      if (error) reject(error);
      else resolve();
    });
  });
}

function waitForDown(timeoutMs = 8000) {
  return new Promise((resolve) => {
    const handler = (topic, message) => {
      if (topic !== downTopic) return;
      deviceClient.removeListener('message', handler);
      try { resolve(JSON.parse(message.toString())); } catch (_) { resolve(null); }
    };
    deviceClient.on('message', handler);
    setTimeout(() => {
      deviceClient.removeListener('message', handler);
      resolve(null);
    }, timeoutMs).unref();
  });
}

function maintenanceSnapshot(overrides = {}) {
  return {
    total_lift_count: 5000,
    maintenance_lift_count: 5000,
    maintenance_threshold: 5000,
    maintenance_count: 0,
    last_maintenance_total: 0,
    maintenance_due: 1,
    usage_epoch: 0,
    ...overrides
  };
}

function telemetry(snapshot, msgId) {
  return {
    v: 1,
    type: 'telemetry',
    chip_uid: chipUid,
    uid: chipUid,
    product_type: 'small_scissor',
    msg_id: msgId,
    online: true,
    locked: false,
    state: snapshot.maintenance_due ? 'maintenance_due' : 'idle',
    alarm: 'none',
    maintenance: snapshot,
    ts_ms: Date.now()
  };
}

function commandResponse(command, result, snapshot, msgId, reason = '') {
  return {
    v: 1,
    type: 'command_response',
    chip_uid: chipUid,
    uid: chipUid,
    product_type: 'small_scissor',
    cmd: command,
    msg_id: msgId,
    result,
    reason,
    event: result === 'maintenance_done' ? 'maintenance_done_ok' : result,
    ...(snapshot ? { maintenance: snapshot } : {})
  };
}

async function startServer() {
  const env = {
    ...process.env,
    DB_PATH: dbPath,
    PORT: String(port),
    MQTT_BROKER: broker,
    MQTT_TOPIC_PREFIX: topicPrefix,
    MQTT_V1_PREFIX: v1Prefix,
    MQTT_GATEWAY_ID: 'maintenance-test-gateway',
    MQTT_DEVICE_ID: deviceId,
    MQTT_QOS: '1',
    JWT_SECRET: `maintenance-test-secret-${runId}-0123456789`,
    CMD_TIMEOUT_MS: '3000',
    CMD_MAX_ATTEMPTS: '1',
    CMD_REDUNDANT_DELAYS_MS: ''
  };
  server = spawn(process.execPath, ['server.js'], { cwd: projectRoot, env });
  let output = '';
  const onData = (chunk) => {
    output += chunk.toString();
    process.stdout.write(chunk);
  };
  server.stdout.on('data', onData);
  server.stderr.on('data', onData);
  const started = await waitUntil('backend starts', () => output.includes(`[Server] Running at http://localhost:${port}`), 15000);
  if (!started) throw new Error('backend did not start');
}

async function connectDevice() {
  deviceClient = mqtt.connect(broker, {
    clientId: `maintenance-device-${runId}`,
    clean: true,
    connectTimeout: 10000,
    reconnectPeriod: 0
  });
  await new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error('device MQTT connection timeout')), 12000);
    deviceClient.once('connect', () => { clearTimeout(timer); resolve(); });
    deviceClient.once('error', (error) => { clearTimeout(timer); reject(error); });
  });
  await new Promise((resolve, reject) => {
    deviceClient.subscribe(downTopic, { qos: 1 }, (error) => error ? reject(error) : resolve());
  });
  log(`device connected uid=${chipUid}`);
}

async function run() {
  await startServer();
  await connectDevice();

  const login = await request('POST', '/api/auth/login', { username: 'admin', password: 'admin123' });
  check('admin login', login.status === 200 && login.data.token);
  const token = login.data.token;

  const serial = deviceId;
  const registry = await request('POST', '/api/admin/registry', {
    serial,
    uid: chipUid,
    product_type: 'small_scissor',
    model: 'maintenance-flow-test',
    bind_code: `BIND-${runId}`
  }, token);
  check('register simulated device', registry.status === 200, JSON.stringify(registry.data));

  const created = await request('POST', '/api/devices', {
    device_id: deviceId,
    name: 'Maintenance flow test device',
    model: 'maintenance-flow-test',
    product_type: 'small_scissor',
    uid: chipUid
  }, token);
  check('create simulated device', created.status === 200, JSON.stringify(created.data));

  await publishUp(telemetry(maintenanceSnapshot(), `telemetry-${runId}-before`));
  await waitUntil('device telemetry reaches backend', async () => {
    const response = await request('GET', `/api/devices/${encodeURIComponent(deviceId)}`, undefined, token);
    return response.status === 200 && response.data.online === true && response.data.maintenance_due === 1;
  });

  const before = await request('GET', `/api/devices/${encodeURIComponent(deviceId)}`, undefined, token);
  check('pre-maintenance counters are due', before.data.maintenance_lift_count === 5000 && before.data.maintenance_count === 0);

  const downPromise = waitForDown();
  const legacyRequest = await request('POST', `/api/maintenance/register_done/${encodeURIComponent(deviceId)}`, undefined, token);
  const down = await downPromise;
  check('legacy endpoint only queues MQTT', legacyRequest.status === 200 && down && down.cmd === 'maintenance_done');
  check('downlink carries message id', !!down?.msg_id && down.target_uid === chipUid);

  const duplicatePending = await request('POST', `/api/commands/maintenance_done/${encodeURIComponent(deviceId)}`, undefined, token);
  const unexpectedSecondDown = await waitForDown(500);
  check('duplicate pending request reuses message id', duplicatePending.status === 200 && duplicatePending.data.msg_id === down.msg_id && unexpectedSecondDown === null);

  const immediate = await request('GET', `/api/devices/${encodeURIComponent(deviceId)}`, undefined, token);
  check('counters unchanged before device ack', immediate.data.maintenance_lift_count === 5000 && immediate.data.maintenance_count === 0 && immediate.data.maintenance_due === 1);

  await publishUp(commandResponse('maintenance_done', 'maintenance_done', maintenanceSnapshot({
    maintenance_lift_count: 0,
    maintenance_count: 1,
    last_maintenance_total: 5000,
    maintenance_due: 0
  }), down.msg_id));
  await waitUntil('maintenance command reaches terminal state', async () => {
    const status = await request('GET', `/api/commands/status/${encodeURIComponent(down.msg_id)}`, undefined, token);
    return status.status === 200 && status.data.status === 'succeeded';
  });

  const after = await request('GET', `/api/devices/${encodeURIComponent(deviceId)}`, undefined, token);
  check('device ack updates counters', after.data.maintenance_lift_count === 0 && after.data.maintenance_count === 1 && after.data.last_maintenance_total === 5000 && after.data.maintenance_due === 0);
  const recordsAfter = await request('GET', `/api/maintenance?device_id=${encodeURIComponent(deviceId)}`, undefined, token);
  check('device ack creates one maintenance record', recordsAfter.status === 200 && recordsAfter.data.filter((row) => row.command_msg_id === down.msg_id).length === 1);

  await publishUp(commandResponse('maintenance_done', 'maintenance_done', maintenanceSnapshot({
    maintenance_lift_count: 0,
    maintenance_count: 1,
    last_maintenance_total: 5000,
    maintenance_due: 0
  }), down.msg_id));
  await sleep(500);
  const duplicateState = await request('GET', `/api/devices/${encodeURIComponent(deviceId)}`, undefined, token);
  const duplicateRecords = await request('GET', `/api/maintenance?device_id=${encodeURIComponent(deviceId)}`, undefined, token);
  check('duplicate device ack is idempotent', duplicateState.data.maintenance_count === 1 && duplicateRecords.data.filter((row) => row.command_msg_id === down.msg_id).length === 1);

  const missingDownPromise = waitForDown();
  const missingRequest = await request('POST', `/api/commands/maintenance_done/${encodeURIComponent(deviceId)}`, undefined, token);
  const missingDown = await missingDownPromise;
  check('second maintenance command is sent', missingRequest.status === 200 && missingDown?.cmd === 'maintenance_done');
  await publishUp(commandResponse('maintenance_done', 'succeeded', null, missingDown.msg_id));
  await waitUntil('missing-ledger command reaches terminal state', async () => {
    const status = await request('GET', `/api/commands/status/${encodeURIComponent(missingDown.msg_id)}`, undefined, token);
    return status.status === 200 && status.data.status === 'succeeded';
  });
  const missingState = await request('GET', `/api/devices/${encodeURIComponent(deviceId)}`, undefined, token);
  const missingRecords = await request('GET', `/api/maintenance?device_id=${encodeURIComponent(deviceId)}`, undefined, token);
  check('missing device ledger does not mutate cache', missingState.data.maintenance_count === 1 && missingState.data.maintenance_due === 0 && missingRecords.data.filter((row) => row.command_msg_id === missingDown.msg_id).length === 0);

  const failedDownPromise = waitForDown();
  const failedRequest = await request('POST', `/api/commands/maintenance_done/${encodeURIComponent(deviceId)}`, undefined, token);
  const failedDown = await failedDownPromise;
  check('failed maintenance command is sent', failedRequest.status === 200 && failedDown?.cmd === 'maintenance_done');
  await publishUp(commandResponse('maintenance_done', 'maintenance_failed', null, failedDown.msg_id, 'simulated failure'));
  await waitUntil('failed command reaches terminal state', async () => {
    const status = await request('GET', `/api/commands/status/${encodeURIComponent(failedDown.msg_id)}`, undefined, token);
    return status.status === 200 && status.data.status === 'failed';
  });
  const failedState = await request('GET', `/api/devices/${encodeURIComponent(deviceId)}`, undefined, token);
  check('failed device command does not mutate cache', failedState.data.maintenance_count === 1 && failedState.data.maintenance_due === 0);

  log(`SUMMARY total=${passCount + failCount} passed=${passCount} failed=${failCount}`);
  if (failCount > 0) process.exitCode = 1;
}

async function cleanup() {
  if (deviceClient) deviceClient.end(true);
  if (server && !server.killed) server.kill();
  await sleep(300);
  for (const suffix of ['', '-shm', '-wal']) {
    try { fs.unlinkSync(`${dbPath}${suffix}`); } catch (_) { /* already removed */ }
  }
}

run().catch((error) => {
  console.error('[maintenance-test] ERROR', error);
  process.exitCode = 1;
}).finally(cleanup);
