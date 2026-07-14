# 更新日志 (Changelog)

本文件记录 `web_monitor`（举升机网页监控）的重要变更。

---

## 2026-06-27 — 设备管理与公开注册体系

**提交区间**：`d4fee71..09b5e38`（共 9 个提交）
**相对基线**：`origin/main` 在 `d4fee71`（推送前）
**代码量**：+1948 行 / −54 行，涉及 9 个文件

### 新增功能

- **公开用户注册**：新增公开注册接口（含验证码），并配套 IP 限流中间件，防止刷注册。
- **设备配件 & 蜂鸣器状态**：数据库 schema 新增设备配件字段与蜂鸣器状态字段。
- **设备级联删除**：删除设备时一并清理其所有关联记录。
- **设备管理**：支持删除、型号选择、配件管理。
- **操作日志筛选**：操作日志页新增「用户」与「日期」筛选。
- **多语言 (i18n)**：新增注册、配件、日志筛选等界面的全套翻译。

### 文档

- 新增《设备绑定系统设计文档》`docs/specs/2026-06-27-device-binding-design.md`

### 文件改动概览

| 功能 | 文件 | 改动 | 说明 |
|---|---|---|---|
| 📄 设备绑定设计文档 | `docs/specs/2026-06-27-device-binding-design.md` | 新增 +897 | 设备绑定系统设计方案 |
| 🌐 多语言 i18n | `public/i18n.js` | +456 | 注册、配件、日志筛选等翻译 |
| 🖥️ 前端主逻辑 | `public/app.js` | +423 | 注册 UI、设备管理、日志筛选 |
| 🗄️ 数据库 | `database.js` | 新增 +42 | 设备配件字段 + 蜂鸣器状态 |
| 🔐 鉴权 | `auth.js` | 新增 +39 | 公开注册接口 |
| 🚦 IP 限流 | `rateLimit.js` | +32 | 限流中间件 |
| 📱 设备管理 | `devices.js` | 71 处改动 | 删除、型号选择、配件、级联删除 |
| 📃 页面结构 | `public/index.html` | +14 | 注册/筛选相关元素 |
| 🎨 样式 | `public/style.css` | 28 处改动 | UI 调整 |

---

## 提交清单（由旧到新）

| 提交 | 主题 |
|---|---|
| `c386d86` | feat: add device accessories and buzzer status fields to DB schema |
| `0cd9672` | feat: add IP rate limiting middleware |
| `c3e051c` | feat: add public registration endpoint with IP rate limiting |
| `4800125` | feat: cascade delete device with all related records |
| `34e7a78` | feat: add public user registration UI with captcha |
| `d2f4d85` | feat: device management with delete, model selection, and accessories |
| `2c88265` | feat: add user and date filters to operation logs page |
| `1ed41e9` | feat: add i18n translations for registration, accessories, and log filters |
| `09b5e38` | docs: add device binding system design spec |
