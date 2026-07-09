# TAS-LTE-892D_s4 4G DTU 完整使用教程

> 适用项目：高昌物联网举升机 F407 网关
> 模块：杭州塔石 TAS-LTE-892D_s4 系列 4G DTU
> 依据资料：`TAS-LTE-892D_s4_使用说明书_V1.0.5.pdf`、`TAS-LTE-892D_s4 产品规格书 V1.0.1.pdf`
> 文档目标：完成硬件接线、串口调试、AT 配置、MQTT 接入、STM32 双向通信和现场排障。

---

## 一、模块定位

TAS-LTE-892D_s4 是工业级 4G DTU，核心作用是把串口数据和网络数据做双向透明传输。对 STM32 来说，它不是裸 4G 模组，而是一个已经封装好 TCP/UDP/MQTT/HTTP/WebSocket 协议栈的串口网关。

在本项目里推荐使用方式：

```text
STM32F407 USART3 <-> TAS-LTE-892D_s4 <-> 4G 网络 <-> MQTT Broker <-> Web 监控平台
```

STM32 只需要：

- 通过 AT 指令把 DTU 配成 MQTT 透传模式；
- 在 MQTT 连接成功后，向串口发送 JSON，DTU 自动发布到 MQTT 主题；
- DTU 收到 MQTT 订阅主题的数据后，会把 payload 原样从串口吐给 STM32；
- STM32 解析 JSON 命令并回传状态。

---

## 二、硬件与规格速查

### 2.1 基本规格

| 项目 | 参数 |
|---|---|
| 产品型号 | TAS-LTE-892D_s4 导轨式 4G DTU |
| 主芯片 | EC716S |
| 网络制式 | LTE Cat 1，全网通 |
| 网络协议 | TCP、UDP、DNS、MQTT、HTTP、WebSocket、NTP |
| MQTT 版本 | MQTT 3.1.1 |
| 网络连接数 | 2 路独立 SOCKET 通道 |
| 工作电压 | DC 9-36V |
| 工作温度 | -30 到 70 摄氏度 |
| 存储环境 | -40 到 85 摄氏度，5-95% RH，无凝露 |
| 安装方式 | 35mm 导轨 / 壁挂垫片 |
| SIM | 主卡 NANO-SIM 卡托，副卡贴片 eSIM |
| USB | Micro USB，仅用于内部升级 |

### 2.2 串口规格

| 项目 | 参数 |
|---|---|
| 默认串口 | 9600, 8, N, 1 |
| 支持波特率 | 1200、2400、4800、9600、14400、19200、38400、57600、115200 |
| 数据位 | 7 / 8 |
| 校验位 | None / Odd / Even |
| 停止位 | 1 / 2 |
| 流控 | 默认无流控 |

### 2.3 子型号接口差异

| 子型号 | 串口接口 |
|---|---|
| TAS-LTE-892D_s4 | RS485 + RS232 |
| TAS-LTE-892D1_s4 | RS485 |
| TAS-LTE-892D2_s4 | RS232 |
| TAS-LTE-892D3_s4 | TTL |
| TAS-LTE-892D4_s4 | RS485 + TTL |
| TAS-LTE-892D5_s4 | RS485 x 2 |

> [!WARNING]
> F407 的 UART 是 TTL 电平，不能直接接 RS232 的正负电平口，也不能直接接 RS485 A/B 差分口。若使用 TAS-LTE-892D_s4 的 RS232 口，需要 MAX3232 等电平转换；若使用 RS485 口，需要 RS485 收发器。只有 TTL 子型号才能直接和 MCU TTL 串口连接。

### 2.4 指示灯

| 指示灯 | 状态 | 含义 |
|---|---|---|
| POWER | 亮 | 已上电 |
| POWER | 灭 | 未上电 |
| WORK | 1 秒亮灭一次 | 正常运行 |
| WORK | 常亮或常灭 | 工作异常 |
| NET | 亮 | 已挂载蜂窝网络 |
| NET | 灭 | 未挂载蜂窝网络 |
| LINK | 亮 | 已连接服务器 |
| LINK | 灭 | 未连接服务器 |

---

## 三、工作模式

`AT+DTUMODE=A,B` 用于设置每个 SOCKET 通道的工作模式。

| A | 工作模式 |
|---|---|
| 0 | 关闭该通道 |
| 1 | TCP/UDP 透传 |
| 2 | MQTT 透传 |
| 3 | 塔石 DTU 云 |
| 4 | 塔石 IoT 云 |
| 5 | HTTP 透传 |
| 6 | 阿里云物联网直连 |
| 7 | OneNetStudio 直连 |
| 8 | WebSocket 直连 |

