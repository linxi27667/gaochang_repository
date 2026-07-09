const Database = require('better-sqlite3');
const path = require('path');
const bcrypt = require('bcryptjs');

const DB_PATH = process.env.DB_PATH || path.join(__dirname, '..', 'data', 'lift.db');

let db;

// 产品型号元数据(供 Web 端动态渲染使用)
const PRODUCT_CONFIGS = [
  {
    product_type: 'double_post',
    display_name: '两柱举升机',
    inputs_json: '["btn_up","btn_down","btn_lock","estop","limit_up"]',
    outputs_json: '["motor","solenoid","valve_drop"]',
    default_motor_hold_ms: 2000,
    default_motor_to_valve_delay_ms: 200,
    has_encoder: 0,
    has_refill: 0,
    has_photoelectric: 0,
    has_rotary: 0,
    has_limit_down: 0
  },
  {
    product_type: 'small_scissor',
    display_name: '小剪举升机',
    inputs_json: '["btn_up","btn_down","btn_lock","estop","limit_up","btn_refill","photoelectric"]',
    outputs_json: '["motor","valve_air","valve_drop"]',
    default_motor_hold_ms: 3000,
    default_motor_to_valve_delay_ms: 200,
    has_encoder: 0,
    has_refill: 1,
    has_photoelectric: 1,
    has_rotary: 0,
    has_limit_down: 0
  },
  {
    product_type: 'thin_scissor',
    display_name: '超薄小剪举升机',
    inputs_json: '["btn_up","btn_down","btn_lock","estop","limit_up","limit_down","btn_refill","photoelectric"]',
    outputs_json: '["motor","valve_air","valve_drop"]',
    default_motor_hold_ms: 3000,
    default_motor_to_valve_delay_ms: 200,
    has_encoder: 0,
    has_refill: 1,
    has_photoelectric: 1,
    has_rotary: 0,
    has_limit_down: 1
  },
  {
    product_type: 'large_scissor',
    display_name: '大剪举升机',
    inputs_json: '["btn_up","btn_down","btn_lock","estop","limit_up","btn_refill","photoelectric","rotary"]',
    outputs_json: '["motor","valve_air_main","valve_air_sub","valve_drop","valve_work_main","valve_work_sub"]',
    default_motor_hold_ms: 3000,
    default_motor_to_valve_delay_ms: 200,
    has_encoder: 0,
    has_refill: 1,
    has_photoelectric: 1,
    has_rotary: 1,
    has_limit_down: 0
  }
];

