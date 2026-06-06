const { getDb } = require('./database');
const { getStatus: getMqttStatus } = require('./mqtt-bridge');

function getPlatformSnapshot() {
  const db = getDb();

  const devices = db.prepare(`
    SELECT d.device_id, d.name, d.model, d.group_name AS "group", d.location,
           s.online, s.locked, s.state, s.alarm, s.height_left_mm,
           s.height_right_mm, s.height_diff_mm, s.run_count, s.run_time_s,
           s.ts_ms, s.updated_at
    FROM devices d
    LEFT JOIN device_status s ON d.device_id = s.device_id
    ORDER BY d.device_id
  `).all().map((d) => ({
    ...d,
    online: !!d.online,
    locked: !!d.locked,
    state: d.state || 'idle',
    alarm: d.alarm || 'none',
    height_left_mm: d.height_left_mm || 0,
    height_right_mm: d.height_right_mm || 0,
    height_diff_mm: d.height_diff_mm || 0,
    run_count: d.run_count || 0,
    run_time_s: d.run_time_s || 0
  }));

  const alarms = db.prepare(`
    SELECT a.id, a.device_id, d.name AS device_name, a.alarm_type, a.message,
           a.level, a.acknowledged, a.created_at, a.resolved_at
    FROM alarms a
    LEFT JOIN devices d ON a.device_id = d.device_id
    ORDER BY a.id DESC
    LIMIT 50
  `).all();

  const maintenance = db.prepare(`
    SELECT m.id, m.device_id, d.name AS device_name, m.type, m.description,
           m.handler, m.result, m.next_date, m.cost, m.created_at
    FROM maintenance_records m
    LEFT JOIN devices d ON m.device_id = d.device_id
    ORDER BY m.id DESC
    LIMIT 50
  `).all();

  const commands = db.prepare(`
    SELECT id, device_id, cmd, msg_id, status, result, created_at, responded_at
    FROM command_queue
    ORDER BY id DESC
    LIMIT 50
  `).all();

  const logs = db.prepare(`
    SELECT l.id, l.action, l.device_id, l.detail, l.result, l.created_at,
           u.username, u.real_name
    FROM operation_logs l
    LEFT JOIN users u ON l.user_id = u.id
    ORDER BY l.id DESC
    LIMIT 30
  `).all();

  const summary = {
    total_devices: devices.length,
    online_devices: devices.filter((d) => d.online).length,
    offline_devices: devices.filter((d) => !d.online).length,
    locked_devices: devices.filter((d) => d.locked).length,
    fault_devices: devices.filter((d) => d.alarm && d.alarm !== 'none').length,
    total_run_count: devices.reduce((sum, d) => sum + (d.run_count || 0), 0),
    total_run_time_s: devices.reduce((sum, d) => sum + (d.run_time_s || 0), 0),
    max_height_diff_mm: devices.reduce((max, d) => Math.max(max, Math.abs(d.height_diff_mm || 0)), 0),
    unacknowledged_alarms: alarms.filter((a) => !a.acknowledged).length,
    pending_commands: commands.filter((c) => c.status === 'pending' || c.status === 'sent').length
  };

  return {
    generated_at: new Date().toISOString(),
    mqtt: getMqttStatus(),
    summary,
    devices,
    recent_alarms: alarms,
    recent_maintenance: maintenance,
    recent_commands: commands,
    recent_logs: logs
  };
}

function buildAiContextText() {
  const snapshot = getPlatformSnapshot();
  return [
    '以下是高昌举升机 IoT 平台当前数据快照。你可以直接基于这些数据做状态分析、风险判断、维护建议和运维决策。',
    '不要声称自己无法访问平台数据；你已经可以读取此快照。不要编造快照之外的数据。',
    '如涉及远程锁机/解锁等控制动作，只给出建议和风险说明，不要声称已经执行控制命令。',
    JSON.stringify(snapshot, null, 2)
  ].join('\n');
}

module.exports = { getPlatformSnapshot, buildAiContextText };
