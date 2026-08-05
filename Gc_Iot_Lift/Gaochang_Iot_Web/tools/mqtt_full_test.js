/**
 * 全链路MQTT测试脚本
 * 模拟STM32 DTU发送MQTT消息，验证完整链路
 */

const mqtt = require('mqtt');

const BROKER = process.env.MQTT_BROKER || 'mqtt://8.134.167.240:1883';
const TOPIC_PREFIX = process.env.MQTT_TOPIC_PREFIX || 'gaochang/lift';
const GATEWAY_ID = process.env.MQTT_GATEWAY_ID || 'f407zet6';
const DEVICE_ID = process.env.MQTT_DEVICE_ID || 'gaochang_lift_f407zet6';
const DEVICE_NAME = 'Gaochang-ThinScissor';
const TOPIC_UP = `${TOPIC_PREFIX}/${GATEWAY_ID}/telemetry`;
const TOPIC_DOWN = `${TOPIC_PREFIX}/${GATEWAY_ID}/command`;

let passCount = 0;
let failCount = 0;
let testResults = [];

function log(tag, msg) {
  const ts = new Date().toLocaleTimeString();
  console.log(`[${ts}][${tag}] ${msg}`);
}

function assert(name, condition) {
  if (condition) {
    passCount++;
    testResults.push(`  PASS: ${name}`);
    log('PASS', name);
  } else {
    failCount++;
    testResults.push(`  FAIL: ${name}`);
    log('FAIL', name);
  }
}

function buildStatusJson(overrides = {}) {
  return JSON.stringify({
    type: 'telemetry', ver: 1, device: DEVICE_ID, device_id: DEVICE_ID, name: DEVICE_NAME,
    online: true, locked: false, state: 'idle', alarm: 'none',
    height_left_mm: 0, height_right_mm: 0, height_diff_mm: 0,
    run_count: 0, run_time_s: 0, ts_ms: Date.now(),
    ...overrides
  });
}

