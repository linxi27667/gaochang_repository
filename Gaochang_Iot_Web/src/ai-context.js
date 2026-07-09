const { getDb } = require('./database');
const { getStatus: getMqttStatus } = require('./mqtt-bridge');

function getPlatformSnapshot(user = null) {
  const db = getDb();
  const isAdmin = user && user.role === 'admin';
  const deviceScope = isAdmin ? '' : 'WHERE d.owner_id = ?';
  const deviceParams = isAdmin ? [] : [user?.id || -1];

  const devices = db.prepare(`
    SELECT d.device_id, d.name, d.model, d.group_name AS "group", d.location,
           d.product_type, d.has_encoder, d.has_buzzer, d.has_pressure_sensor, d.has_display,
           s.online, s.locked, s.state, s.alarm, s.height_left_mm,
           s.height_right_mm, s.height_diff_mm, s.run_count, s.run_time_s,
           s.uptime_s, s.ts_ms, s.updated_at,
           s.direction, s.upper_limit, s.lower_limit, s.stall, s.collision_up,
           s.collision_down, s.alarm_code, s.csq, s.dtu_state,
           s.left_pulse, s.right_pulse, s.left_up_collision, s.right_up_collision,
           s.left_down_collision, s.right_down_collision
    FROM devices d
    LEFT JOIN device_status s ON d.device_id = s.device_id
    ${deviceScope}
    ORDER BY d.device_id
  `).all(...deviceParams).map((d) => ({
    ...d,
    online: !!d.online,
    locked: !!d.locked,
    state: d.state || 'idle',
    alarm: d.alarm || 'none',
    product_type: d.product_type || 'double_post',
    has_encoder: !!d.has_encoder,
    has_buzzer: !!d.has_buzzer,
    has_pressure_sensor: !!d.has_pressure_sensor,
    has_display: !!d.has_display,
    height_left_mm: d.height_left_mm || 0,
    height_right_mm: d.height_right_mm || 0,
    height_diff_mm: d.height_diff_mm || 0,
    run_count: d.run_count || 0,
    run_time_s: d.run_time_s || 0,
    uptime_s: d.uptime_s || 0,
    direction: d.direction || 'stop',
    upper_limit: d.upper_limit || 0,
    lower_limit: d.lower_limit || 0,
    stall: d.stall || 0,
    collision_up: d.collision_up || 0,
    collision_down: d.collision_down || 0,
    alarm_code: d.alarm_code || 0,
    csq: d.csq ?? -1,
    dtu_state: d.dtu_state || '',
    left_pulse: d.left_pulse || 0,
    right_pulse: d.right_pulse || 0,
    left_up_collision: d.left_up_collision || 0,
    right_up_collision: d.right_up_collision || 0,
    left_down_collision: d.left_down_collision || 0,
    right_down_collision: d.right_down_collision || 0
  }));

  const itemScope = isAdmin ? '' : 'WHERE d.owner_id = ?';
  const itemParams = isAdmin ? [] : [user?.id || -1];

  const alarms = db.prepare(`
    SELECT a.id, a.device_id, d.name AS device_name, a.alarm_type, a.message,
           a.level, a.acknowledged, a.created_at, a.resolved_at
    FROM alarms a
    LEFT JOIN devices d ON a.device_id = d.device_id
    ${itemScope}
    ORDER BY a.id DESC
    LIMIT 50
  `).all(...itemParams);

  const maintenance = db.prepare(`
    SELECT m.id, m.device_id, d.name AS device_name, m.type, m.description,
           m.handler, m.result, m.next_date, m.cost, m.created_at
    FROM maintenance_records m
    LEFT JOIN devices d ON m.device_id = d.device_id
    ${itemScope}
    ORDER BY m.id DESC
    LIMIT 50
  `).all(...itemParams);

  const commands = db.prepare(`
    SELECT c.id, c.device_id, c.cmd, c.msg_id, c.status, c.result, c.created_at, c.responded_at
    FROM command_queue c
    LEFT JOIN devices d ON c.device_id = d.device_id
    ${itemScope}
    ORDER BY c.id DESC
    LIMIT 50
  `).all(...itemParams);

  const logScope = isAdmin ? '' : 'WHERE l.user_id = ? OR d.owner_id = ?';
  const logParams = isAdmin ? [] : [user?.id || -1, user?.id || -1];
  const logs = db.prepare(`
    SELECT l.id, l.action, l.device_id, l.detail, l.result, l.created_at,
           u.username, u.real_name
    FROM operation_logs l
    LEFT JOIN users u ON l.user_id = u.id
    LEFT JOIN devices d ON l.device_id = d.device_id
    ${logScope}
    ORDER BY l.id DESC
    LIMIT 30
  `).all(...logParams);

  const encoderDevices = devices.filter((d) => d.has_encoder);
  const summary = {
    total_devices: devices.length,
    height_feedback_devices: encoderDevices.length,
    motion_only_devices: devices.length - encoderDevices.length,
    online_devices: devices.filter((d) => d.online).length,
    offline_devices: devices.filter((d) => !d.online).length,
    locked_devices: devices.filter((d) => d.locked).length,
    fault_devices: devices.filter((d) => d.alarm && d.alarm !== 'none').length,
    total_run_count: devices.reduce((sum, d) => sum + (d.run_count || 0), 0),
    max_height_diff_mm: encoderDevices.reduce((max, d) => Math.max(max, Math.abs(d.height_diff_mm || 0)), 0),
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

function buildAiContextText(user = null) {
  const snapshot = getPlatformSnapshot(user);
  return [
    '以下是高昌举升机 IoT 平台当前数据快照。你可以直接基于这些数据做状态分析、风险判断、维护建议和运维决策。',
    '不要声称自己无法访问平台数据；你已经可以读取此快照。不要编造快照之外的数据。',
    '如涉及远程锁机/解锁等控制动作，只给出建议和风险说明，不要声称已经执行控制命令。',
    JSON.stringify(snapshot, null, 2)
  ].join('\n');
}

module.exports = { getPlatformSnapshot, buildAiContextText };
