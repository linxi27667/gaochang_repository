require('dotenv').config();
const assert = require('assert');
const { getDb } = require('./src/database');
const { createMsgId, enqueueAndSendCommand } = require('./src/routes/commands');
const { initializeShippingResetWebsite } = require('./src/routes/admin');

const db = getDb();
const account = process.argv[2] || 'QIRUI';
const operator = { id: null, username: 'shipping-initializer' };
const devices = db.prepare(`
  SELECT DISTINCT d.device_id, d.name, d.product_type,
         COALESCE(s.online, 0) AS online
    FROM devices d
    JOIN device_bindings b ON b.device_id = d.device_id AND b.status = 'active'
    JOIN users u ON u.id = b.user_id AND u.username = ?
    LEFT JOIN device_status s ON s.device_id = d.device_id
   ORDER BY d.device_id
`).all(account);
assert.ok(devices.length > 0, `没有找到账号 ${account} 的有效绑定设备`);

const bindingSnapshot = () => db.prepare(`
  SELECT d.device_id, b.user_id, u.username, b.status, b.bind_type
    FROM devices d
    JOIN device_bindings b ON b.device_id = d.device_id
    JOIN users u ON u.id = b.user_id
   WHERE d.device_id IN (${devices.map(() => '?').join(',')})
   ORDER BY d.device_id, b.user_id, b.status
`).all(...devices.map(d => d.device_id));
const beforeBindings = JSON.stringify(bindingSnapshot());
const results = [];

for (const device of devices) {
  const existing = db.prepare(`
    SELECT msg_id, status, purpose FROM command_queue
     WHERE device_id = ?
       AND purpose IN ('shipping_reset_deferred', 'shipping_reset', 'shipping_reset_admin_enter', 'shipping_reset_admin_exit')
       AND status IN ('pending', 'sent')
     ORDER BY id DESC LIMIT 1
  `).get(device.device_id);
  if (existing) {
    results.push({ device_id: device.device_id, msg_id: existing.msg_id, status: existing.status, existing: true });
    continue;
  }

  const msgId = createMsgId();
  const isLargeScissor = device.product_type === 'large_scissor';
  const cmd = isLargeScissor ? 'admin_enter' : 'reset_usage';
  initializeShippingResetWebsite(db, device.device_id, operator.id, msgId);
  const queued = enqueueAndSendCommand(db, device.device_id, cmd, msgId, {
    purpose: 'shipping_reset_deferred',
    deferIfOffline: true,
    ...(isLargeScissor ? { password: process.env.LIFT_IOT_ADMIN_PASSWORD || '123456' } : {})
  }, operator);
  results.push({ device_id: device.device_id, msg_id: queued.msg_id, status: queued.status, deferred: !!queued.deferred });
}

const afterBindings = JSON.stringify(bindingSnapshot());
assert.strictEqual(afterBindings, beforeBindings, '绑定关系发生变化，已中止验收');
const pending = db.prepare(`
  SELECT COUNT(*) AS count FROM command_queue
   WHERE device_id IN (${devices.map(() => '?').join(',')})
     AND purpose = 'shipping_reset_deferred' AND status = 'pending'
`).get(...devices.map(d => d.device_id)).count;
const zeroRows = db.prepare(`
  SELECT COUNT(*) AS count FROM device_status
   WHERE device_id IN (${devices.map(() => '?').join(',')})
     AND (run_count <> 0 OR run_time_s <> 0 OR total_run_ms <> 0 OR total_lift_count <> 0
       OR maintenance_lift_count <> 0 OR maintenance_count <> 0 OR last_maintenance_total <> 0
       OR maintenance_due <> 0)
`).get(...devices.map(d => d.device_id)).count;
assert.strictEqual(zeroRows, 0, '网站业务计数未全部清零');
console.log(JSON.stringify({ account, device_count: devices.length, pending_deferred: pending, zero_rows: zeroRows, results }, null, 2));
db.close();