本项目使用：

```text
AT+DTUMODE=2,1
```

含义：把第 1 路 SOCKET 配成 MQTT 透传。

---

## 四、AT 指令模式

### 4.1 命令结尾

除 `+++` 外，AT 指令都应以 `\r\n` 结尾，也就是十六进制 `0D 0A`。

```text
AT\r\n
AT+CSQ\r\n
AT+DTUMODE=2,1\r\n
```

### 4.2 进入命令模式

透传模式下发送：

```text
+++
```

实际发送内容是 3 个加号：

```text
+++? 错误
+++ 错误
++  正确
```

模块回复：

```text
OK
```

注意事项：

- `+++` 必须严格是 3 个字符；
- 发送前后不要夹杂其他业务数据；
- 进入命令模式后默认约 2 分钟自动返回透传，可用 `AT+AUTOATO` 修改；
- 两个 UART 口设备的命令模式状态相互独立。

### 4.3 退出命令模式

```text
ATO
```

回复：

```text
OK
```

> [!WARNING]
> 旧资料里可能写成 `AT+ENTM`，完整手册中 TAS-LTE-892D_s4 退出命令模式使用 `ATO`。

### 4.4 保存配置

```text
AT&W
```

回复：

```text
OK
```

配置后不执行 `AT&W`，重启后参数会丢失。

### 4.5 重启模块

```text
AT+CFUN=1,1
```

回复：

```text
OK
```

> [!WARNING]
> 旧资料里的 `AT+Z`、`AT+S` 不应作为本模块的主流程指令。当前完整手册对应保存为 `AT&W`，重启为 `AT+CFUN=1,1`。

---

## 五、MQTT 透传原理

TAS-LTE-892D_s4 在 MQTT 模式下：

- 最多 5 个订阅主题；
- 最多 2 个发布主题；
- 支持 QoS 0/1/2；
- 支持遗嘱；
- 支持 clean session；
- 支持 retained 发布；
- 支持 MQTT keepalive；
- 支持备用服务器；
- 支持屏蔽非订阅主题。

### 5.1 上行

STM32 发送到 DTU 串口的数据会被 DTU 发布到配置好的 MQTT 发布主题。

```text
STM32 串口 JSON -> DTU -> MQTT publish -> Web 后端订阅
```

### 5.2 下行

Web 后端向命令主题发布 JSON，DTU 收到后把 payload 原样从串口输出。

```text
Web publish command -> DTU MQTT subscribe -> DTU 串口输出 JSON -> STM32 解析命令
```

### 5.3 非订阅主题处理

模块默认可能会把非订阅主题的数据也解析并从串口输出。现场建议开启屏蔽：

```text
AT+BLOCKINFO=1,0
```

含义：通道 1 屏蔽非订阅主题，通道 2 不屏蔽。

---

## 六、本项目 MQTT 主题规划

推荐统一主题前缀：

```text
gaochang/lift
```

推荐网关 ID：

```text
f407zet6
```

推荐设备 ID：

```text
gaochang_lift_f407zet6
```

### 6.1 主题

| 方向 | 主题 | 说明 |
|---|---|---|
| 设备上报 | `gaochang/lift/f407zet6/telemetry` | 周期遥测 |
| 设备上报 | `gaochang/lift/f407zet6/status` | 事件、命令回执、启动状态 |
| 平台下发 | `gaochang/lift/f407zet6/command` | Web 到 STM32 的控制命令 |

### 6.2 遥测 payload

```json
{
  "type": "telemetry",
  "device": "gaochang_lift_f407zet6",
  "name": "Gaochang-Lift-F407-01",
  "model": "GC-F407-2POST",
  "group": "training-area-1",
  "seq": 1,
  "tick": 123456,
  "state": "normal",
  "locked": 0,
  "maintenance_due": 0,
  "admin_mode": 0,
  "runtime": {
    "total_ms": 0,
    "current_ms": 0,
    "run_count": 0,
    "avg_ms": 0
  },
  "direction": "stop",
  "height": {
    "left_mm": 0,
    "right_mm": 0,
    "diff_mm": 0,
    "left_pulse": 0,
    "right_pulse": 0
  },
  "safety": {
    "alarm": "none",
    "alarm_code": 0,
    "upper": 0,
    "lower": 0,
    "stall": 0,
    "collision_up": 0,
    "collision_down": 0
  },
  "dtu": {
    "state": "transparent",
    "csq": 20
  }
}
```

### 6.3 命令 payload