function init() {
  const fs = require('fs');
  const dir = path.dirname(DB_PATH);
  if (!fs.existsSync(dir)) fs.mkdirSync(dir, { recursive: true });

  db = new Database(DB_PATH);
  db.pragma('journal_mode = WAL');
  db.pragma('foreign_keys = ON');

  db.exec(`
    CREATE TABLE IF NOT EXISTS users (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      username TEXT UNIQUE NOT NULL,
      password_hash TEXT NOT NULL,
      role TEXT NOT NULL DEFAULT 'user',
      real_name TEXT DEFAULT '',
      phone TEXT DEFAULT '',
      created_at TEXT NOT NULL DEFAULT '',
      last_login TEXT,
      enabled INTEGER DEFAULT 1,
      must_change_password INTEGER DEFAULT 0
    );

    CREATE TABLE IF NOT EXISTS devices (
      device_id TEXT PRIMARY KEY,
      name TEXT NOT NULL,
      model TEXT DEFAULT '',
      group_name TEXT DEFAULT '默认分组',
      location TEXT DEFAULT '',
      gateway_id TEXT DEFAULT '',
      product_type TEXT DEFAULT 'double_post',
      lift_role TEXT DEFAULT 'main',
      uid TEXT DEFAULT '',
      owner_id INTEGER DEFAULT NULL,
      bind_status TEXT DEFAULT 'unbound',
      bound_at TEXT,
      has_encoder INTEGER DEFAULT 0,
      has_buzzer INTEGER DEFAULT 0,
      has_pressure_sensor INTEGER DEFAULT 0,
      has_display INTEGER DEFAULT 0,
      created_at TEXT NOT NULL DEFAULT ''
    );
    CREATE INDEX IF NOT EXISTS idx_devices_uid ON devices(uid);
    CREATE INDEX IF NOT EXISTS idx_devices_owner ON devices(owner_id);

    CREATE TABLE IF NOT EXISTS device_status (
      device_id TEXT PRIMARY KEY,
      online INTEGER DEFAULT 0,
      locked INTEGER DEFAULT 0,
      state TEXT DEFAULT 'idle',
      alarm TEXT DEFAULT 'none',
      rotary_switch TEXT DEFAULT 'main',
      height_left_mm INTEGER DEFAULT 0,
      height_right_mm INTEGER DEFAULT 0,
      height_diff_mm INTEGER DEFAULT 0,
      run_count INTEGER DEFAULT 0,
      run_time_s INTEGER DEFAULT 0,
      uptime_s INTEGER DEFAULT 0,
      ts_ms INTEGER DEFAULT 0,
      updated_at TEXT NOT NULL DEFAULT '',
      direction TEXT DEFAULT 'stop',
      upper_limit INTEGER DEFAULT 0,
      lower_limit INTEGER DEFAULT 0,
      stall INTEGER DEFAULT 0,
      collision_up INTEGER DEFAULT 0,
      collision_down INTEGER DEFAULT 0,
      alarm_code INTEGER DEFAULT 0,
      csq INTEGER DEFAULT -1,
      dtu_state TEXT DEFAULT '',
      left_pulse INTEGER DEFAULT 0,
      right_pulse INTEGER DEFAULT 0,
      buzzer_on INTEGER DEFAULT 0,
      up_count INTEGER DEFAULT 0,
      down_count INTEGER DEFAULT 0,
      lock_count INTEGER DEFAULT 0,
      refill_count INTEGER DEFAULT 0,
      estop_count INTEGER DEFAULT 0,
      photo_alarm_count INTEGER DEFAULT 0,
      total_run_ms INTEGER DEFAULT 0,
      last_run_at TEXT,
      io_input_json TEXT DEFAULT '{}',
      io_output_json TEXT DEFAULT '{}',
      FOREIGN KEY (device_id) REFERENCES devices(device_id) ON DELETE CASCADE
    );

    CREATE TABLE IF NOT EXISTS unbound_device_status (
      device_id TEXT PRIMARY KEY,
      uid TEXT DEFAULT '',
      serial TEXT DEFAULT '',
      product_type TEXT DEFAULT 'double_post',
      gateway_id TEXT DEFAULT '',
      online INTEGER DEFAULT 0,
      state TEXT DEFAULT 'idle',
      alarm TEXT DEFAULT 'none',
      ts_ms INTEGER DEFAULT 0,
      updated_at TEXT NOT NULL DEFAULT '',
      status_json TEXT DEFAULT '{}'
    );
    CREATE INDEX IF NOT EXISTS idx_unbound_status_uid ON unbound_device_status(uid);
    CREATE INDEX IF NOT EXISTS idx_unbound_status_serial ON unbound_device_status(serial);

    CREATE TABLE IF NOT EXISTS alarms (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      device_id TEXT NOT NULL,
      alarm_type TEXT NOT NULL,
      message TEXT DEFAULT '',
      level TEXT DEFAULT 'warning',
      acknowledged INTEGER DEFAULT 0,
      created_at TEXT NOT NULL DEFAULT '',
      resolved_at TEXT,
      FOREIGN KEY (device_id) REFERENCES devices(device_id) ON DELETE CASCADE
    );

    CREATE TABLE IF NOT EXISTS maintenance_records (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      device_id TEXT NOT NULL,
      type TEXT NOT NULL DEFAULT '保养',
      description TEXT DEFAULT '',
      handler TEXT DEFAULT '',
      result TEXT DEFAULT '',
      next_date TEXT DEFAULT '',
      cost REAL DEFAULT 0,
      created_at TEXT NOT NULL DEFAULT '',
      FOREIGN KEY (device_id) REFERENCES devices(device_id) ON DELETE CASCADE
    );

    -- 平台操作日志(用户/管理员在 Web 端的操作,如登录/绑定/配置下发)
    CREATE TABLE IF NOT EXISTS operation_logs (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      user_id INTEGER,
      action TEXT NOT NULL,
      device_id TEXT DEFAULT '',
      detail TEXT DEFAULT '',
      result TEXT DEFAULT '',
      created_at TEXT NOT NULL DEFAULT '',
      FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE SET NULL
    );

    -- 设备端操作日志(工人在设备上的物理操作,通过 MQTT 上报)
    CREATE TABLE IF NOT EXISTS device_operation_logs (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      device_uid TEXT NOT NULL,
      device_serial TEXT NOT NULL DEFAULT '',
      op_type TEXT NOT NULL,
      op_result TEXT DEFAULT 'ok',
      duration_ms INTEGER DEFAULT 0,
      detail TEXT DEFAULT '',
      device_state TEXT DEFAULT '',
      occurred_at TEXT NOT NULL,
      received_at TEXT NOT NULL
    );
    CREATE INDEX IF NOT EXISTS idx_devop_uid ON device_operation_logs(device_uid);
    CREATE INDEX IF NOT EXISTS idx_devop_type ON device_operation_logs(op_type);
    CREATE INDEX IF NOT EXISTS idx_devop_time ON device_operation_logs(occurred_at);

    CREATE TABLE IF NOT EXISTS command_queue (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      device_id TEXT NOT NULL,
      cmd TEXT NOT NULL,
      msg_id TEXT NOT NULL,
      status TEXT DEFAULT 'pending',
      result TEXT DEFAULT '',
      created_at TEXT NOT NULL DEFAULT '',
      responded_at TEXT
    );

    -- 出厂设备注册表(SN <-> UID 映射)
    CREATE TABLE IF NOT EXISTS device_registry (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      serial TEXT UNIQUE NOT NULL,
      uid TEXT UNIQUE NOT NULL,
      product_type TEXT NOT NULL DEFAULT 'double_post',
      display_name TEXT DEFAULT '',
      model TEXT DEFAULT '',
      batch TEXT DEFAULT '',
      produced_at TEXT DEFAULT '',
      has_encoder INTEGER DEFAULT 0,
      has_buzzer INTEGER DEFAULT 0,
      has_pressure_sensor INTEGER DEFAULT 0,
      has_display INTEGER DEFAULT 0,
      status TEXT DEFAULT 'unbound',
      bound_device_id TEXT DEFAULT '',
      created_at TEXT NOT NULL DEFAULT ''
    );
    CREATE INDEX IF NOT EXISTS idx_registry_uid ON device_registry(uid);
    CREATE INDEX IF NOT EXISTS idx_registry_serial ON device_registry(serial);

    CREATE TABLE IF NOT EXISTS binding_logs (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      uid TEXT NOT NULL DEFAULT '',
      serial TEXT NOT NULL DEFAULT '',
      device_id TEXT NOT NULL DEFAULT '',
      user_id INTEGER,
      action TEXT NOT NULL,
      ip TEXT DEFAULT '',
      detail TEXT DEFAULT '',
      created_at TEXT NOT NULL DEFAULT ''
    );
    CREATE INDEX IF NOT EXISTS idx_binding_logs_uid ON binding_logs(uid);
    CREATE INDEX IF NOT EXISTS idx_binding_logs_user ON binding_logs(user_id);

    -- 产品型号元数据表
    CREATE TABLE IF NOT EXISTS product_configs (
      product_type TEXT PRIMARY KEY,
      display_name TEXT NOT NULL,
      inputs_json TEXT NOT NULL,
      outputs_json TEXT NOT NULL,
      default_motor_hold_ms INTEGER NOT NULL,
      default_motor_to_valve_delay_ms INTEGER NOT NULL,
      has_encoder INTEGER DEFAULT 0,
      has_refill INTEGER DEFAULT 0,
      has_photoelectric INTEGER DEFAULT 0,
      has_rotary INTEGER DEFAULT 0,
      has_limit_down INTEGER DEFAULT 0
    );
  `);

  // 兼容旧库的列迁移
  ensureColumn(db, 'device_status', 'uptime_s', 'INTEGER DEFAULT 0');
  ensureColumn(db, 'device_status', 'direction', "TEXT DEFAULT 'stop'");
  ensureColumn(db, 'device_status', 'upper_limit', 'INTEGER DEFAULT 0');
  ensureColumn(db, 'device_status', 'lower_limit', 'INTEGER DEFAULT 0');
  ensureColumn(db, 'device_status', 'stall', 'INTEGER DEFAULT 0');
  ensureColumn(db, 'device_status', 'collision_up', 'INTEGER DEFAULT 0');
  ensureColumn(db, 'device_status', 'collision_down', 'INTEGER DEFAULT 0');
  ensureColumn(db, 'device_status', 'alarm_code', 'INTEGER DEFAULT 0');
  ensureColumn(db, 'device_status', 'csq', 'INTEGER DEFAULT -1');
  ensureColumn(db, 'device_status', 'dtu_state', "TEXT DEFAULT ''");
  ensureColumn(db, 'device_status', 'left_pulse', 'INTEGER DEFAULT 0');
  ensureColumn(db, 'device_status', 'right_pulse', 'INTEGER DEFAULT 0');
  ensureColumn(db, 'device_status', 'buzzer_on', 'INTEGER DEFAULT 0');
  // 碰撞检测明细列(被 mqtt-bridge.js / ai-context.js 引用)
  ensureColumn(db, 'device_status', 'left_up_collision', 'INTEGER DEFAULT 0');
  ensureColumn(db, 'device_status', 'right_up_collision', 'INTEGER DEFAULT 0');
  ensureColumn(db, 'device_status', 'left_down_collision', 'INTEGER DEFAULT 0');
  ensureColumn(db, 'device_status', 'right_down_collision', 'INTEGER DEFAULT 0');

  ensureColumn(db, 'devices', 'has_encoder', 'INTEGER DEFAULT 0');
  ensureColumn(db, 'devices', 'has_buzzer', 'INTEGER DEFAULT 0');
  ensureColumn(db, 'devices', 'has_pressure_sensor', 'INTEGER DEFAULT 0');
  ensureColumn(db, 'devices', 'has_display', 'INTEGER DEFAULT 0');
  ensureColumn(db, 'devices', 'owner_id', 'INTEGER DEFAULT NULL');
  ensureColumn(db, 'devices', 'uid', "TEXT DEFAULT ''");
  ensureColumn(db, 'devices', 'gateway_id', "TEXT DEFAULT ''");
  ensureColumn(db, 'devices', 'bind_status', "TEXT DEFAULT 'unbound'");
  ensureColumn(db, 'devices', 'bound_at', 'TEXT');
  ensureColumn(db, 'devices', 'product_type', "TEXT DEFAULT 'double_post'");
  ensureColumn(db, 'devices', 'lift_role', "TEXT DEFAULT 'main'");

  ensureColumn(db, 'users', 'must_change_password', 'INTEGER DEFAULT 0');

  // 设备状态表新增多产品字段
  ensureColumn(db, 'device_status', 'rotary_switch', "TEXT DEFAULT 'main'");
  ensureColumn(db, 'device_status', 'up_count', 'INTEGER DEFAULT 0');
  ensureColumn(db, 'device_status', 'down_count', 'INTEGER DEFAULT 0');
  ensureColumn(db, 'device_status', 'lock_count', 'INTEGER DEFAULT 0');
  ensureColumn(db, 'device_status', 'refill_count', 'INTEGER DEFAULT 0');
  ensureColumn(db, 'device_status', 'estop_count', 'INTEGER DEFAULT 0');
  ensureColumn(db, 'device_status', 'photo_alarm_count', 'INTEGER DEFAULT 0');
  ensureColumn(db, 'device_status', 'total_run_ms', 'INTEGER DEFAULT 0');
  ensureColumn(db, 'device_status', 'last_run_at', 'TEXT');
  ensureColumn(db, 'device_status', 'io_input_json', "TEXT DEFAULT '{}'");
  ensureColumn(db, 'device_status', 'io_output_json', "TEXT DEFAULT '{}'");

  // device_registry 加 product_type / display_name
  ensureColumn(db, 'device_registry', 'product_type', "TEXT DEFAULT 'double_post'");
  ensureColumn(db, 'device_registry', 'display_name', "TEXT DEFAULT ''");
  ensureColumn(db, 'device_registry', 'has_encoder', 'INTEGER DEFAULT 0');
  ensureColumn(db, 'device_registry', 'has_buzzer', 'INTEGER DEFAULT 0');
  ensureColumn(db, 'device_registry', 'has_pressure_sensor', 'INTEGER DEFAULT 0');
  ensureColumn(db, 'device_registry', 'has_display', 'INTEGER DEFAULT 0');

  ensureColumn(db, 'product_configs', 'has_encoder', 'INTEGER DEFAULT 0');

  // 角色统一化迁移:把旧的多角色(admin/superadmin/operator/viewer)合并为两层 admin/user
  migrateRolesToTwoLayer(db);

  // 写入产品型号元数据(每次启动同步)
  const upsertProduct = db.prepare(`
    INSERT INTO product_configs (product_type, display_name, inputs_json, outputs_json,
      default_motor_hold_ms, default_motor_to_valve_delay_ms,
      has_encoder, has_refill, has_photoelectric, has_rotary, has_limit_down)
    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    ON CONFLICT(product_type) DO UPDATE SET
      display_name=excluded.display_name,
      inputs_json=excluded.inputs_json,
      outputs_json=excluded.outputs_json,
      default_motor_hold_ms=excluded.default_motor_hold_ms,
      default_motor_to_valve_delay_ms=excluded.default_motor_to_valve_delay_ms,
      has_encoder=excluded.has_encoder,
      has_refill=excluded.has_refill,
      has_photoelectric=excluded.has_photoelectric,
      has_rotary=excluded.has_rotary,
      has_limit_down=excluded.has_limit_down
  `);
  for (const p of PRODUCT_CONFIGS) {
    upsertProduct.run(
      p.product_type, p.display_name, p.inputs_json, p.outputs_json,
      p.default_motor_hold_ms, p.default_motor_to_valve_delay_ms,
      p.has_encoder, p.has_refill, p.has_photoelectric, p.has_rotary, p.has_limit_down
    );
  }

  // 创建默认管理员账号
  ensureDefaultAdmin(db);
}

