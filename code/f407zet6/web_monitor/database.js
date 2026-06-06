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
      ts_ms INTEGER DEFAULT 0,
      updated_at TEXT NOT NULL DEFAULT '',
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

function getDb() {
  if (!db) init();
  return db;
}

module.exports = { init, getDb };