```json
{
  "type": "command",
  "device": "gaochang_lift_f407zet6",
  "cmd": "get_status",
  "command": "get_status",
  "msg_id": "cmd_001",
  "account": "admin"
}
```

常用命令：

| cmd | 说明 |
|---|---|
| `ping` | 设备回复 `pong` |
| `get_status` / `report_now` | 立即上报状态 |
| `lock` | 锁机 |
| `unlock` | 解锁 |
| `admin_enter` | 进入管理模式，需要 `password` |
| `admin_exit` | 退出管理模式 |
| `fault_clear` | 清除故障，需管理模式 |
| `admin_jog` | 管理点动，需 `column`、`direction`、`duration_ms` |
| `maintenance_done` | 保养完成 |
| `reboot_dtu` | 重启 DTU |

---

## 七、MQTT 配置指令

### 7.1 设置 MQTT 服务器

```text
AT+IPPORT="服务器地址",端口,通道
```

示例：

```text
AT+IPPORT="192.168.1.10",1883,1
```

参数：

| 参数 | 说明 |
|---|---|
| 服务器地址 | 域名或 IP，最大 128 字符 |
| 端口 | 1-65535 |
| 通道 | 1 或 2 |

### 7.2 设置 ClientID

```text
AT+CLIENTID="ClientID",通道
```

示例：

```text
AT+CLIENTID="gaochang_lift_f407zet6_dtu",1
```

建议：MQTT ClientID 只作为 Broker 会话 ID，不要直接复用设备 ID。当前项目设备 ID 是 `gaochang_lift_f407zet6`，DTU ClientID 使用 `gaochang_lift_f407zet6_dtu`，避免与测试工具、Web 桥接或其他设备会话冲突。

### 7.3 设置用户名和密码

```text
AT+USERPWD="用户名","密码",通道
```

无认证 Broker：

```text
AT+USERPWD="","",1
```

有认证 Broker：

```text
AT+USERPWD="mqtt_user","mqtt_password",1
```

### 7.4 设置订阅主题

```text
AT+MQTTSUB=使能,"主题",QoS,主题号,通道
```

示例：

```text
AT+MQTTSUB=1,"gaochang/lift/#",0,1,1
```

参数：

| 参数 | 范围 | 说明 |
|---|---|---|
| 使能 | 0/1 | 1 开启该订阅 |
| 主题 | 最大 256 字符 | MQTT topic |
| QoS | 0/1/2 | 消息质量 |
| 主题号 | 1-5 | 最多 5 个订阅主题 |
| 通道 | 1/2 | SOCKET 通道 |

### 7.5 设置发布主题

```text
AT+MQTTPUB=使能,"主题",QoS,retain,主题号,通道
```

单发布主题示例：

```text
AT+MQTTPUB=1,"gaochang/lift/f407zet6/telemetry",0,0,1,1
```

参数：

| 参数 | 范围 | 说明 |
|---|---|---|
| 使能 | 0/1 | 1 开启该发布主题 |
| 主题 | 最大 256 字符 | MQTT topic |
| QoS | 0/1/2 | 消息质量 |
| retain | 0/1 | 是否保留消息 |
| 主题号 | 1-2 | 最多 2 个发布主题 |
| 通道 | 1/2 | SOCKET 通道 |

### 7.6 双发布主题与区分字符串

若同时启用 telemetry 和 status 两个发布主题，必须决定串口数据如何分流。

不配置区分字符串时，串口数据会同时发布到两个主题。若 Web 后端按主题区分业务，会造成重复或误判。

推荐方案：

```text
AT+MQTTPUB=1,"gaochang/lift/f407zet6/telemetry",0,0,1,1
AT+MQTTPUB=1,"gaochang/lift/f407zet6/status",0,0,2,1
AT+MQTTPUBID=1,1,1,"TEL:"
AT+MQTTPUBID=1,2,1,"STA:"
```

上报时：

```text
TEL:{"type":"telemetry",...}
STA:{"type":"status",...}
```

模块会根据前缀选择发布主题，并去掉区分字符串后再发布 payload。

如果固件不想加 `TEL:` / `STA:` 前缀，就只启用一个发布主题，把状态事件也作为 JSON 的 `type` 字段区分。

### 7.7 设置 MQTT 协议心跳

```text
AT+MQTTKEEP=120,1
```

参数：

| 参数 | 说明 |
|---|---|
| 120 | MQTT keepalive 秒数，范围 60-65535 |
| 1 | 通道 1 |

建议 60-120 秒。

### 7.8 Clean Session

```text
AT+CLEANSESSION=1,1
```