function ensureColumn(database, table, column, definition) {
  const cols = database.prepare(`PRAGMA table_info(${table})`).all();
  if (!cols.some((c) => c.name === column)) {
    database.exec(`ALTER TABLE ${table} ADD COLUMN ${column} ${definition}`);
  }
}

// 把旧角色(admin/superadmin/operator/viewer)统一为 admin/user 两层
function migrateRolesToTwoLayer(database) {
  // superadmin / admin -> admin
  database.exec(`UPDATE users SET role = 'admin' WHERE role IN ('superadmin', 'admin')`);
  // operator / viewer -> user
  database.exec(`UPDATE users SET role = 'user' WHERE role IN ('operator', 'viewer', 'user')`);
  // 防御性:任何非 admin/user 的角色统一降为 user
  database.exec(`UPDATE users SET role = 'user' WHERE role NOT IN ('admin', 'user')`);
}

function ensureDefaultAdmin(database) {
  const now = new Date().toISOString().replace('T', ' ').substring(0, 19);

  // 默认网站管理员账号
  const admin = database.prepare('SELECT id FROM users WHERE username = ?').get('admin');
  if (!admin) {
    const hash = bcrypt.hashSync('admin123', 10);
    database.prepare(
      'INSERT OR IGNORE INTO users (username, password_hash, role, real_name, created_at) VALUES (?, ?, ?, ?, ?)'
    ).run('admin', hash, 'admin', '网站管理员', now);
  } else {
    // 旧库存在 admin,确保角色为 admin
    database.prepare("UPDATE users SET role = 'admin' WHERE username = 'admin'").run();
  }
}

function getDb() {
  if (!db) init();
  return db;
}

module.exports = { init, getDb, PRODUCT_CONFIGS };
