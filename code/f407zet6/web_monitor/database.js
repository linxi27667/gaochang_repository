const Database = require('better-sqlite3');
const path = require('path');
const bcrypt = require('bcryptjs');

const DB_PATH = process.env.DB_PATH || path.join(__dirname, 'data', 'lift.db');

let db;

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
      role TEXT NOT NULL DEFAULT 'operator',
      real_name TEXT DEFAULT '',
      phone TEXT DEFAULT '',
      created_at TEXT NOT NULL DEFAULT '',
      last_login TEXT,
      enabled INTEGER DEFAULT 1
    );

    CREATE TABLE IF NOT EXISTS devices (
      device_id TEXT PRIMARY KEY,
      name TEXT NOT NULL,
      model TEXT DEFAULT 'TL-5000',
      group_name TEXT DEFAULT '默认分组',
      location TEXT DEFAULT '',
      has_encoder INTEGER DEFAULT 0,
      has_buzzer INTEGER DEFAULT 0,
      has_pressure_sensor INTEGER DEFAULT 0,
      has_display INTEGER DEFAULT 0,
      created_at TEXT NOT NULL DEFAULT ''
    );

    CREATE TABLE IF NOT EXISTS device_status (
      device_id TEXT PRIMARY KEY,
      online INTEGER DEFAULT 0,
      locked INTEGER DEFAULT 0,
      state TEXT DEFAULT 'idle',
      alarm TEXT DEFAULT 'none',
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
      left_up_collision INTEGER DEFAULT 0,
      right_up_collision INTEGER DEFAULT 0,
      left_down_collision INTEGER DEFAULT 0,
      right_down_collision INTEGER DEFAULT 0,
      buzzer_on INTEGER DEFAULT 0,
      FOREIGN KEY (device_id) REFERENCES devices(device_id) ON DELETE CASCADE
    );

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
  `);

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
  ensureColumn(db, 'device_status', 'left_up_collision', 'INTEGER DEFAULT 0');
  ensureColumn(db, 'device_status', 'right_up_collision', 'INTEGER DEFAULT 0');
  ensureColumn(db, 'device_status', 'left_down_collision', 'INTEGER DEFAULT 0');
  ensureColumn(db, 'device_status', 'right_down_collision', 'INTEGER DEFAULT 0');

  // 版本迁移：添加新字段（如果不存在）
  ensureColumn(db, 'devices', 'has_encoder', 'INTEGER DEFAULT 0');
  ensureColumn(db, 'devices', 'has_buzzer', 'INTEGER DEFAULT 0');
  ensureColumn(db, 'devices', 'has_pressure_sensor', 'INTEGER DEFAULT 0');
  ensureColumn(db, 'devices', 'has_display', 'INTEGER DEFAULT 0');
  ensureColumn(db, 'device_status', 'buzzer_on', 'INTEGER DEFAULT 0');

  const userCount = db.prepare('SELECT COUNT(*) AS cnt FROM users').get();
  if (userCount.cnt === 0) {
    const now = new Date().toISOString().replace('T', ' ').substring(0, 19);
    const hash = bcrypt.hashSync('admin123', 10);
    db.prepare('INSERT INTO users (username, password_hash, role, real_name, created_at) VALUES (?, ?, ?, ?, ?)')
      .run('admin', hash, 'admin', '系统管理员', now);

    const defaultDevices = [
      ['gaochang_lift_f407zet6', '高昌举升机F407', 'GC-LIFT-F407ZET6', '现场默认组'],
    ];
    const insDev = db.prepare('INSERT OR IGNORE INTO devices (device_id, name, model, group_name, created_at) VALUES (?, ?, ?, ?, ?)');
    const insStatus = db.prepare('INSERT OR IGNORE INTO device_status (device_id, updated_at) VALUES (?, ?)');
    for (const [id, name, model, grp] of defaultDevices) {
      insDev.run(id, name, model, grp, now);
      insStatus.run(id, now);
    }
  }
}

function ensureColumn(database, table, column, definition) {
  const cols = database.prepare(`PRAGMA table_info(${table})`).all();
  if (!cols.some((c) => c.name === column)) {
    database.exec(`ALTER TABLE ${table} ADD COLUMN ${column} ${definition}`);
  }
}

function getDb() {
  if (!db) init();
  return db;
}

module.exports = { init, getDb };