建议本项目启用 clean session，避免离线期间堆积旧命令。

### 7.9 遗嘱

```text
AT+WILL="gaochang/lift/f407zet6/status","{\"type\":\"status\",\"event\":\"offline\"}",0,0,1
```

参数：

| 参数 | 说明 |
|---|---|
| 遗嘱主题 | 最大 256 字符 |
| 遗嘱数据 | 最大 256 字符 |
| QoS | 0-2 |
| retain | 0/1 |
| 通道 | 1/2 |

如果遗嘱主题或遗嘱数据为空，遗嘱不会生效。

### 7.10 状态主动上报

```text
AT+AUTOSTATUS=1,1
```

含义：

- 第 1 个参数为 1：主动上报网络连接状态变化和重启原因；
- 第 2 个参数为 1：开机输出 AT Ready。

MQTT 连接状态上报示例：

```text
+STATUS: 1, MQTT CONNECTED
+STATUS: 1, MQTT CLOSED
+STATUS: 1, MQTT SUB LOST
```

STM32 应等待 `MQTT CONNECTED` 后再认为透传链路可用。

---

## 八、本项目推荐完整配置流程

### 8.1 本项目串口基准

本项目现场 DTU 固定使用 `9600,8,N,1`。不要在固件里自动切换 `115200`，也不要写自动波特率探测流程；如果 AT 完全无回应，优先检查 9600 串口方向、TX/RX 交叉、供电、DMA 接收和 `+++` 进入命令模式时序。

```text
+++
AT
AT+UARTCFG?
```

只有确认硬件和固件都要迁移时，才允许修改 `AT+UARTCFG` 并同步修改 CubeMX/USART3 配置；当前举升机项目不做该迁移。

### 8.2 单发布主题配置

这是最稳妥方案。DTU 只发布到 telemetry，Web 后端通过 JSON `type` 区分遥测与状态。

```text
+++
AT
AT+DTUMODE=2,1
AT+IPPORT="192.168.1.10",1883,1
AT+CLIENTID="gaochang_lift_f407zet6_dtu",1
AT+USERPWD="","",1
AT+MQTTSUB=1,"gaochang/lift/#",0,1,1
AT+MQTTPUB=1,"gaochang/lift/f407zet6/telemetry",0,0,1,1
AT+MQTTPUB=0,"",0,0,2,1
AT+MQTTKEEP=120,1
AT+CLEANSESSION=1,1
AT+BLOCKINFO=0,0
AT+AUTOSTATUS=1,1
AT+DTUPACKET=0,1024
AT+RELINKTIME=30
AT+DSCTIME=300
AT&W
AT+CFUN=1,1
```

### 8.3 双发布主题配置

如果必须让 status 独立到 `.../status`，固件发送时要加分流前缀。

```text
+++
AT
AT+DTUMODE=2,1
AT+IPPORT="192.168.1.10",1883,1
AT+CLIENTID="gaochang_lift_f407zet6_dtu",1
AT+USERPWD="","",1
AT+MQTTSUB=1,"gaochang/lift/#",0,1,1
AT+MQTTPUB=1,"gaochang/lift/f407zet6/telemetry",0,0,1,1
AT+MQTTPUB=1,"gaochang/lift/f407zet6/status",0,0,2,1
AT+MQTTPUBID=1,1,1,"TEL:"
AT+MQTTPUBID=1,2,1,"STA:"
AT+MQTTKEEP=120,1
AT+CLEANSESSION=1,1
AT+BLOCKINFO=0,0
AT+AUTOSTATUS=1,1
AT+DTUPACKET=0,1024
AT+RELINKTIME=30
AT+DSCTIME=300
AT&W
AT+CFUN=1,1
```

固件上报：

```text
TEL:{"type":"telemetry","device":"gaochang_lift_f407zet6",...}
STA:{"type":"status","device":"gaochang_lift_f407zet6","event":"boot",...}
```

### 8.4 COM52 实测下行订阅坑

2026-06-05 使用 COM52、`9600,8,N,1` 对当前 TAS-LTE-892D_s4 模块实测：

- 串口上行 JSON 可以稳定发布到 `gaochang/lift/f407zet6/telemetry`；
- `AT+MQTTSUB=1,"gaochang/lift/f407zet6/command",0,1,1` 查询显示配置成功，但 MQTT 下发到该 exact topic 时串口不输出；
- `gaochang/lift/f407zet6/#`、`gaochang/lift/f407zet6/command/#`、`gaochang/cmd/#` 也未稳定输出；
- `AT+MQTTSUB=1,"gaochang/lift/#",0,1,1` 实测可以收到 `gaochang/lift/f407zet6/command` 下发；
- 宽订阅会收到本机 telemetry/status 回环，所以固件必须做 payload 过滤。

