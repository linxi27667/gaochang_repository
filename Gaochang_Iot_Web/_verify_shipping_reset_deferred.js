const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');

const tempDir = fs.mkdtempSync(path.join(os.tmpdir(), 'gaochang-shipping-deferred-'));
process.env.DB_PATH = path.join(tempDir, 'test.db');
process.env.JWT_SECRET = 'deferred-shipping-reset-test-secret';
const { getDb } = require('./src/database');
const { enqueueAndSendCommand } = require('./src/routes/commands');
const { initializeShippingResetWebsite } = require('./src/routes/admin');
const { _test } = require('./src/mqtt-bridge');
const db = getDb();
const deviceId = 'GC-DEFERRED-001';
const uid = '00112233445566778899aabb';
const now = new Date().toISOString();

db.prepare(`INSERT INTO devices (device_id, name, product_type, uid, bind_status, created_at)
  VALUES (?, '待发货设备', 'small_scissor', ?, 'bound', ?)`).run(deviceId, uid, now);
db.prepare(`INSERT INTO device_registry (serial, uid, product_type, display_name, status, bound_device_id, created_at)
  VALUES (?, ?, 'small_scissor', '小剪举升机', 'bound', ?, ?)`).run(deviceId, uid, deviceId, now);
db.prepare(`INSERT INTO device_status
  (device_id, online, run_count, run_time_s, total_run_ms, total_lift_count,
   maintenance_lift_count, maintenance_count, last_maintenance_total, maintenance_due, updated_at)
  VALUES (?, 0, 9, 888, 888000, 99, 44, 2, 55, 1, ?)`).run(deviceId, now);
db.prepare("INSERT INTO alarms (device_id, alarm_type, created_at) VALUES (?, 'fault', ?)").run(deviceId, now);
db.prepare("INSERT INTO maintenance_records (device_id, type, created_at) VALUES (?, '保养', ?)").run(deviceId, now);

const msgId = 'deferred-reset-001';
initializeShippingResetWebsite(db, deviceId, 1, msgId);
const queued = enqueueAndSendCommand(db, deviceId, 'reset_usage', msgId,
  { purpose: 'shipping_reset_deferred', deferIfOffline: true }, { id: 1, username: 'admin' });
assert.strictEqual(queued.status, 'pending');
assert.strictEqual(queued.deferred, true);
assert.strictEqual(db.prepare('SELECT status, purpose FROM command_queue WHERE msg_id = ?').get(msgId).purpose, 'shipping_reset_deferred');

const statusAfterInit = db.prepare('SELECT * FROM device_status WHERE device_id = ?').get(deviceId);
for (const field of ['run_count', 'run_time_s', 'total_run_ms', 'total_lift_count', 'maintenance_lift_count',
  'maintenance_count', 'last_maintenance_total', 'maintenance_due']) assert.strictEqual(statusAfterInit[field], 0);
assert.strictEqual(db.prepare('SELECT COUNT(*) count FROM alarms WHERE device_id = ?').get(deviceId).count, 0);
assert.strictEqual(db.prepare('SELECT COUNT(*) count FROM maintenance_records WHERE device_id = ?').get(deviceId).count, 0);

_test.handleStatusUpdate(deviceId, _test.normalizeTelemetry({
  uid, run_count: 123, run_time_s: 456, maintenance: {
    total_lift_count: 999, maintenance_lift_count: 88, maintenance_count: 7,
    last_maintenance_total: 888, maintenance_due: 1, usage_epoch: 0, maintenance_revision: 0
  }
}));
const protectedStatus = db.prepare('SELECT * FROM device_status WHERE device_id = ?').get(deviceId);
assert.strictEqual(protectedStatus.run_count, 0);
assert.strictEqual(protectedStatus.total_lift_count, 0);
assert.strictEqual(protectedStatus.maintenance_count, 0);
assert.strictEqual(db.prepare('SELECT COUNT(*) count FROM command_queue WHERE msg_id = ?').get(msgId).count, 1);

db.close();
fs.rmSync(tempDir, { recursive: true, force: true });
console.log('deferred shipping reset verification passed');
