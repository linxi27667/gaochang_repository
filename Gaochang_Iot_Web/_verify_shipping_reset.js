const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');

const tempDir = fs.mkdtempSync(path.join(os.tmpdir(), 'gaochang-shipping-reset-'));
process.env.DB_PATH = path.join(tempDir, 'test.db');

const { getDb } = require('./src/database');
const { _test } = require('./src/mqtt-bridge');
const db = getDb();
const deviceId = 'GC-TEST-001';
const uid = '00112233445566778899aabb';
const serial = 'GC-TEST-SN-001';
const now = new Date().toISOString();

const userId = db.prepare("INSERT INTO users (username, password_hash, role, real_name, created_at) VALUES ('shipping-user', 'x', 'user', 'Shipping User', ?)").run(now).lastInsertRowid;
db.prepare(`INSERT INTO devices (device_id, name, product_type, uid, bind_status, created_at)
  VALUES (?, '原设备名', 'small_scissor', ?, 'bound', ?)`).run(deviceId, uid, now);
db.prepare(`INSERT INTO device_registry (serial, uid, product_type, display_name, status, bound_device_id, created_at)
  VALUES (?, ?, 'small_scissor', '小剪举升机', 'bound', ?, ?)`).run(serial, uid, deviceId, now);
db.prepare(`INSERT INTO device_bindings (device_id, user_id, status, bind_type, bound_at)
  VALUES (?, ?, 'active', 'normal', ?)`).run(deviceId, userId, now);
db.prepare(`INSERT INTO device_status
  (device_id, online, run_count, run_time_s, total_run_ms, up_count, down_count, lock_count,
   refill_count, estop_count, photo_alarm_count, total_lift_count, maintenance_lift_count,
   maintenance_threshold, maintenance_count, last_maintenance_total, maintenance_due,
   usage_epoch, maintenance_revision, last_run_at, updated_at)
  VALUES (?, 1, 9, 888, 888000, 3, 4, 5, 6, 7, 8, 99, 44, 5000, 2, 55, 1, 6, 3, ?, ?)`)
  .run(deviceId, now, now);
db.prepare("INSERT INTO alarms (device_id, alarm_type, created_at) VALUES (?, 'fault', ?)").run(deviceId, now);
db.prepare("INSERT INTO maintenance_records (device_id, type, created_at) VALUES (?, '保养', ?)").run(deviceId, now);
db.prepare(`INSERT INTO device_operation_logs
  (device_uid, device_serial, op_type, occurred_at, received_at) VALUES (?, ?, 'up', ?, ?)`)
  .run(uid, serial, now, now);
db.prepare(`INSERT INTO command_queue (device_id, cmd, msg_id, status, created_at, purpose)
  VALUES (?, 'lock', 'old-command', 'succeeded', ?, '')`).run(deviceId, now);
db.prepare(`INSERT INTO command_queue (device_id, cmd, msg_id, status, created_at, operator_id, purpose)
  VALUES (?, 'reset_usage', 'shipping-command', 'succeeded', ?, ?, 'shipping_reset')`).run(deviceId, now, userId);

const preservedBefore = {
  device: db.prepare('SELECT device_id, uid, product_type, bind_status FROM devices WHERE device_id = ?').get(deviceId),
  registry: db.prepare('SELECT serial, uid, product_type, status, bound_device_id FROM device_registry WHERE uid = ?').get(uid),
  binding: db.prepare('SELECT device_id, user_id, status, bind_type FROM device_bindings WHERE device_id = ?').get(deviceId)
};

_test.applyCommandResultToDevice(deviceId, 'reset_usage', 'succeeded', {
  maintenance: {
    total_lift_count: 0,
    maintenance_lift_count: 0,
    maintenance_threshold: 5000,
    maintenance_count: 0,
    last_maintenance_total: 0,
    maintenance_due: 0,
    usage_epoch: 7,
    maintenance_revision: 4
  }
}, 'shipping-command');

const status = db.prepare('SELECT * FROM device_status WHERE device_id = ?').get(deviceId);
for (const field of ['run_count', 'run_time_s', 'total_run_ms', 'up_count', 'down_count', 'lock_count',
  'refill_count', 'estop_count', 'photo_alarm_count', 'total_lift_count', 'maintenance_lift_count',
  'maintenance_count', 'last_maintenance_total', 'maintenance_due']) {
  assert.strictEqual(status[field], 0, `${field} should be reset`);
}
assert.strictEqual(status.usage_epoch, 7);
assert.strictEqual(status.maintenance_revision, 4);
assert.strictEqual(status.maintenance_threshold, 5000);
assert.strictEqual(status.last_run_at, null);
assert.strictEqual(db.prepare('SELECT COUNT(*) count FROM alarms WHERE device_id = ?').get(deviceId).count, 0);
assert.strictEqual(db.prepare('SELECT COUNT(*) count FROM maintenance_records WHERE device_id = ?').get(deviceId).count, 0);
assert.strictEqual(db.prepare('SELECT COUNT(*) count FROM device_operation_logs WHERE device_uid = ?').get(uid).count, 0);
assert.strictEqual(db.prepare('SELECT COUNT(*) count FROM command_queue WHERE device_id = ?').get(deviceId).count, 1);
assert.ok(db.prepare("SELECT 1 FROM command_queue WHERE msg_id = 'shipping-command'").get());
assert.deepStrictEqual(db.prepare('SELECT device_id, uid, product_type, bind_status FROM devices WHERE device_id = ?').get(deviceId), preservedBefore.device);
assert.deepStrictEqual(db.prepare('SELECT serial, uid, product_type, status, bound_device_id FROM device_registry WHERE uid = ?').get(uid), preservedBefore.registry);
assert.deepStrictEqual(db.prepare('SELECT device_id, user_id, status, bind_type FROM device_bindings WHERE device_id = ?').get(deviceId), preservedBefore.binding);
assert.ok(db.prepare("SELECT 1 FROM operation_logs WHERE action = 'shipping_reset' AND device_id = ? AND result = 'completed'").get(deviceId));

db.close();
fs.rmSync(tempDir, { recursive: true, force: true });
console.log('shipping reset verification passed');