当前项目最终建议：

```text
AT+MQTTSUB=1,"gaochang/lift/#",0,1,1
AT+BLOCKINFO=0,0
```

Web 端仍然只向正式命令主题下发：

```text
gaochang/lift/f407zet6/command
```

固件只处理满足以下条件的 JSON：

```json
{"type":"command","device":"gaochang_lift_f407zet6","cmd":"ping","command":"ping","msg_id":"xxx"}
```

非 `type:"command"` 的 telemetry/status，或 `device` 不匹配本机的 payload，固件必须直接丢弃，避免 MQTT 回环触发误动作。

### 8.5 COM52 实测 ClientID 冲突坑

2026-06-05 继续用 COM52 做 Web 到模块闭环测试时，出现过以下现象：

- `AT+ASKNET?` 返回 `+NETMODE:4,"4G LTE Net"`，说明 4G 已驻网；
- `AT+ASKCONNECT?` 返回 `+ASKCONNECT: 1,0`，说明通道 1 曾建立连接；
- 但透传态发送 JSON 不上 MQTT，Web 下发也不吐串口；
- 透传态再发 `AT` 时模块上报 `+STATUS: 1, MQTT CLOSED`。

将 ClientID 从 `gaochang_lift_f407zet6` 改为 `gaochang_lift_f407zet6_dtu` 后，`AT+CFUN=1,1` 重启出现：

```text
STATUS: 1, MQTT CONNECTED
```

随后完整闭环通过：

- COM52 写 telemetry JSON -> Web 数据库更新；
- Web API `/api/commands/query/gaochang_lift_f407zet6` 下发 -> COM52 收到 `type:"command"` JSON；
- COM52 写 ack status JSON -> Web `command_queue` 更新为 `responded/reported`。

结论：ClientID 必须全局唯一。设备身份用 payload 中的 `device:"gaochang_lift_f407zet6"` 表示，MQTT 会话 ID 用 `gaochang_lift_f407zet6_dtu`。

---

## 九、运行期查询与诊断

### 9.1 查询 MQTT/TCP 连接状态

```text
AT+ASKCONNECT?
```

示例：

```text
+ASKCONNECT: 1,0
OK
```

含义：通道 1 已连接，通道 2 未连接。

### 9.2 查询驻网状态

```text
AT+ASKNET?
```

未驻网：

```text
+NETMODE:0,"UnRegist"
OK
```

4G 驻网成功：

```text
+NETMODE:4,"4G LTE Net"
OK
```

### 9.3 查询信号强度

```text
AT+CSQ
```

CSQ 粗略判断：

| CSQ | 现场判断 |
|---|---|
| 0-9 | 差信号，容易掉线 |
| 10-14 | 勉强可用 |
| 15-20 | 可用 |
| 21-31 | 较好 |
| 99 | 未知或未获取 |

### 9.4 查询 SIM 卡 ICCID

```text
AT+ICCID
```

### 9.5 查询 IMEI

```text
AT+GSN
```

### 9.6 查询所有 DTU 参数

```text
AT+ALL?
```

用于现场导出配置，修复前建议先记录。

---

## 十、远程网络 AT

模块支持透传模式下通过网络发送远程配置指令，格式：

```text
@DTU:0000:指令
```

普通 AT 指令远程写法：去掉 `AT+`。

| 串口 AT | 网络 AT |
|---|---|
| `AT+DTUID?` | `@DTU:0000:DTUID?` |
| `AT+CSQ` | `@DTU:0000:CSQ` |
| `AT+ICCID` | `@DTU:0000:ICCID` |

特殊指令：

| 网络 AT | 说明 |
|---|---|
| `@DTU:0000:AT&W` | 保存参数 |
| `@DTU:0000:AT&F` | 恢复出厂并重启 |
| `@DTU:0000:POWEROFF` | 重启设备 |
| `@DTU:0000:CSQ` | 查询信号 |
| `@DTU:0000:ICCID` | 查询 ICCID |

> [!WARNING]
> 举升机是安全相关设备。远程 AT 只建议用于状态查询和受控维护，不建议开放给普通 Web 用户直接下发。

---

## 十一、APN 与定向卡

手册说明：

