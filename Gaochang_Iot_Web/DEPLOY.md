# 高昌举升机物联网管理平台 · 部署说明

Web/API 端口默认 **3100**，MQTT Broker 默认 **1883**（本机）。

## 环境要求

- Node.js **>= 18**（推荐 20/22 LTS），npm
- Linux 服务器（本项目在 Ubuntu/Debian 验证）
- MQTT Broker：mosquitto（或兼容 broker），监听 1883
- 构建 better-sqlite3 原生模块需要：`build-essential`、`python3`

## 快速部署

```bash
# 1. 安装依赖（node_modules 未打包，需重新安装）
cd Gaochang_Iot_Web
npm ci          # 或 npm install

# 2. 安装并启动 MQTT Broker（若目标机没有）
apt-get install -y mosquitto
systemctl enable --now mosquitto

# 3. 按需修改 .env（重要！）
vim .env
#    - JWT_SECRET    改为足够长的随机串（>=16字符）
#    - BIND_CODE_SALT 改为随机盐值（>=32字符）
#    - MIMO_API_KEY  按实际填（AI 能力，不用可留空）
#    - MQTT_BROKER   默认 mqtt://127.0.0.1:1883，现场服务器改为实际地址
#    - PORT          默认 3100

# 4. 启动
node server.js
# 或用 pm2:  pm2 start server.js --name lift-monitor
# 或用 systemd（见下方示例）

# 5. 防火墙
#    放行 3100（Web/API）
#    放行 1883（MQTT，设备/DTU 需要连）
```

## MQTT endpoint

- Hardware DTUs connect to `mqtt.gclift.net:1883` through a DNS-only A record.
- The Node service stays on the local broker: `mqtt://127.0.0.1:1883`.
- Keep the MQTT topics, port, and anonymous-access policy unchanged during the domain migration.
- When moving servers, deploy Mosquitto and this service first, then change the DNS A record.

## 目录结构

| 路径 | 说明 |
|---|---|
| `server.js` | Express 入口（Web + API + MQTT + WebSocket） |
| `src/` | 路由 / 数据库 / MQTT bridge |
| `public/` | 前端静态页面（由 server.js 托管） |
| `data/gaochang_lift.db` | SQLite 数据库（已含现有完整数据：用户/设备/绑定/日志） |
| `docs/` | 系统架构、网关接入说明 |
| `tools/` | 测试脚本 |

## systemd 服务示例

```ini
# /etc/systemd/system/lift-monitor.service
[Unit]
Description=Gaochang Lift IoT Monitor
After=network.target mosquitto.service

[Service]
WorkingDirectory=/opt/Gaochang_Iot_Web
ExecStart=/usr/bin/node server.js
Restart=always
User=root

[Install]
WantedBy=multi-user.target
```

```bash
systemctl daemon-reload && systemctl enable --now lift-monitor
```

## 升级固件/设备接入

- 设备遥测走 MQTT 主题 `gaochang/lift/v1/<chip_uid>/telemetry`（v1 协议）
- 设备必须先登记进「设备注册表」（管理后台），固件上报 UID 与注册表一致才会入库
- 注册表 product_type 与固件上报型号不一致会自动隔离设备

## ⚠️ 安全提醒

- 包内含 `.env`（真实 JWT_SECRET / MIMO_API_KEY）及数据库用户数据，**请勿外传**，妥善保管
- 生产部署务必更换 `JWT_SECRET`、`BIND_CODE_SALT`，否则旧密钥可能被用于伪造登录
- 管理后台默认账号：admin / GaoChang（部署后请立即改密码）
