/**
 * 全链路端到端测试
 * MQTT → Backend → Database → WebSocket → API
 */

const mqtt = require('mqtt');
const http = require('http');

const BROKER = process.env.MQTT_BROKER || 'mqtt://8.134.201.118:1883';
const API_BASE = process.env.API_BASE || 'http://localhost:3000/api';
const TOPIC_PREFIX = process.env.MQTT_TOPIC_PREFIX || 'gaochang/lift';
const GATEWAY_ID = process.env.MQTT_GATEWAY_ID || 'f407zet6';
const DEVICE_ID = process.env.MQTT_DEVICE_ID || 'gaochang_lift_f407zet6';
const DEVICE_NAME = 'Gaochang-ThinScissor';
const CHIP_UID = process.env.MQTT_CHIP_UID || '3d002d0018474e5036393820';
const TOPIC_TELEMETRY = `${TOPIC_PREFIX}/v1/devices/${CHIP_UID}/up`;
const TOPIC_COMMAND = `${TOPIC_PREFIX}/v1/devices/${CHIP_UID}/down`;

let passCount = 0;
let failCount = 0;
let testResults = [];

function log(tag, msg) {
  console.log(`[${new Date().toLocaleTimeString()}][${tag}] ${msg}`);
}

function assert(name, condition) {
  if (condition) { passCount++; testResults.push(`  PASS: ${name}`); log('PASS', name); }
  else { failCount++; testResults.push(`  FAIL: ${name}`); log('FAIL', name); }
}

async function apiRequest(method, path, body, token) {
  const url = new URL(API_BASE + path);
  const headers = { 'Content-Type': 'application/json' };
  if (token) headers['Authorization'] = 'Bearer ' + token;

  return new Promise((resolve, reject) => {
    const req = http.request(url, { method, headers }, (res) => {
      let data = '';
      res.on('data', c => data += c);
      res.on('end', () => {
        try { resolve({ status: res.statusCode, data: JSON.parse(data) }); }
        catch { resolve({ status: res.statusCode, data }); }
      });
    });
    req.on('error', reject);
    if (body) req.write(JSON.stringify(body));
    req.end();
  });
}

