# 高昌举升机物联网平台增强设计文档

日期：2026-06-27
状态：已批准

## 概述

为高昌举升机物联网监控平台添加用户注册、设备删除、多型号支持、配件系统、蜂鸣器控制等功能，准备上市。

## 技术栈

- 前端：Vanilla HTML/CSS/JavaScript SPA
- 后端：Node.js + Express.js
- 数据库：SQLite (better-sqlite3)
- 认证：JWT + bcryptjs
- 通信：MQTT + WebSocket

## 需求与设计

### 1. 用户开放注册

**前端 (index.html, app.js)**
- 登录页底部添加"注册账号"链接
- 点击弹出注册模态框，字段：用户名、密码、确认密码、真实姓名(选填)、手机号(选填)
- 简单数学验证码（如 `3 + 5 = ?`），纯前端生成，防机器人
- 注册成功后自动跳转登录页，显示成功提示

**后端 (auth.js)**
- 新增 `POST /api/auth/register-public` 路由，无需认证
- 新增 IP限流中间件 `rateLimit.js`：每IP每分钟最多3次注册，内存Map存储
- 用户名校验：字母数字下划线，3-20位，唯一性检查
- 密码校验：最少6位
- bcrypt加密存储
- 新用户默认 `viewer` 角色
- 注册成功记录 operation_logs

**安全措施**
- IP限流防暴力注册
- 数学验证码防机器人
- 用户名/密码格式校验
- 用户名唯一性约束

### 2. 设备删除功能

**前端 (app.js)**
- 设备管理页每行操作列添加红色"删除"按钮
- 点击弹出确认对话框："确定要删除设备 {name} 吗？删除后数据不可恢复。"
- 确认后调用 `DELETE /api/devices/:id`
- 删除成功后刷新设备列表，WebSocket通知其他在线客户端

**后端 (devices.js)**
- 已有 `DELETE /api/devices/:id` 路由
- 补充级联删除SQL：删除 devices、device_status、alarms、maintenance_records、operation_logs、command_queue 中该设备的所有数据
- 使用事务确保原子性
- 删除前检查设备是否在线，在线则先发送锁定命令

### 3. 多型号支持

**数据库 (database.js)**
- `devices` 表 `model` 字段保持 TEXT 类型
- 定义型号常量数组：`['GC-4.0sle', 'GC-4.0sb', 'GC-4.0MSL', 'GC-4.0PRO-DW']`

**前端 (app.js)**
- 添加设备模态框：model字段从 `<input>` 改为 `<select>`，选项为4个型号
- 编辑设备时同样显示下拉框，预选当前型号
- 设备卡片和详情页显示完整型号名称
- 型号列表集中定义在 `app.js` 顶部常量中，方便后续扩展

**后端 (devices.js)**
- 创建/更新设备时校验model值是否在允许列表中

### 4. 警报}号修复

**前端 (app.js)**
- 第637行：`${t('alarms.resolve')}` 后补全 `</button>`
- 修改为：`${t('alarms.resolve')}</button>`

### 5. 设备配件系统

**数据库 (database.js)**
- `devices` 表新增4个布尔字段（INTEGER, 默认0）：
  - `has_encoder` - 高度编码器
  - `has_buzzer` - 蜂鸣器
  - `has_pressure_sensor` - 压力传感器
  - `has_display` - 显示屏
- 数据库版本迁移：ALTER TABLE 添加字段

**前端 (app.js)**
- 添加/编辑设备模态框：添加"设备配件"区域，4个复选框
- 设备详情页：根据配件配置显示/隐藏对应面板
  - has_encoder=true 时显示高度信息
  - has_buzzer=true 时显示蜂鸣器状态和控制按钮
  - has_pressure_sensor=true 时显示压力数据
  - has_display=true 时显示显示屏状态
- 设备卡片：用小图标标识已安装配件

**后端**
- 创建/更新设备时接收并存储配件字段
- GET /api/devices 返回配件配置

### 6. 蜂鸣器MQTT控制

**前端 (app.js)**
- 设备详情页：仅当 has_buzzer=true 时显示
- 显示当前蜂鸣器状态（开/关）
- "开启蜂鸣器"/"关闭蜂鸣器" 按钮
- 调用 `POST /api/commands/buzzer_on/:deviceId` 或 `buzzer_off/:deviceId`

**后端 (commands.js)**
- 新增 `POST /api/commands/buzzer_on/:deviceId`
- 新增 `POST /api/commands/buzzer_off/:deviceId`
- 复用现有command_queue机制，通过MQTT发送命令
- MQTT payload: `{ "cmd": "buzzer_on" }` 或 `{ "cmd": "buzzer_off" }`

**数据库 (database.js)**
- `device_status` 表新增 `buzzer_on` 字段 (INTEGER, 默认0)

**MQTT (mqtt-bridge.js)**
- 在 status 消息处理中解析 `buzzer_on` 字段
- 更新 device_status 表
- 通过WebSocket推送到前端

### 7. 管理后台增强

**前端 (app.js)**
- 操作日志页添加筛选栏：
  - "操作用户"下拉框（从/users接口获取用户列表）
  - "设备"下拉框（从/devices接口获取设备列表）
  - 时间范围选择器（开始日期-结束日期）
- 筛选变化时重新请求日志数据

**后端 (logs.js)**
- `GET /api/logs` 路由添加查询参数：
  - `userId` - 按用户筛选
  - `deviceId` - 按设备筛选
  - `startTime` / `endTime` - 按时间范围筛选
- 分页支持：`page`, `pageSize` 参数

### 8. 代码质量（AI友好）

- 关键函数添加简短中文注释
- 前端页面渲染函数统一命名：`render{PageName}Page()`
- API路由在 server.js 中按模块分组注册
- 数据库schema集中在 database.js

## 涉及文件

| 文件 | 改动类型 |
|------|----------|
| database.js | 新增字段、版本迁移 |
| auth.js | 新增注册路由、限流中间件 |
| devices.js | 增强删除、型号校验、配件字段 |
| commands.js | 新增蜂鸣器命令 |
| mqtt-bridge.js | 解析蜂鸣器状态 |
| logs.js | 增强筛选查询 |
| server.js | 注册新路由 |
| public/index.html | 注册模态框、设备表单改动 |
| public/app.js | 所有前端改动 |
| public/style.css | 新增样式（注册表单、配件复选框等） |
| public/i18n.js | 新增翻译词条 |

## 实施顺序

1. Bug修复：警报}号
2. 数据库：新增字段和迁移
3. 后端：注册、删除、蜂鸣器命令、日志筛选
4. 前端：注册、设备管理、配件系统、蜂鸣器控制、日志筛选
5. 测试验证