async function runTests() {
  log('TEST', '========================================');
  log('TEST', '全链路MQTT集成测试');
  log('TEST', `Broker: ${BROKER}`);
  log('TEST', `Device: ${DEVICE_ID}`);
  log('TEST', '========================================\n');

  // ===== Test 1: MQTT Broker连接 =====
  log('TEST', '--- Test 1: MQTT Broker连接 ---');
  const client = await new Promise((resolve, reject) => {
    const c = mqtt.connect(BROKER, {
      clientId: 'test-' + Date.now().toString(36),
      clean: true, connectTimeout: 10000
    });
    c.on('connect', () => resolve(c));
    c.on('error', (err) => reject(err));
    setTimeout(() => reject(new Error('Connection timeout')), 10000);
  });
  assert('MQTT Broker连接成功', client.connected);

  // 订阅上行+下行两个主题
  await new Promise((resolve) => {
    client.subscribe([TOPIC_UP, TOPIC_DOWN], { qos: 0 }, () => resolve());
  });
  assert('订阅上行主题 ' + TOPIC_UP, true);
  assert('订阅下行主题 ' + TOPIC_DOWN, true);

  // 收集消息的辅助函数
  function waitForMessage(filterTopic, timeoutMs = 5000) {
    return new Promise((resolve) => {
      const handler = (topic, msg) => {
        if (topic === filterTopic) {
          try {
            const data = JSON.parse(msg.toString());
            client.removeListener('message', handler);
            resolve(data);
          } catch (e) { /* ignore parse errors */ }
        }
      };
      client.on('message', handler);
      setTimeout(() => { client.removeListener('message', handler); resolve(null); }, timeoutMs);
    });
  }

  // ===== Test 2: 发送设备状态 (模拟DTU→Broker) =====
  log('TEST', '\n--- Test 2: 设备状态上行 ---');
  const statusData = {
    state: 'up', height_left_mm: 1200, height_right_mm: 1195,
    height_diff_mm: 5, run_count: 42, run_time_s: 3600
  };
  const statusPromise = waitForMessage(TOPIC_UP);
  client.publish(TOPIC_UP, buildStatusJson(statusData));
  const received = await statusPromise;

  assert('状态消息被Broker接收', received !== null);
  assert('device正确', received && (received.device === DEVICE_ID || received.device_id === DEVICE_ID));
  assert('name字段存在', received && received.name === DEVICE_NAME);
  assert('state=up', received && received.state === 'up');
  assert('height_left_mm=1200', received && received.height_left_mm === 1200);
  assert('height_right_mm=1195', received && received.height_right_mm === 1195);
  assert('height_diff_mm=5', received && received.height_diff_mm === 5);
  assert('run_count=42', received && received.run_count === 42);
  assert('run_time_s=3600', received && received.run_time_s === 3600);
  assert('ts_ms是数字', received && typeof received.ts_ms === 'number');
  assert('ver=1', received && received.ver === 1);

  // ===== Test 3: 报警状态 =====
  log('TEST', '\n--- Test 3: 报警状态 ---');
  const alarmPromise = waitForMessage(TOPIC_UP);
  client.publish(TOPIC_UP, buildStatusJson({ alarm: 'collision', state: 'stop' }));
  const alarmData = await alarmPromise;
  assert('alarm=collision', alarmData && alarmData.alarm === 'collision');
  assert('state=stop', alarmData && alarmData.state === 'stop');

  // ===== Test 4: 锁机状态 =====
  log('TEST', '\n--- Test 4: 锁机状态 ---');
  const lockPromise = waitForMessage(TOPIC_UP);
  client.publish(TOPIC_UP, buildStatusJson({ locked: true }));
  const lockData = await lockPromise;
  assert('locked=true', lockData && lockData.locked === true);

  // ===== Test 5: 后端发送锁机命令 =====
  log('TEST', '\n--- Test 5: 后端→设备命令 ---');
  const cmdPromise = waitForMessage(TOPIC_DOWN);
  client.publish(TOPIC_DOWN, JSON.stringify({ cmd: 'lock', msg_id: 'cmd_001' }));
  const cmdData = await cmdPromise;
  assert('命令cmd=lock', cmdData && cmdData.cmd === 'lock');
  assert('命令msg_id存在', cmdData && cmdData.msg_id === 'cmd_001');

  // ===== Test 6: STM32命令回应 =====
  log('TEST', '\n--- Test 6: STM32命令回应 ---');
  const ackPromise = waitForMessage(TOPIC_UP);
  client.publish(TOPIC_UP, JSON.stringify({
    type: 'status', device: DEVICE_ID, msg_id: 'cmd_001', cmd: 'lock', result: 'locked', ts_ms: Date.now()
  }));
  const ackData = await ackPromise;
  assert('ack msg_id正确', ackData && ackData.msg_id === 'cmd_001');
  assert('ack result=locked', ackData && ackData.result === 'locked');

  // ===== Test 7: rename命令+回应 =====
  log('TEST', '\n--- Test 7: 设备改名 ---');
  const renameCmdPromise = waitForMessage(TOPIC_DOWN);
  client.publish(TOPIC_DOWN, JSON.stringify({ cmd: 'rename', msg_id: 'cmd_r1', name: '高昌A区3号' }));
  const renameCmd = await renameCmdPromise;
  assert('rename命令cmd=rename', renameCmd && renameCmd.cmd === 'rename');
  assert('rename命令name字段', renameCmd && renameCmd.name === '高昌A区3号');

  const renameAckPromise = waitForMessage(TOPIC_UP);
  client.publish(TOPIC_UP, JSON.stringify({
    type: 'status', device: DEVICE_ID, msg_id: 'cmd_r1', cmd: 'rename', result: 'renamed', ts_ms: Date.now()
  }));
  const renameAck = await renameAckPromise;
  assert('rename回应result=renamed', renameAck && renameAck.result === 'renamed');

  // ===== Test 8: 离线状态 =====
  log('TEST', '\n--- Test 8: 离线状态 ---');
  const offPromise = waitForMessage(TOPIC_UP);
  client.publish(TOPIC_UP, buildStatusJson({ online: false }));
  const offData = await offPromise;
  assert('online=false', offData && offData.online === false);

  // ===== Test 9: 多设备 =====
  log('TEST', '\n--- Test 9: 多设备测试 ---');
  // 先订阅所有设备主题
  const multiTopics = [2,3,4].map(i => `${TOPIC_PREFIX}/sim00${i}/telemetry`);
  await new Promise((resolve) => {
    client.subscribe(multiTopics, { qos: 0 }, () => resolve());
  });

  for (let i = 2; i <= 4; i++) {
    const devId = `gaochang_lift_sim00${i}`;
    const topic = `${TOPIC_PREFIX}/sim00${i}/telemetry`;
    const p = waitForMessage(topic);
    client.publish(topic, JSON.stringify({
      type: 'telemetry', device: devId, device_id: devId, name: `高昌${i}号举升机`,
      online: true, locked: false, state: 'idle', alarm: 'none',
      height_left_mm: i * 300, height_right_mm: i * 300 - 3, height_diff_mm: 3,
      run_count: i * 10, run_time_s: i * 1800, ts_ms: Date.now()
    }));
    const d = await p;
    assert(`设备${devId}上行`, d && (d.device === devId || d.device_id === devId));
  }

  // 恢复正常
  client.publish(TOPIC_UP, buildStatusJson());
  await new Promise(r => setTimeout(r, 1000));

  // ===== 报告 =====
  log('TEST', '\n========================================');
  log('TEST', '测试报告');
  log('TEST', '========================================');
  testResults.forEach(r => log('TEST', r));
  log('TEST', '----------------------------------------');
  log('TEST', `总计: ${passCount + failCount} | 通过: ${passCount} | 失败: ${failCount}`);
  log('TEST', `成功率: ${((passCount / (passCount + failCount)) * 100).toFixed(1)}%`);
  log('TEST', '========================================\n');

  client.end();
  process.exit(failCount > 0 ? 1 : 0);
}

runTests().catch(err => {
  log('ERROR', `测试失败: ${err.message}`);
  console.error(err);
  process.exit(1);
});