async function runTests() {
  log('TEST', '========================================');
  log('TEST', '全链路端到端集成测试');
  log('TEST', '========================================\n');

  // ===== Test 1: 用户登录 =====
  log('TEST', '--- Test 1: 用户认证 ---');
  const loginRes = await apiRequest('POST', '/auth/login', { username: 'admin', password: 'admin123' });
  assert('登录成功 (200)', loginRes.status === 200);
  assert('返回token', loginRes.data && loginRes.data.token);
  assert('返回用户信息', loginRes.data && loginRes.data.user);
  const token = loginRes.data.token;

  const registry = await apiRequest('POST', '/admin/registry', {
    serial: DEVICE_ID, uid: CHIP_UID, product_type: 'double_post',
    model: 'release-e2e', bind_code: 'E2E-123456'
  }, token);
  assert('登记隔离测试设备', registry.status === 200 || registry.status === 409);

  const binding = await apiRequest('POST', '/binding/bind', {
    serial: DEVICE_ID, bind_code: 'E2E-123456'
  }, token);
  assert('绑定隔离测试设备', binding.status === 201 || binding.status === 202 || binding.status === 409);

  // 错误密码
  const badLogin = await apiRequest('POST', '/auth/login', { username: 'admin', password: 'wrong' });
  assert('错误密码被拒绝 (401)', badLogin.status === 401);

  // 无token访问
  const noAuth = await apiRequest('GET', '/devices');
  assert('无token被拒绝 (401)', noAuth.status === 401);

  // ===== Test 2: 设备管理API =====
  log('TEST', '\n--- Test 2: 设备管理API ---');
  let devices = await apiRequest('GET', '/devices', null, token);
  if (devices.status === 200 && !devices.data.some(d => d.device_id === DEVICE_ID)) {
    const created = await apiRequest('POST', '/devices', {
      device_id: DEVICE_ID, name: DEVICE_NAME, model: 'release-e2e',
      product_type: 'double_post', lift_role: 'main', uid: CHIP_UID
    }, token);
    assert('创建隔离测试设备', created.status === 200 || created.status === 409);
    devices = await apiRequest('GET', '/devices', null, token);
  }
  assert('获取设备列表成功', devices.status === 200);
  assert('设备列表是数组', Array.isArray(devices.data));
  assert('有设备数据', devices.data.length > 0);
  const dev001 = devices.data.find(d => d.device_id === DEVICE_ID);
  assert(`${DEVICE_ID}存在`, !!dev001);
  assert(`${DEVICE_ID}有name字段`, dev001 && dev001.name);
  log('TEST', `  设备数: ${devices.data.length}, ${DEVICE_ID}: ${dev001?.name}`);

  // ===== Test 3: MQTT→后端→数据库 =====
  log('TEST', '\n--- Test 3: MQTT→后端→数据库 ---');
  const mqttClient = await new Promise((resolve, reject) => {
    const c = mqtt.connect(BROKER, { clientId: 'e2e-test-' + Date.now().toString(36), clean: true, connectTimeout: 10000 });
    c.on('connect', () => resolve(c));
    c.on('error', reject);
    setTimeout(() => reject(new Error('MQTT timeout')), 10000);
  });

  // 发送带有特定数据的状态消息
  const testRunCount = 888;
  const testHeight = 1500;
  mqttClient.publish(TOPIC_TELEMETRY, JSON.stringify({
    v: 1, type: 'telemetry', chip_uid: CHIP_UID, uid: CHIP_UID,
    product_type: 'double_post', msg_id: `e2e-telemetry-${Date.now()}`,
    device: DEVICE_ID, device_id: DEVICE_ID, name: DEVICE_NAME,
    online: true, locked: false, state: 'up', alarm: 'none', data: {
    height_left_mm: testHeight, height_right_mm: testHeight - 5,
    height_diff_mm: 5, run_count: testRunCount, run_time_s: 7200, ts_ms: Date.now()
    }
  }));

  // 等待后端处理
  await new Promise(r => setTimeout(r, 2000));

  // 验证数据库已更新
  const statusAfter = await apiRequest('GET', '/devices', null, token);
  const dev001After = statusAfter.data.find(d => d.device_id === DEVICE_ID);
  assert('MQTT后数据库更新-height', dev001After && dev001After.height_left_mm === testHeight);
  assert('MQTT后数据库更新-run_count', dev001After && dev001After.run_count === testRunCount);
  assert('MQTT后数据库更新-state', dev001After && dev001After.state === 'up');
  assert('MQTT后数据库更新-online', dev001After && dev001After.online === true);

  // ===== Test 4: 报警生成 =====
  log('TEST', '\n--- Test 4: 报警生成 ---');
  mqttClient.publish(TOPIC_TELEMETRY, JSON.stringify({
    v: 1, type: 'telemetry', chip_uid: CHIP_UID, uid: CHIP_UID,
    product_type: 'double_post', msg_id: `e2e-alarm-${Date.now()}`,
    device: DEVICE_ID, device_id: DEVICE_ID, name: DEVICE_NAME,
    online: true, locked: false, state: 'stop', alarm: 'stall', data: {
    height_left_mm: 800, height_right_mm: 790, height_diff_mm: 10,
    run_count: testRunCount, run_time_s: 7200, ts_ms: Date.now()
    }
  }));
  await new Promise(r => setTimeout(r, 2000));

  const alarms = await apiRequest('GET', '/alarms', null, token);
  assert('报警API返回成功', alarms.status === 200);
  assert('有报警数据', alarms.data.length > 0);
  const stallAlarm = alarms.data.find(a => a.alarm_type === 'stall' && a.device_id === DEVICE_ID);
  assert('stall报警已创建', !!stallAlarm);

  // ===== Test 5: 报警确认和解除 =====
  log('TEST', '\n--- Test 5: 报警确认/解除 ---');
  if (stallAlarm) {
    const ackRes = await apiRequest('PUT', `/alarms/${stallAlarm.id}/acknowledge`, null, token);
    assert('报警确认成功', ackRes.status === 200);

    const resolveRes = await apiRequest('PUT', `/alarms/${stallAlarm.id}/resolve`, null, token);
    assert('报警解除成功', resolveRes.status === 200);
  }

  // ===== Test 6: 维修保养记录 =====
  log('TEST', '\n--- Test 6: 维修保养 ---');
  const addMaint = await apiRequest('POST', '/maintenance', {
    device_id: DEVICE_ID, type: '保养', description: '全链路测试保养记录',
    handler: '测试员', result: '完成', cost: 500
  }, token);
  assert('添加维修记录成功', addMaint.status === 200 || addMaint.status === 201);

  const maintList = await apiRequest('GET', '/maintenance', null, token);
  assert('获取维修列表成功', maintList.status === 200);
  assert('维修记录存在', maintList.data.length > 0);

  // ===== Test 7: 操作日志 =====
  log('TEST', '\n--- Test 7: 操作日志 ---');
  const logs = await apiRequest('GET', '/logs', null, token);
  assert('获取日志成功', logs.status === 200);
  assert('日志有数据', logs.data && logs.data.logs);

  // ===== Test 8: 命令发送 (后端→MQTT) =====
  log('TEST', '\n--- Test 8: 命令发送 ---');
  // 订阅下行主题验证命令
  let receivedCmd = null;
  await new Promise((resolve) => {
    mqttClient.subscribe(TOPIC_COMMAND, () => {
      const handler = (topic, msg) => {
        if (topic === TOPIC_COMMAND) {
          receivedCmd = JSON.parse(msg.toString());
          mqttClient.removeListener('message', handler);
          resolve();
        }
      };
      mqttClient.on('message', handler);
      // 发送查询命令
      apiRequest('POST', `/commands/query/${DEVICE_ID}`, null, token).then(() => {});
      setTimeout(() => { mqttClient.removeListener('message', handler); resolve(); }, 5000);
    });
  });
  assert('查询命令通过MQTT下发', !!receivedCmd);
  assert('命令cmd=get_status', receivedCmd && receivedCmd.cmd === 'get_status');
  assert('命令有msg_id', receivedCmd && receivedCmd.msg_id);

  // ===== Test 9: 统计API =====
  log('TEST', '\n--- Test 9: 运行统计 ---');
  const stats = await apiRequest('GET', '/stats/runtime', null, token);
  assert('统计API成功', stats.status === 200);

  // ===== Test 10: MQTT状态API =====
  log('TEST', '\n--- Test 10: MQTT状态 ---');
  const mqttStatus = await apiRequest('GET', '/mqtt-status', null, token);
  assert('MQTT状态API成功', mqttStatus.status === 200);
  assert('MQTT已连接', mqttStatus.data && mqttStatus.data.connected === true);

  // 恢复正常状态
  mqttClient.publish(TOPIC_TELEMETRY, JSON.stringify({
    v: 1, type: 'telemetry', chip_uid: CHIP_UID, uid: CHIP_UID,
    product_type: 'double_post', msg_id: `e2e-normal-${Date.now()}`,
    device: DEVICE_ID, device_id: DEVICE_ID, name: DEVICE_NAME,
    online: true, locked: false, state: 'idle', alarm: 'none', data: {
    height_left_mm: 0, height_right_mm: 0, height_diff_mm: 0,
    run_count: testRunCount, run_time_s: 7200, ts_ms: Date.now()
    }
  }));

  await new Promise(r => setTimeout(r, 1000));
  mqttClient.end();

  // ===== 测试报告 =====
  log('TEST', '\n========================================');
  log('TEST', '端到端测试报告');
  log('TEST', '========================================');
  testResults.forEach(r => log('TEST', r));
  log('TEST', '----------------------------------------');
  log('TEST', `总计: ${passCount + failCount} | 通过: ${passCount} | 失败: ${failCount}`);
  log('TEST', `成功率: ${((passCount / (passCount + failCount)) * 100).toFixed(1)}%`);
  log('TEST', '========================================\n');

  process.exit(failCount > 0 ? 1 : 0);
}

runTests().catch(err => {
  log('ERROR', `测试失败: ${err.message}`);
  console.error(err);
  process.exit(1);
});