- 公网 SIM 通常不需要额外设置 APN；
- TAS-LTE-892D_s4 当前不建议自行配置专网卡 APN，专网 APN 需求应联系塔石商务确认；
- 使用专网 APN 会影响塔石远程配置平台和远程升级；
- 使用定向卡时，需要把模块会访问的外部服务器加入白名单，否则可能掉线；
- 设置 APN 或使用定向卡时，应关闭远程配置平台。

关闭远程配置平台：

```text
AT+RCMDCLOUDEN=0
AT&W
AT+CFUN=1,1
```

---

## 十二、STM32 固件集成建议

### 12.1 UART

本项目约束：

- USART3 优先 `9600,8,N,1`，新设备/异常设备按 `9600 -> 115200 -> 报错离线` 的顺序探测；
- 如果 9600 已能 AT 通信，不再无意义切换 115200；
- 使用 DMA + IDLE 接收，RX DMA 保持 `DMA_CIRCULAR`；
- UART TX DMA 必须是 `DMA_NORMAL`，不要配置成 circular；
- AT 配置和业务 JSON 发送可在 DTU 任务内阻塞发送，避免 TX DMA 状态机影响配置可靠性；
- 接收缓冲至少 1024 字节，JSON payload 可能接近 1KB。

### 12.2 配置状态机

建议固件不要每次开机都盲目完整重配。推荐状态机：

```text
BOOT
  -> INIT_UART_9600
  -> PASSIVE_WAIT_MQTT_CONNECTED
  -> PROBE_9600_AND_QUERY_SAVED_LINK
  -> PROBE_115200_AND_QUERY_SAVED_LINK
  -> CONFIG_IF_NO_VALID_LINK
  -> SAVE_AND_REBOOT_DTU
  -> WAIT_MQTT_CONNECTED
  -> TRANSPARENT_READY
```

关键点：

- 已配过网且 `AT+ASKCONNECT?` 显示通道 1 已连接时，不再重配、不再 `AT&W`、不再 `AT+CFUN=1,1`；
- `AT+ASKCONNECT?` 不能只等待 `OK` 就返回，应收集完整响应窗口并解析 `+ASKCONNECT: 1,0`；实测模块可能先吐 `OK`，随后才吐连接状态；
- 只有检测不到有效保存连接时，才完整配置 MQTT 并重启 DTU；
- `AT` 成功说明当前在命令模式；
- 透传模式下需要 `+++` 进入命令模式；
- `+++` 只等待 `OK`，不要走旧资料的 `a / +ok` 握手；
- 配置后用 `AT&W` 保存；
- 参数变更后用 `AT+CFUN=1,1` 重启，NET/LINK 灯会短暂熄灭，等待重新驻网和 MQTT 连接；
- 正常启动优先 `+++` 进入命令模式，避免透传态下把 `AT` 当作业务数据发到 MQTT；
- 退出命令模式可用 `ATO`，但本项目配置后直接 `AT+CFUN=1,1` 重启并等待 `MQTT CONNECTED`；
- 收到 `+STATUS: 1, MQTT CONNECTED` 后才允许上报业务 JSON。
- 正常透传期间不要周期发送 `AT+CSQ` 或 `@DTU:0000:CSQ`，否则会和 MQTT 下行 JSON 共用同一个串口流，导致命令被等待响应逻辑吞掉。信号查询只作为人工诊断动作。
- 物联网失败只允许影响 DTU/MQTT 状态，不能阻塞举升机本地控制、安全、按键任务；DTU 任务后台周期重试即可。

### 12.3 下行 JSON 解析

DTU 会把 MQTT payload 原样送到串口。STM32 应按 JSON 对象边界解析：

- 从 `{` 开始累计；
- 根据 `{` / `}` 深度判断完整 JSON；
- 支持 `\r\n` 或 `\n` 结尾；
- 超长或无效 JSON 要丢弃并清空缓冲；
- 控制命令必须校验 `device`、`cmd`、权限字段。

### 12.4 举升机安全约束

远程控制必须遵守：

- 普通远程命令只能锁机、解锁、查询；
- 故障清除、点动必须进入管理模式；
- 锁机状态下必须立即停止运动；
- 有安全报警时，不允许远程点动绕过本地安全逻辑；
- 所有远程命令要有日志与回执。

---

## 十三、Web 端 MQTT 集成要求

Web 后端必须做到：

- 连接同一个 Broker：`mqtt://192.168.1.10:1883`；
- 订阅：
  - `gaochang/lift/+/telemetry`
  - `gaochang/lift/+/status`
  - 可选 `gaochang/lift/+/response`
- 下发：
  - `gaochang/lift/{gatewayId}/command`
