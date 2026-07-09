# Gaochang_Iot_Web

高昌举升机物联网 Web 管理端。该项目是 Node.js/Express 后端加静态前端页面，负责 MQTT 接入、设备状态入库、命令下发、报警/维修/日志/统计展示。

## 主要功能

- 连接 MQTT Broker，订阅 `gaochang/lift/#`。
- 接收固件 telemetry/status/operation log，写入 SQLite。
- 提供登录鉴权、设备管理、远程锁机/解锁、清报警、配置下发、产品类型切换等 API。
- WebSocket 推送 MQTT/设备状态到前端。
- 静态前端在 `public`，包含管理页面、多语言文本和基础样式。

## 目录说明

| 目录 | 内容 |
|---|---|
| `server.js` | Express 入口、路由挂载、MQTT 启动、WebSocket 服务 |
| `src` | 数据库、MQTT bridge、工具函数和 API 路由 |
| `src/routes` | auth/devices/commands/logs/alarms/maintenance/admin 等接口 |
| `public` | 前端 HTML/CSS/JS、logo 和多语言文本 |
| `tools` | MQTT 全链路和端到端测试脚本 |
| `docs` | 系统架构、管理中心、网关接入说明 |
| `.env.example` | 环境变量示例 |

## 关键配置

默认配置可通过环境变量覆盖：

```text
MQTT_BROKER=mqtt://192.168.1.10:1883
MQTT_TOPIC_PREFIX=gaochang/lift
MQTT_DEVICE_ID=gaochang_lift_f407zet6
PORT=3000
```

## 常用命令

```bash
npm install
npm start
npm run dev
```

## API 范围

- `/api/auth`：登录和鉴权。
- `/api/devices`：设备列表、设备资料和绑定关系。
- `/api/commands`：远程锁机、解锁、查询、清报警、配置和产品类型命令。
- `/api/logs`：操作日志。
- `/api/alarms`：报警记录。
- `/api/maintenance`：维修保养记录。
- `/api/device-ops`：固件上报的设备运行日志。
- `/api/mqtt-status`：MQTT 连接状态。
