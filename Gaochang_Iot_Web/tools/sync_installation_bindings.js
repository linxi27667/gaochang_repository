#!/usr/bin/env node
/*
 * 将现场安装纠正后的四台设备 UID 绑定同步到当前数据库。
 * 运行前请备份数据库；脚本会拒绝缺少设备或注册表记录的数据库。
 */
const fs = require('fs');
const path = require('path');
const Database = require('better-sqlite3');
require('dotenv').config();

const dbPath = path.resolve(process.env.DB_PATH || path.join(__dirname, '..', 'data', 'gaochang_lift.db'));
if (!fs.existsSync(dbPath)) throw new Error(`数据库不存在: ${dbPath}`);
const backupPath = `${dbPath}.before-installation-binding-swap-${new Date().toISOString().replace(/[:.]/g, '-')}`;
const db = new Database(dbPath);
const expected = [
  { deviceId: 'GC-WHSS2026713-008', uid: '002a00203335471736373130', productType: 'small_scissor' },
  { deviceId: 'GC-WHSS2026713-009', uid: '001b00343335471436373130', productType: 'small_scissor' },
  { deviceId: 'GC-WHTP2026713-002', uid: '002800343335471436373130', productType: 'double_post' },
  { deviceId: 'GC-WHTP2026713-001', uid: '002900203335471736373130', productType: 'double_post' }
];
const rows = () => expected.map(({ deviceId }) => ({
  device: db.prepare('SELECT device_id, name, model, product_type, uid, bind_status FROM devices WHERE device_id = ?').get(deviceId),
  registry: db.prepare('SELECT serial, uid, product_type, status, bound_device_id FROM device_registry WHERE serial = ?').get(deviceId)
}));
const assert = (condition, message) => { if (!condition) throw new Error(message); };

try {
  const before = rows();
  assert(before.every(({ device, registry }) => device && registry), '四台设备或注册表记录不完整');
  fs.copyFileSync(dbPath, backupPath, fs.constants.COPYFILE_EXCL);
  db.transaction(() => {
    expected.forEach(({ deviceId }, index) => {
      db.prepare('UPDATE devices SET uid = ? WHERE device_id = ?').run(`__swap_device_${index}__`, deviceId);
      db.prepare('UPDATE device_registry SET uid = ? WHERE serial = ?').run(`__swap_registry_${index}__`, deviceId);
    });
    expected.forEach(({ deviceId, uid, productType }) => {
      db.prepare('UPDATE devices SET uid = ?, product_type = ? WHERE device_id = ?').run(uid, productType, deviceId);
      db.prepare('UPDATE device_registry SET uid = ?, product_type = ?, status = ?, bound_device_id = ? WHERE serial = ?')
        .run(uid, productType, 'bound', deviceId, deviceId);
    });
  })();
  const after = rows();
  after.forEach(({ device, registry }) => {
    const target = expected.find((item) => item.deviceId === device.device_id);
    assert(device.uid === target.uid && device.product_type === target.productType, `设备映射错误: ${device.device_id}`);
    assert(registry.uid === target.uid && registry.product_type === target.productType && registry.bound_device_id === target.deviceId,
      `注册表映射错误: ${target.deviceId}`);
  });
  console.log(JSON.stringify({ dbPath, backupPath, after }, null, 2));
} finally {
  db.close();
}