- 从 payload 的 `device` 字段识别设备 ID；
- 从 topic 的 `{gatewayId}` 字段识别 DTU 网关；
- 将 MQTT 状态通过 WebSocket/SSE 推给前端；
- 离线判断不能只靠前端刷新，应由后端按 `lastSeen` 超时计算。

### 13.1 Web 下发命令示例

```json
{
  "type": "command",
  "device": "gaochang_lift_f407zet6",
  "cmd": "lock",
  "command": "lock",
  "msg_id": "cmd_lock_001",
  "account": "admin",
  "ts": 1710000000000
}
```

### 13.2 设备回执示例

```json
{
  "type": "status",
  "device": "gaochang_lift_f407zet6",
  "seq": 22,
  "event": "lock_ok",
  "state": "locked",
  "locked": 1,
  "dtu": {
    "state": "transparent",
    "csq": 20
  }
}
```

---

## 十四、常用 AT 指令速查表

### 14.1 基础

| 指令 | 说明 |
|---|---|
| `+++` | 透传模式进入命令模式 |
| `AT` | 测试 AT 通信 |
| `ATO` | 命令模式返回透传模式 |
| `AT&W` | 保存当前配置 |
| `AT&F` | 恢复出厂设置并重启 |
| `AT+CFUN=1,1` | 重启模块 |
| `AT+ALL?` | 查询所有 DTU 参数 |
| `AT+RUNNINGLOG?` | 查询注网后运行时间、平均 CSQ、内存等 |

### 14.2 串口

| 指令 | 说明 |
|---|---|
| `AT+UARTCFG?` | 查询串口 1 参数 |
| `AT+UARTCFG=9600,1,0,0` | 串口 1 设置为 9600,8,N,1；本项目现场固定值 |
| `AT+UART2CFG?` | 查询串口 2 参数 |
| `AT+UARTCONTROL=0` | 串口 1 无流控 |
| `AT+UART2CONTROL=0` | 串口 2 无流控 |
| `AT+DTUPACKET=0,1024` | 串口打包时间自适应，长度 1024 |

### 14.3 网络与状态

| 指令 | 说明 |
|---|---|
| `AT+ASKNET?` | 查询蜂窝驻网状态 |
| `AT+ASKCONNECT?` | 查询 2 个 SOCKET 通道连接状态 |
| `AT+CSQ` | 查询信号强度 |
| `AT+ICCID` | 查询 SIM ICCID |
| `AT+CPIN` | 查询是否识卡 |
| `AT+TIME` | 查询实时时间 |
| `AT+TIMESTAMP` | 查询时间戳 |
| `AT+SELECTSIMCARD?` | 查询 SIM 卡策略 |
| `AT+SELECTSIMCARD=0` | 自动切卡 |
| `AT+SELECTSIMCARD=1` | 使用 SIM0 |
| `AT+SELECTSIMCARD=2` | 使用 SIM1 |
| `AT+GSN` | 查询 IMEI |
| `AT+CGMR` | 查询版本号 |
| `AT+DEVICEID?` | 查询模块标识符 |

### 14.4 MQTT

| 指令 | 说明 |
|---|---|
| `AT+DTUMODE=2,1` | 通道 1 设置 MQTT 透传 |
| `AT+IPPORT="host",1883,1` | 设置 MQTT Broker |
| `AT+CLIENTID="clientid",1` | 设置 ClientID |
| `AT+USERPWD="user","pwd",1` | 设置用户名密码 |
| `AT+MQTTSUB=1,"topic",0,1,1` | 设置订阅主题 |
| `AT+MQTTPUB=1,"topic",0,0,1,1` | 设置发布主题 |
| `AT+MQTTPUBID=1,1,1,"TEL:"` | 设置发布主题区分字符串 |
| `AT+MQTTKEEP=120,1` | 设置 MQTT keepalive |
| `AT+CLEANSESSION=1,1` | 设置 clean session |
| `AT+BLOCKINFO=0,0` | 本项目实测下行更稳定，固件负责过滤回环 JSON |
| `AT+WILL="topic","payload",0,0,1` | 设置遗嘱 |

### 14.5 保活与自恢复

| 指令 | 说明 |
|---|---|
| `AT+KEEPALIVE=60,0,"keepalive",1` | 设置业务心跳 |
| `AT+HEARTDODGE=0,1` | 业务心跳避让，默认建议避让 |
| `AT+RELINKTIME=30` | 掉线重连间隔 30 秒 |
| `AT+DSCTIME=300` | 300 秒未连上服务器则重启 |
| `AT+ACKTIME=0` | 关闭无下行数据重启 |
| `AT+PORTTIME=0` | 关闭无上行数据重启 |
| `AT+RESTIME=0` | 关闭定时重启 |

