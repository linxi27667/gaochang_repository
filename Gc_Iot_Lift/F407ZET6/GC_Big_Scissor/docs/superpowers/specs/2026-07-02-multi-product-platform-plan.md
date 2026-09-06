# 高昌机电 多型号举升机物联网平台 完整方案

> 文档版本:V2.0  日期:2026-07-02
> 角色:产品设计 + 系统架构
> 适用项目:[GC-LeadScrewLift-IOT](../..)(固件) + [Gaochang_Iot_Web](../../../Gaochang_Iot_Web)(平台)
> 参考文档:[2026-07-01-device-binding-implementation.md](./2026-07-01-device-binding-implementation.md)

---

## 目录

- [一、产品全景与目标](#一产品全景与目标)
- [二、产品型号矩阵](#二产品型号矩阵)
- [三、硬件引脚分配与接线](#三硬件引脚分配与接线)
- [四、控制逻辑详细说明](#四控制逻辑详细说明)
- [五、设备身份与绑定体系](#五设备身份与绑定体系)
- [六、物联网平台功能完整清单](#六物联网平台功能完整清单)
- [七、操作日志与计数体系](#七操作日志与计数体系)
- [八、W25Q Flash 存储规划](#八w25q-flash-存储规划)
- [九、固件架构设计](#九固件架构设计)
- [十、Web 端功能设计](#十web-端功能设计)
- [十一、数据流与时序图](#十一数据流与时序图)
- [十二、实施计划](#十二实施计划)

---

## 一、产品全景与目标

### 1.1 产品愿景

让高昌机电的举升机设备从"出厂即孤岛"升级为"全生命周期可追溯的智能设备":
- **客户**:扫码绑定设备,手机查看设备状态、操作记录、维保提醒
- **工厂**:统一固件烧录,通过配置切换 4 种产品型号,无需为每个订单重编固件
- **售后**:远程定位故障,远程清除报警,远程下发配置,减少现场出差
- **管理**:统计设备运行数据,分析使用习惯,优化产品设计

### 1.2 核心设计原则

| 原则 | 说明 |
|---|---|
| **单一固件** | 所有 4 种型号烧同一份固件,通过 `product_type` 配置切换 |
| **UID 即身份** | 用 STM32F407 96-bit UID 作为设备唯一身份,贯穿出厂→绑定→运行→售后 |
| **配置驱动** | 模块使能用位图开关,Web 端远程开关,不用 `#if` 重编固件 |
| **零废弃升级** | 短期方案(15 台)的所有资产在量产方案中全部复用 |
| **操作可追溯** | 工人所有操作(上升/下降/锁定/急停/补油/解锁)全记录到平台 |
| **安全优先** | 急停 > 光电 > 上限位 > 按键,故障状态必须平台远程解除 |

### 1.3 平台角色定义

| 角色 | 权限 | 典型用户 |
|---|---|---|
| **superadmin** | 全部权限,含切换 product_type、管理所有用户和设备 | 高昌机电管理员 |
| **admin** | 管理设备、查看所有设备数据、远程命令、调时序参数 | 高昌售后工程师 |
| **user(客户)** | 只看自己绑定的设备、远程清除报警、查看操作日志 | 终端客户车间主管 |
| **operator(工人)** | 仅本地物理操作,无 Web 权限(操作通过设备指纹关联) | 客户车间工人 |

---

## 二、产品型号矩阵

### 2.1 型号定义

| 内部代号 | 产品名称 | SN 前缀 | 特征 |
|---|---|---|---|
| `double_post` | 两柱举升机 | `DP-` | 3 输出,电磁铁,无光电 |
| `small_scissor` | 小剪举升机 | `SS-` | 3 输出,气阀,有光电 |
| `thin_scissor` | 超薄小剪举升机 | `TS-` | 3 输出,气阀+下限位 |
| `large_scissor` | 大剪举升机 | `LS-` | 6 输出,主子机切换 |

### 2.2 大剪主子机说明

**硬件**:一块 PCB,通过旋转开关切换当前控制对象(主机阀组 / 子机阀组)。
**软件**:单一固件,单 SN/UID,一个 telemetry JSON 同时含主子机状态。运行时读取旋转开关决定当前操作作用在哪套阀组上。电机(PF8)和下降阀(PF9)主子机共用。

---

## 三、硬件引脚分配与接线

### 3.1 输出引脚(全型号共用)

| 引脚 | 用途 | 说明 |
|---|---|---|
| **PF8** | 电机继电器 | 所有型号都用,主子机共用 |
| **PF9** | 下降执行继电器 | 两柱接电磁铁,其余接下降阀 |
| **PD8** | 通用输出 1 | 两柱=下降阀,小剪/超薄=气阀,大剪=主机气阀 |
| **PD9** | 通用输出 2 | 大剪=主机工作阀;其他型号备用 |
| **PD10** | 通用输出 3 | 大剪=子机气阀;其他型号备用 |
| **PD11** | 通用输出 4 | 大剪=子机工作阀;其他型号备用 |

**电气规则**:
- 继电器输出 24V = 接通
- 输入 0V = 接通(按键、行程开关、光电)
- 急停为常闭型,断开表示触发
- 上限位为常开型,接通表示碰到

### 3.2 输入引脚(全型号共用)

| 引脚 | 信号 | 适用型号 |
|---|---|---|
| PE0 | 上升按钮 | 全部 |
| PE1 | 下降按钮 | 全部 |
| PE2 | 锁定按钮 | 全部 |
| PE3 | 急停 | 全部 |
| PE4 | 上限位 | 全部(大剪为主机上限位) |
| PE5 | 下限位 | 仅超薄小剪 |
| PE6 | 补油按钮 | 小剪/超薄/大剪 |
| PE7 | 光电开关 | 小剪/超薄/大剪 |
| PE8 | 旋转开关(主/子切换) | 仅大剪 |

### 3.3 各型号接线清单

#### 3.3.1 两柱(double_post)

| 信号 | 引脚 | 接线 |
|---|---|---|
| 上升按钮 | PE0 | NO → GND |
| 下降按钮 | PE1 | NO → GND |
| 锁定按钮 | PE2 | NO → GND |
| 急停 | PE3 | COM → GND(NC) |
| 上限位 | PE4 | NO → GND |
| 电机继电器 | PF8 | → 电机接触器 |
| 电磁铁继电器 | PF9 | → 电磁铁线圈 |
| 下降阀 | PD8 | → 24V 电磁阀 |

#### 3.3.2 小剪(small_scissor)

| 信号 | 引脚 | 接线 |
|---|---|---|
| 上升/下降/锁定/急停/上限位 | PE0~PE4 | 同两柱 |
| 补油按钮 | PE6 | NO → GND |
| 光电开关 | PE7 | OUT → GND(遮挡时 0V) |
| 电机继电器 | PF8 | → 电机接触器 |
| 下降阀继电器 | PF9 | → 下降阀 |
| 气阀 | PD8 | → 气阀 24V |

#### 3.3.3 超薄小剪(thin_scissor)

在小剪基础上,PE5 接下限位(行程开关 NO → GND)。其余与小剪完全相同。

#### 3.3.4 大剪(large_scissor)

| 信号 | 引脚 | 接线 |
|---|---|---|
| 上升/下降/锁定/急停 | PE0~PE3 | → GND |
| 主机上限位 | PE4 | → GND |
| 补油/光电 | PE6/PE7 | → GND |
| 旋转开关 | PE8 | → GND(主/子档切换) |
| 电机继电器(主子共用) | PF8 | → 电机接触器 |
| 下降阀继电器(主子共用) | PF9 | → 下降阀 |
| 主机气阀/工作阀 | PD8/PD9 | → 主机阀组 |
| 子机气阀/工作阀 | PD10/PD11 | → 子机阀组 |

> 大剪的子机上限位需硬件通过旋转开关同步切换到 PE4,具体由电气设计实现。

---

## 四、控制逻辑详细说明

### 4.1 公共规则(所有型号)

1. **急停优先级最高**:按下急停立即停止所有输出,任何状态都进入 `STATE_ESTOP`。急停释放后才能继续操作。
2. **光电保护**(小剪/超薄/大剪):光电遮挡时停止所有动作,触发 `EVENT_PHOTO_ALARM` 上报后台,**必须后台远程下发 `clear_alarm` 命令才能继续动作**。
3. **按键互斥**:上升+下降同时按下视为危险输入,空闲时不启动,运动中立即急停。
4. **所有时序用 FreeRTOS tick 计时**,不阻塞主循环。
5. **每次状态变化、按键操作、报警触发都通过 MQTT 实时上报**。

### 4.2 两柱(double_post)控制逻辑

#### 上升流程
1. 按下上升按钮 → 电机接通(PF8=ON),记录操作日志 `op_up_start`
2. 持续运行,直到:
   - 上限位触发 → 电机断开 → 记录 `op_up_stop_limit`
   - 松开按钮 → 电机断开 → 记录 `op_up_stop_release`,统计 `up_count++`
   - 急停 → 电机断开 → 记录 `op_estop`

#### 下降流程
1. 按下下降按钮 → 电机接通(PF8=ON),记录 `op_down_start`
2. **200ms 后** → 电磁铁接通(PF9=ON)
3. **从电磁铁接通起 2000ms 后** → 电机断开(PF8=OFF),下降阀打开(PD8=ON)
4. 持续下降,直到:
   - 松开按钮 → 电磁铁断开,下降阀关闭 → 统计 `down_count++`
   - 上限位触发 → 电机断开,电磁铁立即打开,下降阀保持
   - 锁定按钮接通 → 电机断开,电磁铁立即打开 → 进入 LOCKED
   - 急停 → 全部断开

#### 锁定流程
- 按下锁定按钮 → 下降阀打开(PD8=ON),记录 `op_lock_start`
- 松开 → 下降阀关闭 → 记录 `op_lock_stop`

#### 补油流程
- 两柱无补油功能

### 4.3 小剪(small_scissor)控制逻辑

#### 上升流程
1. 按下上升按钮 → 电机接通(PF8=ON)
2. **不接气阀**
3. 直到上限位/松开/光电/急停

#### 下降流程
1. 按下下降按钮 → 电机接通(PF8=ON)
2. **200ms 后** → 气阀接通(PD8=ON)
3. **从气阀接通起 3000ms 后** → 电机断开(PF8=OFF),下降阀打开(PF9=ON)
4. 持续下降,直到:
   - 松开按钮 → 气阀断开,下降阀关闭
   - 上限位或锁定按钮 → 电机断开,**气阀和下降阀立即打开**(强制快速下降)
   - 光电/急停 → 全部断开

#### 锁定流程
- 按下锁定按钮 → 下降阀打开(PF9=ON)
- 松开 → 下降阀关闭

#### 补油流程
- 同时按下上升+补油 → 电机接通(PF8=ON),气阀打开(PD8=ON)
- 松开任一 → 全部断开

### 4.4 超薄小剪(thin_scissor)控制逻辑

仅在小剪基础上增加**下限位**处理:下降流程中如果**下限位触发**(到达最低位) → 气阀断开(PD8=OFF)。其余逻辑与小剪完全相同。

### 4.5 大剪(large_scissor)控制逻辑

#### 旋转开关切换

每次动作前读取旋转开关:
- **主机档**:按钮作用在主机阀组(PD8 主机气阀 + PD9 主机工作阀)
- **子机档**:按钮作用在子机阀组(PD10 子机气阀 + PD11 子机工作阀)
- 电机(PF8)和下降阀(PF9)主子机共用
- 切换档位时记录 `op_rotary_switch` 操作日志

#### 主机流程(旋转开关=主机档)

**上升**:
1. 按下上升按钮 → 电机接通(PF8),主机工作阀接通(PD9)
2. **200ms 后** → 主机气阀接通(PD8)
3. 直到主机上限位/松开/光电/急停

**下降**:
1. 按下下降按钮 → 电机接通(PF8),主机工作阀接通(PD9)
2. **200ms 后** → 主机气阀接通(PD8)
3. **3000ms 后** → 电机断开,下降阀打开(PF9)
4. 直到松开/主机上限位或锁定/光电/急停

**锁定**:主机工作阀(PD9)+下降阀(PF9)打开

**补油**:电机+主机工作阀+主机气阀打开

#### 子机流程(旋转开关=子机档)

**上升**:
1. 按下上升按钮 → 电机接通(PF8),子机工作阀接通(PD11)
2. **不接气阀**
3. 直到子机上限位/松开/光电/急停

**下降**:
1. 按下下降按钮 → 电机接通(PF8),子机工作阀接通(PD11)
2. **200ms 后** → 子机气阀接通(PD10)
3. **1500ms 后**(子机比主机短) → 电机断开,下降阀打开(PF9)
4. 直到松开/子机上限位或锁定/光电/急停

**锁定**:子机工作阀(PD11)+下降阀(PF9)打开

**补油**:电机+子机工作阀打开

### 4.6 时序参数汇总(Web 端可调)

| 参数 | 两柱 | 小剪 | 超薄 | 大剪主机 | 大剪子机 |
|---|---|---|---|---|---|
| `motor_to_valve_delay_ms` | 200 | 200 | 200 | 200 | 200 |
| `motor_hold_ms` | 2000 | 3000 | 3000 | 3000 | 1500 |

---

## 五、设备身份与绑定体系

### 5.1 设备身份设计

**核心身份**:STM32F407 芯片 96-bit UID(地址 `0x1FFF7A10`),固件运行时读取,**所有板子烧同一份固件**。

**身份派生**:
```
UID (96-bit HEX,如 "4A003258534B501620383552")
 ├─→ MQTT ClientID = dev_<UID>
 ├─→ MQTT Username = <UID>     (长期方案启用)
 ├─→ MQTT Password = <UID>     (长期方案启用)
 ├─→ MQTT Topic    = dev/<UID>/up, dev/<UID>/down
 └─→ SN (人工编的顺序号,与 UID 无算法关系,数据库映射)
```

### 5.2 出厂流程(工厂操作)

#### 5.2.1 录入 device_registry

15 台板子逐台上电,从串口/RTT 日志读到 UID,通过 Web 后台批量录入:

```sql
CREATE TABLE IF NOT EXISTS device_registry (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  serial TEXT UNIQUE NOT NULL,           -- SN,如 DP-001
  uid TEXT UNIQUE NOT NULL,              -- STM32 96-bit UID HEX
  product_type TEXT NOT NULL,            -- double_post/small_scissor/...
  display_name TEXT DEFAULT '',          -- 显示名称
  model TEXT DEFAULT '',                 -- 型号描述
  batch TEXT DEFAULT '',                 -- 批次
  produced_at TEXT DEFAULT '',           -- 出厂日期
  status TEXT DEFAULT 'unbound',         -- unbound / bound
  bound_device_id TEXT DEFAULT '',
  created_at TEXT NOT NULL DEFAULT ''
);
```

#### 5.2.2 贴标

每台设备外壳贴一张二维码标签:

```
┌─────────────────────────────┐
│  高昌机电 举升机              │
│  型号:两柱举升机              │
│  SN:DP-001                  │
│                             │
│  扫码绑定设备 ↓               │
│  ▓▓▓▓▓▓▓▓▓▓▓                │
│  ▓▓▓▓▓▓▓▓▓                  │
│  ▓▓▓▓▓▓▓▓▓▓▓▓▓▓             │
│                             │
│  https://你的域名/bind?sn=DP-001
└─────────────────────────────┘
```

#### 5.2.3 Web 后台注册页面

管理后台 [public/admin.html](../../../Gaochang_Iot_Web/public/admin.html) 提供批量注册:
- 单台录入:填 SN、UID(可扫码枪输入)、产品型号
- 批量导入:CSV 文件,模板:

```csv
sn,uid,product_type,display_name,batch
DP-001,4A003258534B501620383552,double_post,1号两柱,BATCH-001
SS-001,5B003258534B501620383553,small_scissor,1号小剪,BATCH-001
LS-001,6C003258534B501620383554,large_scissor,1号大剪,BATCH-001
```

### 5.3 客户绑定流程(三种方式)

#### 方式 1:扫二维码绑定(推荐)

```
客户收到设备 → 撕标签扫二维码 → 微信打开 /bind?sn=DP-001
→ 登录/注册账号 → 点绑定 → 完成
```

[public/bind.html](../../../Gaochang_Iot_Web/public/bind.html) 移动端友好页面:
- 自动从 URL 提取 SN
- 显示设备型号和预览信息
- 引导登录后绑定

#### 方式 2:手动输入 UID 绑定

适用于二维码标签损坏或扫不出的情况:

1. 客户在设备机身或说明书找到 UID(出厂时同时贴 UID 标签)
2. 登录 Web 平台 → "我的设备" → "添加设备"
3. 选择"手动输入 UID",输入 24 位 HEX UID
4. 系统查 device_registry 匹配,匹配成功则绑定

[public/bind.html](../../../Gaochang_Iot_Web/public/bind.html) 增加"手动输入"Tab:
```html
<div class="tab">扫码绑定</div>
<div class="tab">手动输入 UID</div>
<input type="text" placeholder="24位设备 UID" maxlength="24">
<button>查询并绑定</button>
```

#### 方式 3:管理员代绑定

售后工程师通过 admin 后台直接为客户绑定:
- 设备列表 → 选设备 → "分配给客户" → 输入客户账号

### 5.4 绑定 API

```javascript
// POST /api/devices/bind
// Body: { serial?: "DP-001", uid?: "4A0032..." }
// 三种绑定入口最终都调这一个 API,serial 和 uid 二选一
router.post('/api/devices/bind', authMiddleware, async (req, res) => {
    const { serial, uid } = req.body;
    const userId = req.user.id;

    // 1. 查 registry(serial 或 uid 任一匹配)
    const registry = serial
        ? db.prepare('SELECT * FROM device_registry WHERE serial = ?').get(serial)
        : db.prepare('SELECT * FROM device_registry WHERE uid = ?').get(uid);

    if (!registry) return res.status(404).json({message: '设备不存在,请检查 SN 或 UID'});

    // 2. 检查是否已被他人绑定
    const device = db.prepare('SELECT * FROM devices WHERE uid = ?').get(registry.uid);
    if (device?.owner_id && device.owner_id !== userId)
        return res.status(409).json({message: '该设备已被其他用户绑定'});
    if (device?.owner_id === userId)
        return res.status(409).json({message: '该设备已绑定到您的账号'});

    // 3. 绑定
    db.prepare(`UPDATE devices SET owner_id=?, bind_status='bound', bound_at=?
                WHERE uid=?`).run(userId, nowISO(), registry.uid);
    db.prepare(`UPDATE device_registry SET status='bound' WHERE id=?`).run(registry.id);

    // 4. 记录绑定日志
    db.prepare(`INSERT INTO binding_logs (uid, serial, user_id, action, ip, created_at)
                VALUES (?, ?, ?, 'bind', ?, ?)`)
      .run(registry.uid, registry.serial, userId, req.ip, nowISO());

    res.json({message: '绑定成功', device_id: registry.serial});
});
```

### 5.5 解绑与设备转移

- **客户自助解绑**:Web 端"我的设备"→ 解绑(只能解绑自己的)
- **管理员强制解绑**:售后场景,admin 后台强制解绑(记录操作日志)
- **设备转移**:管理员从客户 A 解绑后,客户 B 重新绑定

所有绑定/解绑操作都写入 `binding_logs` 表,含操作人、IP、时间。

### 5.6 安全策略

| 威胁 | 短期(15台) | 长期(量产) |
|---|---|---|
| 未知设备连 broker | ❌ 不防(EMQX 暂不校验) | ✅ EMQX HTTP 认证 |
| 设备 A 订阅设备 B 命令 | ✅ 固件订阅 `dev/<自己UID>/down` | ✅ + EMQX ACL |
| 伪造 UID 发数据 | ❌ 接受风险 | ✅ EMQX 认证+ACL |
| 客户 A 看客户 B 设备 | ✅ 服务端 owner_id 过滤 | ✅ |
| 未绑定设备数据泄露 | ✅ 未绑定不推送 | ✅ |

---

## 六、物联网平台功能完整清单

### 6.1 实时监控功能

| 功能 | 说明 | 显示位置 |
|---|---|---|
| **设备在线状态** | 心跳 30s 一次,60s 无心跳判离线 | 设备列表/详情 |
| **当前运行状态** | idle/rising/dropping/locked/estop/photo_alarm | 详情页大图标 |
| **实时 IO 状态** | 所有输入输出引脚的实时电平 | 详情页 IO 卡片 |
| **大剪旋转档位** | 主机档/子机档实时显示 | 详情页(仅大剪) |
| **WebSocket 推送** | 状态变化时主动推送,无需轮询 | 全局 |

### 6.2 运行统计与计数

| 统计项 | 说明 | 存储位置 |
|---|---|---|
| **上升次数** `up_count` | 每次完成一次完整上升+停止循环 +1 | W25Q + 数据库 |
| **下降次数** `down_count` | 每次完成一次完整下降+停止循环 +1 | W25Q + 数据库 |
| **锁定次数** `lock_count` | 每次按下锁定 +1 | W25Q + 数据库 |
| **补油次数** `refill_count` | 每次按下补油 +1 | W25Q + 数据库 |
| **急停次数** `estop_count` | 每次急停触发 +1 | W25Q + 数据库 |
| **光电报警次数** `photo_alarm_count` | 每次光电遮挡 +1 | W25Q + 数据库 |
| **累计运行时长** `total_run_ms` | 电机接通时长累计 | W25Q + 数据库 |
| **主子机分别计数**(大剪) | `up_count_main` / `up_count_sub` 等 | W25Q + 数据库 |
| **上次运行时间** `last_run_at` | 最后一次操作时间 | 数据库 |
| **历史峰值** | 单次最长运行时间等 | 数据库 |

**计数规则**(重要):
- **上升次数**:按下上升按钮 → 电机启动 → 电机停止(松手/上限位/急停) = 1 次。中途急停也算 1 次。
- **下降次数**:按下下降按钮 → 下降流程启动 → 下降阀关闭 = 1 次。中途急停也算 1 次。
- **锁定次数**:按下锁定按钮 = 1 次,与持续时间无关。
- 计数在 W25Q 中持久化,断电不丢失;同时每次状态变化时上报平台,平台同步更新数据库。

### 6.3 操作日志(工人操作全记录)

**所有工人在设备上的物理操作都记录到平台**,日志结构:

```sql
CREATE TABLE IF NOT EXISTS operation_logs (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  device_uid TEXT NOT NULL,           -- 设备 UID
  device_serial TEXT NOT NULL,        -- 设备 SN
  operator_id TEXT,                   -- 操作者(预留,本地无登录则 null)
  op_type TEXT NOT NULL,              -- 操作类型,见下表
  op_result TEXT,                     -- ok / interrupted / failed
  duration_ms INTEGER,                -- 操作持续时长(可统计的)
  detail TEXT,                        -- JSON 详细信息
  device_state TEXT,                  -- 操作发生时设备状态
  occurred_at TEXT NOT NULL,          -- 发生时间(设备时间)
  received_at TEXT NOT NULL           -- 平台接收时间
);
CREATE INDEX idx_op_logs_uid ON operation_logs(device_uid);
CREATE INDEX idx_op_logs_type ON operation_logs(op_type);
CREATE INDEX idx_op_logs_time ON operation_logs(occurred_at);
```

**操作类型枚举**:

| op_type | 说明 | detail 示例 |
|---|---|---|
| `op_up_start` | 按下上升按钮 | `{"role":"main"}` |
| `op_up_stop_release` | 松开上升按钮停止 | `{"duration_ms":3200}` |
| `op_up_stop_limit` | 上升碰到上限位停止 | `{"duration_ms":4500}` |
| `op_down_start` | 按下下降按钮 | `{"role":"main"}` |
| `op_down_stop_release` | 松开下降按钮停止 | `{"duration_ms":8000}` |
| `op_down_stop_limit` | 下降中上限位异常触发 | `{}` |
| `op_lock_start` | 按下锁定按钮 | `{}` |
| `op_lock_stop` | 松开锁定按钮 | `{"duration_ms":1500}` |
| `op_refill_start` | 按下上升+补油 | `{}` |
| `op_refill_stop` | 松开补油 | `{"duration_ms":5000}` |
| `op_estop` | 急停触发 | `{"from_state":"rising"}` |
| `op_photo_alarm` | 光电遮挡报警 | `{}` |
| `op_rotary_switch` | 大剪旋转开关切换 | `{"from":"main","to":"sub"}` |
| `op_remote_clear_alarm` | 平台远程清除报警 | `{"admin_id":2}` |
| `op_remote_lock` | 平台远程锁定 | `{"admin_id":2}` |
| `op_remote_unlock` | 平台远程解锁 | `{"admin_id":2}` |
| `op_config_change` | 平台下发配置变更 | `{"changes":["motor_hold_ms"]}` |
| `op_product_type_change` | 切换产品型号 | `{"from":"double_post","to":"small_scissor"}` |
| `op_power_on` | 设备上电 | `{"boot_count":123}` |
| `op_power_off` | 设备断电(检测到电压跌落) | `{}` |

**日志上报策略**:
- 关键操作(急停/光电/远程命令/配置变更)立即上报
- 常规操作(上升/下降/锁定/补油)实时上报,断网时缓存到 W25Q,联网后补传
- 操作日志同时写入 W25Q 环形缓冲(最近 256 条)作为离线备份

### 6.4 报警与事件

| 事件类型 | 触发条件 | 处置 |
|---|---|---|
| `EVENT_ESTOP` | 急停按下 | 立即停止+上报,急停释放后自动恢复 |
| `EVENT_PHOTO_ALARM` | 光电遮挡 | 立即停止+上报,**必须远程 clear_alarm** |
| `EVENT_LIMIT_UP_ABNORMAL` | 下降过程中上限位异常触发 | 上报告警,按流程处理 |
| `EVENT_OVERHEAT` | 预留:温度传感器超阈值 | 上报,持续超阈值锁定设备 |
| `EVENT_OFFLINE` | 设备 60s 无心跳 | 平台标记离线,推送给绑定用户 |
| `EVENT_REBOOT` | 设备异常重启 | 上报,记录重启时的状态 |

### 6.5 远程命令

| 命令 | 用途 | 权限 |
|---|---|---|
| `clear_alarm` | 清除光电报警,恢复动作 | admin 以上(可授予 user) |
| `remote_lock` | 远程锁定设备(禁止本地操作) | admin 以上 |
| `remote_unlock` | 远程解锁 | admin 以上 |
| `enter_maintenance` | 进入维护模式(解锁特殊操作) | admin 以上 |
| `exit_maintenance` | 退出维护模式 | admin 以上 |
| `set_config` | 下发配置(时序/模块使能) | admin 以上 |
| `set_product_type` | 切换产品型号 | **仅 superadmin** |
| `query_telemetry` | 立即请求一次 telemetry | admin 以上 |
| `query_logs` | 请求补传离线日志 | admin 以上(自动触发) |

### 6.6 维保管理

| 功能 | 说明 |
|---|---|
| **维保周期配置** | 按运行次数(如每 5000 次)或时长(如每 200 小时)触发 |
| **维保提醒** | 接近阈值时平台推送提醒给绑定用户和 admin |
| **维保记录** | admin 维保后录入记录(时间/内容/更换部件) |
| **维保历史** | 设备详情页可查所有维保记录 |
| **强制锁定** | 超期未维保可配置为自动锁定设备(可选) |

### 6.7 数据分析与报表

| 报表 | 用途 | 时间维度 |
|---|---|---|
| **设备使用频率** | 每台设备每日/周/月操作次数 | 按日/周/月 |
| **型号分布** | 各型号设备数量和使用情况 | 全局 |
| **故障率统计** | 急停/光电报警/上限位异常的发生频率 | 按设备/型号 |
| **客户活跃度** | 客户绑定设备数和操作频率 | 按客户 |
| **运维报表** | 远程命令次数、维保及时率 | 按月 |

### 6.8 各型号 telemetry JSON

#### 两柱(double_post)

```json
{
    "uid": "4A003258534B501620383552",
    "product_type": "double_post",
    "seq": 12345,
    "state": "idle",
    "io_input": {
        "btn_up": false, "btn_down": false, "btn_lock": false,
        "estop": false, "limit_up": false
    },
    "io_output": {
        "motor": false, "solenoid": false, "valve_drop": false
    },
    "stats": {
        "up_count": 1024, "down_count": 980, "lock_count": 50,
        "estop_count": 2, "total_run_ms": 3600000,
        "last_run_at": "2026-07-02T10:30:00Z"
    }
}
```

#### 大剪(large_scissor)

```json
{
    "uid": "...",
    "product_type": "large_scissor",
    "seq": 12345,
    "state": "idle",
    "rotary_switch": "main",
    "io_input": { /* 8 路 */ },
    "io_output": { /* 6 路 */ },
    "stats": {
        "up_count": 1024, "down_count": 980, "lock_count": 50,
        "up_count_main": 600, "up_count_sub": 424,
        "down_count_main": 580, "down_count_sub": 400,
        "estop_count": 2, "photo_alarm_count": 1,
        "total_run_ms": 3600000
    }
}
```

### 6.9 MQTT Topic 设计

```
dev/<UID>/up           # 设备→平台 所有上行(telemetry/event/status/logs/commands_resp)
dev/<UID>/down         # 平台→设备 所有下行(command/config)
```

**上行消息按 `type` 字段区分**:
```json
{"type":"telemetry", ...}      // 遥测
{"type":"event", ...}          // 事件
{"type":"status", ...}         // 在线状态心跳
{"type":"op_log", ...}         // 操作日志
{"type":"cmd_resp", ...}       // 命令响应
{"type":"logs_batch", ...}     // 离线日志批量补传
```

---

## 七、操作日志与计数体系

### 7.1 计数流程(以上升为例)

```
工人按下上升按钮
  ↓
固件检测到 PE0=0V
  ↓
App_LiftCore 触发 on_up_pressed()
  ↓
具体型号 ops 实现:
  1. 电机接通
  2. 状态切换为 RISING
  3. 记录 op_log: {op_type:"op_up_start", occurred_at:now}
  4. 立即通过 MQTT 上报 op_log
  5. 更新 telemetry.state = "rising",立即上报 telemetry
  ↓
电机持续运行...
  ↓
工人松开上升按钮(或上限位触发)
  ↓
  1. 电机断开
  2. 状态切换为 IDLE
  3. up_count++ (写入 W25Q 统计区)
  4. 记录 op_log: {op_type:"op_up_stop_release", duration_ms:3200}
  5. 上报 op_log + telemetry(含新 stats)
```

### 7.2 离线日志补传

断网场景下操作日志处理:

1. **断网检测**:DTU 离线标志位,固件检测到后进入"离线缓存模式"
2. **本地缓存**:操作日志写入 W25Q 环形缓冲(最多 256 条,超出覆盖最旧)
3. **联网恢复**:固件主动上报 `{"type":"status","network":"recovered"}`
4. **平台拉取**:平台收到恢复通知后,下发 `query_logs` 命令
5. **批量补传**:固件把缓存的日志批量打包上报 `{"type":"logs_batch","logs":[...]}`
6. **平台确认**:平台收到后回 `cmd_resp{result:"ok"}`,固件清空已确认的缓存

### 7.3 操作日志查询 API

```
GET /api/devices/:uid/operation_logs?limit=50&offset=0&op_type=op_up_start&start=2026-07-01&end=2026-07-02
→ 返回操作日志列表

GET /api/devices/:uid/operation_logs/stats?period=daily&start=2026-07-01&end=2026-07-31
→ 返回每日操作次数统计(上升/下降/锁定/急停等)
```

### 7.4 日志展示

设备详情页"操作日志"Tab:
- 时间轴视图:按时间倒序展示每条操作,含状态图标
- 筛选:按操作类型、时间范围、操作结果筛选
- 导出:导出 CSV/Excel(管理员权限)
- 统计图表:近 7 天/30 天操作次数柱状图

---

## 八、W25Q Flash 存储规划

### 8.1 新版地址规划

| 地址范围 | 大小 | 用途 |
|---|---|---|
| 0x000000 ~ 0x000FFF | 4KB | 系统配置 SLOT A(product_type/时序/模块使能) |
| 0x001000 ~ 0x001FFF | 4KB | 系统配置 SLOT B(双扇区轮转) |
| 0x002000 ~ 0x002FFF | 4KB | 运行统计 SLOT A(计数/时长) |
| 0x003000 ~ 0x003FFF | 4KB | 运行统计 SLOT B(双扇区轮转) |
| 0x004000 ~ 0x004FFF | 4KB | 操作日志环形缓冲(最近 256 条) |
| 0x005000 ~ 0x005FFF | 4KB | 报警日志环形缓冲(最近 256 条) |
| 0x006000 ~ 0x006FFF | 4KB | 维保日志环形缓冲(最近 64 条) |
| 0x007000 ~ 0x007FFF | 4KB | 设备元数据(出厂信息/绑定状态/上电次数) |
| 0x008000 ~ | 剩余 | 预留(OTA 等) |

### 8.2 系统配置结构体

```c
typedef struct {
    uint16_t header;                       /* 0xA5A5 */
    uint16_t version;
    uint8_t  product_type;                 /* product_type_t */
    uint8_t  reserved_0;
    uint16_t motor_to_valve_delay_ms;      /* 默认 200 */
    uint16_t motor_hold_ms;                /* 主机保持时间 */
    uint16_t sub_motor_hold_ms;            /* 子机(大剪)默认 1500 */
    uint32_t module_enable_mask;           /* 模块使能位图 */
    uint16_t photoelectric_debounce_ms;
    uint16_t estop_debounce_ms;
    uint8_t  reserved[32];
    uint16_t crc16;
} w25q_config_t;

#define MODULE_EN_BUZZER    (1U << 0)
#define MODULE_EN_PRESSURE  (1U << 1)
#define MODULE_EN_RS485     (1U << 2)
#define MODULE_EN_TEMP      (1U << 3)
```

### 8.3 运行统计结构体

```c
typedef struct {
    uint32_t magic;                        /* 0x53544154 = "STAT" */
    uint32_t version;
    uint32_t up_count;
    uint32_t down_count;
    uint32_t lock_count;
    uint32_t refill_count;
    uint32_t estop_count;
    uint32_t photo_alarm_count;
    uint32_t total_run_ms;
    uint32_t up_count_main;                /* 大剪专用 */
    uint32_t up_count_sub;
    uint32_t down_count_main;
    uint32_t down_count_sub;
    uint32_t boot_count;                   /* 上电次数 */
    uint32_t crc;
} w25q_stats_t;
```

### 8.4 操作日志条目结构

```c
typedef struct {
    uint32_t timestamp;                    /* Unix 时间戳(从平台同步) */
    uint8_t  op_type;                      /* op_type 枚举 */
    uint8_t  op_result;                    /* 0=ok 1=interrupted 2=failed */
    uint16_t duration_ms;
    uint8_t  detail[8];                    /* 预留(JSON 简化) */
} w25q_op_log_entry_t;                     /* 16 字节,4KB 可存 256 条 */
```

### 8.5 W25Q 功能清单

| 功能 | 启用 | 说明 |
|---|---|---|
| 系统配置持久化 | ✅ | product_type、时序、模块使能 |
| 运行统计持久化 | ✅ | 计数、时长、断电不丢 |
| 操作日志离线缓存 | ✅ | 断网时缓存,联网补传 |
| 报警日志 | ✅ | 光电/急停/异常记录 |
| 维保日志 | ✅ | 维保记录留痕 |
| 设备元数据 | ✅ | 出厂信息、上电次数 |
| 高度存储 | ❌ | 四新产品无编码器,代码保留不调用 |
| OTA 备份区 | 🔜 | 预留 |

---

## 九、固件架构设计

### 9.1 新增文件清单

| 文件 | 用途 |
|---|---|
| `APP/Inc/app_product.h` | 产品型号枚举、角色枚举 |
| `APP/Inc/app_io_map.h` | IO 抽象层接口 |
| `APP/Src/app_io_map.c` | IO 映射表 + 4 型号配置数据 |
| `APP/Inc/app_lift_core.h` | 控制框架接口(函数指针注册) |
| `APP/Src/app_lift_core.c` | 控制框架实现(公共逻辑+状态分发+日志记录) |
| `APP/Src/app_lift_double_post.c` | 两柱控制逻辑 |
| `APP/Src/app_lift_small_scissor.c` | 小剪控制逻辑 |
| `APP/Src/app_lift_thin_scissor.c` | 超薄小剪控制逻辑 |
| `APP/Src/app_lift_large_scissor.c` | 大剪控制逻辑(旋转开关切换) |
| `APP/Src/app_op_log.c` | 操作日志记录与上报 |

### 9.2 改造现有文件

| 文件 | 改动 |
|---|---|
| `app_w25qxx.h/c` | 新版配置结构体+运行统计结构体+操作日志结构体,移除高度调用 |
| `app_lift_iot.c` | telemetry 按型号填充,新增 op_log 上报、stats 上报、命令处理 |
| `app_tas_dtu.c` | DTU 参数 UID 派生(已在绑定方案中实现) |

### 9.3 产品型号枚举

```c
typedef enum {
    PRODUCT_TYPE_DOUBLE_POST  = 0,
    PRODUCT_TYPE_SMALL_SCISSOR = 1,
    PRODUCT_TYPE_THIN_SCISSOR  = 2,
    PRODUCT_TYPE_LARGE_SCISSOR = 3,
    PRODUCT_TYPE_MAX
} product_type_t;

typedef enum {
    LIFT_ROLE_MAIN = 0,
    LIFT_ROLE_SUB  = 1,
} lift_role_t;
```

### 9.4 IO 抽象层

```c
typedef enum {
    IO_IN_BTN_UP = 0, IO_IN_BTN_DOWN, IO_IN_BTN_LOCK, IO_IN_ESTOP,
    IO_IN_LIMIT_UP, IO_IN_LIMIT_DOWN, IO_IN_BTN_REFILL,
    IO_IN_PHOTOELECTRIC, IO_IN_ROTARY, IO_IN_MAX
} io_in_id_t;

typedef enum {
    IO_OUT_MOTOR = 0, IO_OUT_VALVE_AIR, IO_OUT_VALVE_DROP,
    IO_OUT_SOLENOID, IO_OUT_VALVE_AIR_MAIN, IO_OUT_VALVE_AIR_SUB,
    IO_OUT_VALVE_WORK_MAIN, IO_OUT_VALVE_WORK_SUB, IO_OUT_MAX
} io_out_id_t;

void    App_IO_Map_Init(product_type_t type);
uint8_t App_IO_Read(io_in_id_t id);
void    App_IO_Write(io_out_id_t id, uint8_t value);
```

### 9.5 控制框架

```c
typedef struct {
    void (*init)(void);
    void (*on_up_pressed)(void);
    void (*on_down_pressed)(void);
    void (*on_lock_pressed)(void);
    void (*on_refill_pressed)(void);
    void (*on_estop)(void);
    void (*on_photoelectric_blocked)(void);
    void (*poll)(void);
} lift_ops_t;

void App_LiftCore_Init(void);
void App_LiftCore_Poll(void);
```

### 9.6 操作日志记录接口

```c
typedef enum {
    OP_UP_START = 0, OP_UP_STOP_RELEASE, OP_UP_STOP_LIMIT,
    OP_DOWN_START, OP_DOWN_STOP_RELEASE, OP_DOWN_STOP_LIMIT,
    OP_LOCK_START, OP_LOCK_STOP,
    OP_REFILL_START, OP_REFILL_STOP,
    OP_ESTOP, OP_PHOTO_ALARM,
    OP_ROTARY_SWITCH,
    OP_REMOTE_CLEAR_ALARM, OP_REMOTE_LOCK, OP_REMOTE_UNLOCK,
    OP_CONFIG_CHANGE, OP_PRODUCT_TYPE_CHANGE,
    OP_POWER_ON, OP_POWER_OFF,
} op_type_t;

void App_OpLog_Record(op_type_t type, uint8_t result, uint16_t duration_ms, 
                      const uint8_t *detail, uint8_t detail_len);
void App_OpLog_Flush(void);              /* 主动上报缓存日志 */
void App_OpLog_On_Network_Recovered(void); /* 联网恢复时批量补传 */
```

### 9.7 大剪旋转开关处理

```c
static lift_role_t g_current_role;

static void poll(void) {
    lift_role_t new_role = App_IO_Read(IO_IN_ROTARY) ? LIFT_ROLE_SUB : LIFT_ROLE_MAIN;
    if (new_role != g_current_role) {
        App_OpLog_Record(OP_ROTARY_SWITCH, 0, 0, 
                         (uint8_t[]){g_current_role, new_role}, 2);
        g_current_role = new_role;
    }
    uint16_t motor_hold = (g_current_role == LIFT_ROLE_MAIN)
                          ? g_config.motor_hold_ms
                          : g_config.sub_motor_hold_ms;
    /* 状态机处理 */
}
```

---

## 十、Web 端功能设计

### 10.1 数据库扩展

#### device_registry 加字段

```sql
ALTER TABLE device_registry ADD COLUMN product_type TEXT DEFAULT 'double_post';
ALTER TABLE device_registry ADD COLUMN lift_role TEXT DEFAULT 'main';
```

#### devices 表加字段

```sql
ALTER TABLE devices ADD COLUMN product_type TEXT;
ALTER TABLE devices ADD COLUMN lift_role TEXT;
ALTER TABLE devices ADD COLUMN up_count INTEGER DEFAULT 0;
ALTER TABLE devices ADD COLUMN down_count INTEGER DEFAULT 0;
ALTER TABLE devices ADD COLUMN lock_count INTEGER DEFAULT 0;
ALTER TABLE devices ADD COLUMN estop_count INTEGER DEFAULT 0;
ALTER TABLE devices ADD COLUMN total_run_ms INTEGER DEFAULT 0;
ALTER TABLE devices ADD COLUMN last_run_at TEXT;
```

#### 新增表

```sql
-- 操作日志
CREATE TABLE operation_logs (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  device_uid TEXT NOT NULL,
  device_serial TEXT NOT NULL,
  operator_id TEXT,
  op_type TEXT NOT NULL,
  op_result TEXT,
  duration_ms INTEGER,
  detail TEXT,
  device_state TEXT,
  occurred_at TEXT NOT NULL,
  received_at TEXT NOT NULL
);
CREATE INDEX idx_op_logs_uid ON operation_logs(device_uid);
CREATE INDEX idx_op_logs_type ON operation_logs(op_type);
CREATE INDEX idx_op_logs_time ON operation_logs(occurred_at);

-- 维保记录
CREATE TABLE maintenance_logs (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  device_uid TEXT NOT NULL,
  admin_id INTEGER NOT NULL,
  content TEXT,
  parts_replaced TEXT,
  occurred_at TEXT NOT NULL
);

-- 报警记录
CREATE TABLE alarm_logs (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  device_uid TEXT NOT NULL,
  alarm_type TEXT NOT NULL,
  detail TEXT,
  cleared_by TEXT,
  cleared_at TEXT,
  occurred_at TEXT NOT NULL
);

-- 型号元数据
CREATE TABLE product_configs (
  product_type TEXT PRIMARY KEY,
  display_name TEXT,
  inputs_json TEXT,
  outputs_json TEXT,
  default_motor_hold_ms INTEGER,
  default_motor_to_valve_delay_ms INTEGER
);
```

### 10.2 页面清单

| 页面 | 路径 | 角色 | 说明 |
|---|---|---|---|
| 登录/注册 | `/login` `/register` | 公开 | 客户注册账号 |
| 设备绑定 | `/bind` | user | 扫码/手动输入 UID 绑定 |
| 我的设备 | `/devices` | user | 客户查看自己绑定的设备列表 |
| 设备详情 | `/devices/:uid` | user/admin | 实时状态+IO+操作日志+统计 |
| 管理后台 | `/admin` | admin/superadmin | 设备注册/用户管理/全局看板 |
| 设备注册 | `/admin/devices/register` | admin | 单台/批量录入设备 |
| 用户管理 | `/admin/users` | superadmin | 管理客户和 admin 账号 |
| 全局看板 | `/admin/dashboard` | admin | 在线设备/报警/型号分布 |
| 报表分析 | `/admin/reports` | admin | 使用统计/故障率/客户活跃度 |

### 10.3 设备详情页布局

```
┌─────────────────────────────────────────┐
│  返回   设备 DP-001(两柱举升机)  [在线]  │
├─────────────────────────────────────────┤
│ ┌─状态─┐  ┌─统计─────────────┐         │
│ │ 空闲 │  │ 上升:1024         │         │
│ │  🟢  │  │ 下降:980          │         │
│ └─────┘  │ 锁定:50            │         │
│          │ 急停:2             │         │
│          │ 累计:60 分钟       │         │
│          └───────────────────┘         │
├─────────────────────────────────────────┤
│  [实时 IO]  [操作日志]  [报警]  [维保]  [配置]
├─────────────────────────────────────────┤
│ Tab:操作日志                            │
│  2026-07-02 10:30:15  上升启动          │
│  2026-07-02 10:30:18  上升停止 3.2s     │
│  2026-07-02 10:31:00  下降启动          │
│  2026-07-02 10:31:08  下降停止 8.0s     │
│  2026-07-02 10:32:00  锁定启动          │
│  ...                                    │
│  [筛选:类型 时间] [导出CSV]             │
├─────────────────────────────────────────┤
│  [远程清除报警] [远程锁定] [进入维护]   │
└─────────────────────────────────────────┘
```

### 10.4 关键 API 清单

```
# 设备绑定
POST   /api/devices/bind              # 绑定(serial 或 uid)
POST   /api/devices/unbind            # 解绑
GET    /api/devices                   # 我的设备列表

# 设备详情
GET    /api/devices/:uid              # 设备详情
GET    /api/devices/:uid/stats        # 统计数据
GET    /api/devices/:uid/operation_logs  # 操作日志
GET    /api/devices/:uid/alarms       # 报警记录
GET    /api/devices/:uid/maintenance  # 维保记录

# 远程命令
POST   /api/devices/:uid/command      # 下发命令
POST   /api/devices/:uid/config       # 下发配置
POST   /api/devices/:uid/product_type # 切换型号(仅 superadmin)

# 管理后台
GET    /api/admin/stats               # 全局统计
GET    /api/admin/devices             # 所有设备
POST   /api/admin/devices/register    # 注册设备
POST   /api/admin/devices/import      # 批量导入
GET    /api/admin/reports/usage       # 使用报表
```

### 10.5 WebSocket 推送

```javascript
// 客户端订阅
ws.send(JSON.stringify({type: 'subscribe', device_uid: '4A0032...'}));

// 服务端推送
{type: 'telemetry', device_uid: '...', data: {...}}
{type: 'op_log', device_uid: '...', data: {...}}
{type: 'event', device_uid: '...', data: {...}}
{type: 'status', device_uid: '...', online: true}
```

### 10.6 前端动态渲染

```javascript
const PRODUCT_META = {
    double_post: {
        displayName: '两柱举升机',
        inputs: ['btn_up','btn_down','btn_lock','estop','limit_up'],
        outputs: ['motor','solenoid','valve_drop'],
        hasRefill: false, hasPhotoelectric: false, hasRotary: false
    },
    large_scissor: {
        displayName: '大剪举升机',
        inputs: ['btn_up','btn_down','btn_lock','estop','limit_up',
                 'btn_refill','photoelectric','rotary'],
        outputs: ['motor','valve_air_main','valve_air_sub',
                  'valve_drop','valve_work_main','valve_work_sub'],
        hasRefill: true, hasPhotoelectric: true, hasRotary: true
    },
    // ...
};
```

---

## 十一、数据流与时序图

### 11.1 工人操作完整数据流

```
工人按下上升按钮(PE0=0V)
   │
   ▼
固件 App_LiftCore.on_up_pressed()
   │
   ├─→ 电机接通 PF8=ON
   ├─→ 状态切换 RISING
   ├─→ 写 W25Q 操作日志(离线缓存)
   ├─→ 写 W25Q 统计(如有需要)
   └─→ MQTT 上报
         ├─→ {"type":"op_log","op_type":"op_up_start",...}
         └─→ {"type":"telemetry","state":"rising",...}
               │
               ▼
       Node.js mqtt-bridge 接收
               │
               ├─→ 写 operation_logs 表
               ├─→ 更新 devices 表 last_run_at
               └─→ WebSocket 推送绑定用户
                     │
                     ▼
              客户浏览器实时刷新
              "设备状态:上升中"
```

### 11.2 光电报警完整流程

```
光电遮挡(PE7=0V)
   │
   ▼
固件立即停止所有输出
   │
   ├─→ 状态切换 PHOTO_ALARM
   ├─→ photo_alarm_count++ (W25Q)
   ├─→ MQTT 上报 {"type":"event","event":"photo_alarm"}
   └─→ 等待远程解除(本地无法复位)
         │
         ▼
   Node.js 收到事件
         │
         ├─→ 写 alarm_logs 表
         ├─→ WebSocket 推送绑定用户和所有 admin
         └─→ 设备状态显示"光电报警,需远程解除"
               │
               ▼
         admin/user 在 Web 端点"清除报警"
               │
               ▼
         POST /api/devices/:uid/command {cmd:"clear_alarm"}
               │
               ▼
         Node.js → MQTT 下发 dev/<UID>/down
               │
               ▼
         固件收到命令 → 状态恢复 IDLE → 上报 telemetry
```

---

## 十二、实施计划

### 12.1 工作量估算

| 阶段 | 工作日 | 说明 |
|---|---|---|
| 固件架构重构 | 2 | IO 抽象层 + 控制框架 + 产品枚举 |
| 4 型号控制逻辑 | 3 | 两柱 0.5 + 小剪 0.5 + 超薄 0.5 + 大剪 1.5 |
| W25Q 配置+统计+日志 | 1.5 | 新版结构体 + 操作日志环形缓冲 |
| IoT telemetry+op_log | 2 | 按型号填充 telemetry + 操作日志上报 + 离线补传 |
| 设备绑定流程 | 1 | 扫码绑定 + 手动 UID 绑定 + 后台注册 |
| Web 详情页+操作日志 | 2 | 动态渲染 + 日志查询 + 统计图表 |
| Web 管理后台扩展 | 1.5 | 数据库迁移 + 全局看板 + 配置下发 |
| 报表分析 | 1 | 使用频率/故障率/客户活跃度 |
| 联调测试 | 2 | 4 型号时序 + 主子机切换 + 日志完整性 |
| **合计** | **16 工作日** | 单人 |

### 12.2 实施顺序

1. **第 1-2 天**:固件架构(`app_product.h` / `app_io_map` / `app_lift_core`)
2. **第 3 天**:两柱逻辑(模板)
3. **第 4 天**:小剪+超薄逻辑
4. **第 5-6 天**:大剪逻辑(旋转开关+主子机)
5. **第 7-8 天**:W25Q 配置+统计+操作日志缓冲
6. **第 9-10 天**:IoT telemetry+op_log 上报+离线补传
7. **第 11 天**:设备绑定流程(扫码+手动 UID)
8. **第 12-13 天**:Web 详情页+操作日志+统计图表
9. **第 14 天**:管理后台扩展+全局看板
10. **第 15 天**:报表分析
11. **第 16 天**:全型号联调测试

### 12.3 测试策略

| 测试项 | 方法 | 验收标准 |
|---|---|---|
| IO 映射正确性 | 万用表测每个引脚 | 各型号引脚对应正确 |
| 时序精度 | 示波器抓波形 | 200ms/3000ms/1500ms 误差 < 5% |
| 急停优先级 | 任意状态按急停 | 所有输出立即断开 |
| 光电保护 | 遮挡光电 | 必须远程 clear_alarm 才能恢复 |
| 大剪主子机切换 | 旋转开关切换 | 输出阀组正确切换,日志记录 |
| 操作日志完整性 | 断网操作→联网 | 离线日志全部补传,无丢失 |
| 计数准确性 | 重复 100 次上升 | up_count=100,断电不丢 |
| 设备绑定 | 扫码+手动 UID 两种方式 | 均能正确绑定 |
| 配置下发 | Web 改时序参数 | 设备重启后生效 |
| 型号切换 | superadmin 改 product_type | 设备重新初始化,Web 显示更新 |
| 断电恢复 | 配置和统计断电 | 全部持久化,不丢失 |
| WebSocket 推送 | 实时操作设备 | 浏览器 1s 内看到状态变化 |

---

## 附录 A:关键决策记录

| 决策项 | 决定 | 原因 |
|---|---|---|
| 单固件 vs 多固件 | 单固件 | 延续 UID 绑定方案,工厂无需挑固件 |
| `#if` 器件裁剪 | 不用 | 配置驱动 + 运行时禁用 |
| 大剪主子机 | 一块板子+旋转开关 | 硬件已定,主子机本质是阀组切换 |
| 大剪 IoT 管理 | 单 SN 单 UID | 主子机是一台机器 |
| product_type 修改权限 | 仅 superadmin | 防止误改导致时序错乱 |
| 上限位语义 | 正常行程开关 | 碰到触发停止 |
| W25Q 高度存储 | 保留代码不调用 | 四新产品无编码器,保留未来扩展 |
| PF8/PF9 用途 | PF8=电机,PF9=下降执行 | 全型号统一 |
| 操作日志存储 | W25Q 缓存 + 平台持久化 | 断网不丢日志 |
| 绑定方式 | 扫码 + 手动 UID + 管理员代绑 | 兼容二维码损坏场景 |
| 计数规则 | 完整循环 +1,中途急停也算 | 真实反映使用频率 |

## 附录 B:文件路径速查

### 固件
- 工程根:`e:\MCU\gaochang\code\GC-LeadScrewLift-IOT\`
- 现有 W25Q:`APP/Inc/app_w25qxx.h` + `APP/Src/app_w25qxx.c`
- 现有 IoT:`APP/Src/app_lift_iot.c`
- 新增模块:`APP/Inc/app_product.h`、`APP/Src/app_lift_*.c`、`APP/Src/app_op_log.c`

### Web 端
- 工程根:`e:\MCU\gaochang\code\Gaochang_Iot_Web\`
- 数据库:`src/database.js`
- MQTT 桥接:`src/mqtt-bridge.js`
- 路由:`src/routes/`(auth/binding/admin/devices/logs/maintenance)
- 前端:`public/`(admin.html/admin.js/bind.html/bind.js/index.html)

### 文档
- 本方案:`docs/superpowers/specs/2026-07-02-multi-product-platform-plan.md`
- 绑定方案:`docs/superpowers/specs/2026-07-01-device-binding-implementation.md`
