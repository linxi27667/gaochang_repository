const path = require('path');
const Database = require('better-sqlite3');

const TYPE_PREFIX = {
  double_post: '两柱',
  small_scissor: '小剪',
  thin_scissor: '超薄小剪',
  large_scissor: '大剪'
};

function naturalDeviceNumber(deviceId) {
  const match = String(deviceId || '').match(/(\d+)$/);
  return match ? Number(match[1]) : Number.MAX_SAFE_INTEGER;
}

function buildRenamePlan(rows) {
  const groups = new Map();
  for (const row of rows) {
    if (!TYPE_PREFIX[row.product_type]) continue;
    if (!groups.has(row.product_type)) groups.set(row.product_type, []);
    groups.get(row.product_type).push(row);
  }

  const plan = [];
  for (const [productType, devices] of groups) {
    devices.sort((a, b) => naturalDeviceNumber(a.device_id) - naturalDeviceNumber(b.device_id)
      || String(a.device_id).localeCompare(String(b.device_id)));
    devices.forEach((device, index) => {
      plan.push({
        device_id: device.device_id,
        product_type: productType,
        old_name: device.name,
        new_name: `${TYPE_PREFIX[productType]}${String(index + 1).padStart(3, '0')}`
      });
    });
  }
  return plan;
}

function main() {
  const dbArg = process.argv.find(arg => !arg.startsWith('--') && arg !== process.argv[0] && arg !== process.argv[1]);
  if (!dbArg) throw new Error('Usage: node tools/normalize_device_names.js <database-path> [--dry-run]');
  const dbPath = path.resolve(dbArg);
  const dryRun = process.argv.includes('--dry-run');
  const db = new Database(dbPath);
  const rows = db.prepare('SELECT device_id, name, product_type FROM devices').all();
  const plan = buildRenamePlan(rows);

  if (!dryRun) {
    const update = db.prepare('UPDATE devices SET name = ? WHERE device_id = ?');
    db.transaction(items => items.forEach(item => update.run(item.new_name, item.device_id)))(plan);
  }

  console.log(JSON.stringify({ db: dbPath, dry_run: dryRun, count: plan.length, devices: plan }, null, 2));
  db.close();
}

if (require.main === module) main();

module.exports = { buildRenamePlan, naturalDeviceNumber };