---

## 十五、现场调试流程

### 15.1 上电检查

1. POWER 灯亮；
2. WORK 灯 1 秒闪烁；
3. NET 灯亮，说明蜂窝网络挂载；
4. LINK 灯亮，说明 MQTT Broker 已连接。

### 15.2 串口检查

先确认波特率和模式：

- 本项目固定 `9600,8,N,1`；
- 如果 `AT` 无响应，先发 `+++`，等待 `OK` 后再发 `AT`；
- 如果仍无响应，检查 STM32 USART3 TX/RX、GND、供电和 RX DMA，不要盲目切换到 115200。

### 15.3 MQTT 检查

串口查询：

```text
AT+ASKNET?
AT+ASKCONNECT?
AT+CSQ
```

Broker 侧订阅：

```bash
mqtt_sub -h 192.168.1.10 -p 1883 -t "gaochang/lift/#" -v
```

下发测试：

```bash
mqtt_pub -h 192.168.1.10 -p 1883 \
  -t "gaochang/lift/f407zet6/command" \
  -m "{\"type\":\"command\",\"device\":\"gaochang_lift_f407zet6\",\"cmd\":\"ping\",\"msg_id\":\"test001\"}"
```

期望设备回传：

```json
{"type":"status","event":"pong",...}
```

---

## 十六、常见故障

### 16.1 `AT` 无响应

可能原因：

- MCU 串口波特率与模块不一致；
- RS232/RS485/TTL 电平接错；
- TX/RX 接反；
- 模块仍在透传模式，业务数据被发到网络；
- 发送缺少 `\r\n`；
- DMA 接收未启动或中断未进。

处理：

1. 用 USB 转串口直连模块确认波特率；
2. 发 `+++`，等待 `OK`；
3. 发 `AT\r\n`；
4. 必要时长按 RELOAD 3 秒恢复默认参数。

### 16.2 LINK 灯不亮

可能原因：

- SIM 未入网；
- Broker 地址或端口错误；
- MQTT ClientID 冲突；
- 用户名密码错误；
- 订阅了 Broker 不允许的主题，导致 MQTT SUB LOST；
- 定向卡未加 Broker 白名单。

处理：

```text
AT+ASKNET?
AT+ASKCONNECT?
AT+CSQ
AT+IPPORT?
AT+CLIENTID?
AT+USERPWD?
AT+MQTTSUB?
AT+MQTTPUB?
```

### 16.3 Web 收不到设备数据

检查：

- DTU 是否已 `MQTT CONNECTED`；
- Web 后端是否连接同一 Broker；
- Web 后端是否订阅 `gaochang/lift/+/telemetry`；
- DTU 发布主题是否写成 `gaochang/lift/f407zet6/telemetry`；
- payload 是否是合法 JSON；
- 固件是否实际发送到串口；
- 若启用了双发布主题，是否配置了 `AT+MQTTPUBID` 分流。

### 16.4 设备收不到 Web 命令

检查：

- DTU 是否按本项目实测方案订阅 `gaochang/lift/#`；
- Web 下发主题是否正好是 `gaochang/lift/f407zet6/command`；
- DTU 是否配置为 `AT+BLOCKINFO=0,0`；
- STM32 串口接收是否解析到完整 JSON；
- payload 是否带 `type:"command"` 和 `device:"gaochang_lift_f407zet6"`；
- 命令字段是否为固件支持的 `cmd`。

---

## 十七、项目当前必须修正的方向

当前固件若仍使用旧资料指令，应按完整手册替换：

| 当前旧指令 | 正确方向 |
|---|---|
| `AT+WKMOD=DTU` | `AT+DTUMODE=2,1` |
| `AT+SOCKAEN=ON` | 不使用，改用 `AT+DTUMODE` 启用通道 |
| `AT+SOCKA=MQTT,...` | 拆成 `AT+IPPORT`、`AT+CLIENTID`、`AT+USERPWD`、`AT+MQTTSUB`、`AT+MQTTPUB` |
| `AT+S` | `AT&W` |
| `AT+ENTM` | `ATO` |
| `AT+Z` | `AT+CFUN=1,1` |
| `+++` 后等待 `a/+ok` | `+++` 后等待 `OK` |

Web 端必须与固件主题严格一致：

```text
上报：gaochang/lift/f407zet6/telemetry
状态：gaochang/lift/f407zet6/status
命令：gaochang/lift/f407zet6/command
```

若固件不加 `TEL:` / `STA:` 前缀，就不要同时开启两个发布主题。

