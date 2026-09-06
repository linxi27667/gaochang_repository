# 物联网 4G DTU / MQTT 修复计划

> 项目：高昌物联网举升机 F407
> 依据：TAS-LTE-892D_s4 完整手册、当前固件与 Web 端源码检查
> 当前状态：教程已重写，固件和 Web 尚未修复。

---

## 0. 已完成

1. 已从完整 PDF 资料重写合并教程：
   - 项目内：`docs/TAS-LTE-892D_s4_4G_DTU_完整使用教程.md`
   - Obsidian：`C:\Users\30817\Documents\Obsidian Vault\高昌\TAS-LTE-892D_s4 4G DTU 使用教程.md`
2. 已删除旧的分散 AT 教程：
   - `C:\Users\30817\Documents\Obsidian Vault\高昌\TAS-LTE-892D_s4 AT指令与STM32双向通信教程.md`
3. 已确认 Node Web 文件语法：
   - `web_monitor/server.js`
   - `web_monitor/mqtt-bridge.js`
   - `web_monitor/commands.js`
   - `web_monitor/server/server.js`

---

## 1. 当前关键问题

### 1.1 固件 AT 指令与完整手册不匹配

位置：`APP/Src/app_tas_dtu.c`

当前仍使用旧资料指令：

| 位置 | 当前指令 | 问题 |
|---|---|---|
| `App_TasDtu_EnterCommandMode()` | `+++` 后等待 `a/+ok` | 完整手册为 `+++` 后返回 `OK` |
| `App_TasDtu_ConfigureMqtt()` | `AT+WKMOD=DTU` | 完整手册应使用 `AT+DTUMODE=2,1` |
| `App_TasDtu_ConfigureMqtt()` | `AT+SOCKAEN=ON` | 完整手册无此主流程 |
| `App_TasDtu_SetMqttSocket()` | `AT+SOCKA=MQTT,...` | 应拆为 `AT+IPPORT/CLIENTID/USERPWD/MQTTSUB/MQTTPUB` |
| `App_TasDtu_ConfigureMqtt()` | `AT+S` | 应为 `AT&W` |
| `App_TasDtu_Reboot()` | `AT+Z` | 应为 `AT+CFUN=1,1` |
| `App_TasDtu_QuerySignal()` | `AT+ENTM` | 应为 `ATO` |

结论：当前固件很可能无法正确配置 TAS-LTE-892D_s4 的 MQTT。

### 1.2 固件过早认为 MQTT 已就绪

位置：`APP/Src/app_tas_dtu.c:812-818`

当前逻辑配置保存后延时 5 秒，就设置：

```c
g_tas_dtu_status.transparent_ready = 1U;
g_tas_dtu_status.state = TAS_DTU_STATE_TRANSPARENT;
```

问题：没有等待 `+STATUS: 1, MQTT CONNECTED`，也没有查询 `AT+ASKCONNECT?`。实际 Broker 未连接时，固件仍会周期上报，数据会丢。

### 1.3 周期 CSQ 查询会打断透传

位置：`Driver/Src/dri_tas_dtu.c:77-89`、`APP/Src/app_tas_dtu.c:845-883`

当前每 60 秒进入命令模式查询 `AT+CSQ`，再用旧指令退出。风险：

- `+++` 进入命令模式过程可能和业务 JSON 上报冲突；
- 如果退出失败，会导致长期不在透传模式；
- 实测 `@DTU:0000:CSQ` 会和 MQTT 下行 JSON 共用同一个串口流，等待 `+CSQ:` 的逻辑可能吞掉 Web 命令。

结论：正常业务链路禁止周期 CSQ 查询。信号强度只作为人工维护诊断项，不放在 DTU 任务周期里。

### 1.4 命令回执协议不闭环

Web 下发命令带 `msg_id`：

```json
{"cmd":"lock","msg_id":"xxx"}
```

固件当前回执只发事件：

```json
{"type":"status","event":"lock_ok",...}
```

问题：

- 固件没有回传 `msg_id`；
- 固件没有回传 `cmd` / `result`；
- `web_monitor/mqtt-bridge.js` 只有收到 `msg_id` 才会把 `command_queue` 更新为 `responded`；
- 前端 `handleCommandResponse()` 期待 `data.cmd === "lock"` 且 `data.result === "locked"`。

结论：Web 能发命令，设备也能执行，但 Web 端无法可靠确认命令完成。

### 1.5 Web 端存在两套服务实现

当前仓库同时存在：

| 路径 | 特点 |
|---|---|
| `web_monitor/server.js` | Express + SQLite + WebSocket，根 `package.json` 默认入口 |
| `web_monitor/server/server.js` | 原生 http + JSON 文件 + SSE，子目录 `package.json` 默认入口 |

两者都连接 MQTT，主题基本一致，但数据库、API、前端、配置变量不同。部署时如果跑错入口，会出现“看似 MQTT 已连接，但页面/数据库不是同一套”的问题。

建议后续选择 `web_monitor/server.js` 作为正式入口，另一套归档或删除。

### 1.6 当前 CMake 构建不可用

验证命令：

```powershell
cmake --build build\Debug
```

失败核心原因：

```text
Middlewares/Third_Party/FreeRTOS/Source/portable/RVDS/ARM_CM4F/portmacro.h
```

被 GCC 编译，触发 `__forceinline`、`msr` 等 ARMCC/RVDS 语法错误。

配置位置：

```text
cmake/stm32cubemx/CMakeLists.txt
```

当前引用：

```text
Middlewares/Third_Party/FreeRTOS/Source/portable/RVDS/ARM_CM4F
Middlewares/Third_Party/FreeRTOS/Source/portable/RVDS/ARM_CM4F/port.c
```

后续要先修构建，否则 4G 修复无法编译验证。

---

## 2. 修复目标

最终目标：

1. F407 能按完整手册配置 TAS-LTE-892D_s4 到 MQTT 透传；
2. 模块真正连接 Broker 后才允许业务上报；
3. Web 后端能稳定收到遥测和状态；
4. Web 下发命令后，设备执行并带 `msg_id` 回执；
5. 命令队列、操作日志、前端提示都能闭环；
6. 构建能通过，现场可烧录测试；
7. 上机测试时能按日志定位失败点。

---

## 3. 推荐实施顺序

### 阶段 A：先修构建

1. 修 `cmake/stm32cubemx/CMakeLists.txt`：
   - 不要用 `portable/RVDS/ARM_CM4F` 给 GCC 编译；
   - 若仓库缺少 `portable/GCC/ARM_CM4F`，从同版本 CubeMX/FreeRTOS 补齐；
   - include 路径与 `port.c` 一起切到 GCC 端口。
2. 清理或确认 `Core/Inc/main.h` 中残留的 `huart2/hdma_usart2_*` 外部声明是否仍需要。
3. 重新执行：

```powershell
cmake --build build\Debug
```

验收：至少进入真实业务代码编译阶段，不再卡在 FreeRTOS RVDS 端口。

### 阶段 B：修 DTU AT 驱动

修改 `APP/Src/app_tas_dtu.c`：

1. `App_TasDtu_EnterCommandMode()`：
   - 发送 `+++`；
   - 等待 `OK`；
   - 去掉 `a/+ok` 握手。
2. 新增配置命令序列：

```text
AT+DTUMODE=2,1
AT+IPPORT="8.134.167.240",1883,1
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

3. `App_TasDtu_Reboot()` 改为 `AT+CFUN=1,1`。
4. 删除周期 `App_TasDtu_QuerySignal()`：
   - 正常透传链路不主动查询 CSQ；
   - 避免等待 AT 响应时吞掉 MQTT 下行 JSON；
   - 如需信号诊断，使用串口助手或维护固件人工执行。
5. 增加状态解析：
   - `+STATUS: 1, MQTT CONNECTED` -> `transparent_ready=1`
   - `+STATUS: 1, MQTT CLOSED` -> `transparent_ready=0`
   - `+STATUS: 1, MQTT SUB LOST` -> error/reconfigure
6. 不要配置后固定延时 5 秒就认为 ready。

验收：

- 串口日志能看到正确 AT 指令；
- 模块 LINK 灯亮；
- 固件日志进入 `transparent_ready` 的依据是 MQTT connected。

### 阶段 C：修 MQTT payload 回执

修改 `APP/Src/app_tas_dtu.c` 与 `APP/Src/app_lift_iot.c`：

1. 解析命令时提取：
   - `msg_id`
   - `cmd`
   - `account`
2. 状态回执加入：

```json
{
  "type": "status",
  "device": "gaochang_lift_f407zet6",
  "cmd": "lock",
  "msg_id": "xxx",
  "result": "locked",
  "event": "lock_ok"
}
```

3. 建议结果映射：

| 命令 | 成功 result | 失败 result |
|---|---|---|
| `ping` | `pong` |  |
| `get_status` | `reported` | `report_failed` |
| `lock` | `locked` | `lock_failed` |
| `unlock` | `unlocked` | `unlock_failed` |
| `admin_enter` | `admin_entered` | `admin_denied` |
| `admin_exit` | `admin_exited` |  |
| `fault_clear` | `fault_cleared` | `fault_clear_denied` |
| `admin_jog` | `admin_jog_ok` | `admin_jog_denied` |
| `maintenance_done` | `maintenance_done` |  |
| `reboot_dtu` | `rebooting` | `reboot_failed` |

验收：

- `web_monitor` 的 `command_queue` 能从 `sent` 变为 `responded`；
- 前端收到 `command_response` 后提示准确。

### 阶段 D：统一 Web 入口

建议保留：

```text
web_monitor/server.js
```

处理：

1. 确认正式启动命令：

```powershell
cd web_monitor
npm start
```

2. 子目录 `web_monitor/server/`：
   - 如不再使用，移动到 `web_monitor_legacy` 或删除；
   - 若保留，必须 README 明确“旧版，不用于正式部署”。
3. 统一环境变量：
   - `MQTT_BROKER=mqtt://8.134.167.240:1883`
   - `MQTT_TOPIC_PREFIX=gaochang/lift`
   - `MQTT_GATEWAY_ID=f407zet6`
   - `MQTT_DEVICE_ID=gaochang_lift_f407zet6`
4. `mqtt-bridge.js` 增强：
   - 兼容 `event: lock_ok` 映射为 `cmd/result`；
   - 或要求固件严格回传 `cmd/msg_id/result`；
   - 增加 MQTT 离线超时，把设备 `online=0`。

验收：

- `/api/mqtt-status` 显示 connected；
- 设备上报后 `/api/devices` 和页面同步刷新；
- Web 下发命令能在 Broker 上抓到；
- 设备回执后 `command_queue` 状态更新。

### 阶段 E：补全模拟测试

更新 `web_monitor/tools/e2e_test.js` 和 `web_monitor/tools/mqtt_full_test.js`。

当前测试里仍有旧主题：

```text
lift/lift_001/up
lift/lift_001/down
```

应改为：

```text
gaochang/lift/f407zet6/telemetry
gaochang/lift/f407zet6/status
gaochang/lift/f407zet6/command
```

测试项：

1. MQTT Broker 连接；
2. 发布 telemetry，Web 数据库更新；
3. 发布 status，Web 记录事件；
4. Web API 下发 lock，测试客户端收到 command；
5. 测试客户端发布带 `msg_id` 回执，Web command_queue 变为 responded；
6. 模拟设备 20 秒不上报，Web 标记离线。

验收：

```powershell
cd web_monitor
npm start
node tools/e2e_test.js
```

### 阶段 F：上机测试

上机前准备：

1. 串口调试器确认模块当前波特率；
2. MQTT Broker 开启抓包订阅：

```bash
mqtt_sub -h 8.134.167.240 -p 1883 -t "gaochang/lift/#" -v
```

3. F407 RTT/串口日志开启 DTU 日志。

上机顺序：

1. 上电，看 POWER/WORK/NET/LINK；
2. 固件输出 AT 配置过程；
3. 等待 `MQTT CONNECTED`；
4. 确认 Broker 收到 boot/status；
5. Web 页面看到在线；
6. 下发 `ping/get_status/lock/unlock`；
7. 验证设备执行和回执；
8. 断 SIM 或断 Broker，验证离线与重连；
9. 恢复网络，验证自动重连。

---

## 4. 风险与注意事项

1. 远程控制举升机不允许绕过本地安全逻辑；
2. 锁机必须立即停止运动；
3. 故障清除、点动必须要求管理权限；
4. 不建议每次上电都 `AT&W`，会增加 flash 写入；
5. 本项目现场 DTU 固定 `9600,8,N,1`，不要做自动波特率或 115200 回退；
6. 若启用两个发布主题，固件必须加 `TEL:` / `STA:` 分流前缀，否则数据会同时发到两个主题；
7. 当前最稳方案是只启用 telemetry 一个发布主题，通过 JSON `type` 区分 telemetry/status。

---

## 5. 2026-06-05 修复执行记录

本轮已完成：

- CMake/GCC 构建从 FreeRTOS `RVDS/ARM_CM4F` 端口切换到官方 FreeRTOS Kernel V10.3.1 `GCC/ARM_CM4F` 端口。
- Keil5/ARMCC 保留 `RVDS/ARM_CM4F` 端口，继续适配 Keil 原工程。
- RTOS 调度入口改为原生 FreeRTOS：`xTaskCreate`、`vTaskStartScheduler`、`vTaskDelete`；延时继续使用 `osDelay`。
- TAS-LTE-892D_s4 配置流程改为完整手册指令：`+++` 等 `OK`、`AT+DTUMODE=2,1`、`AT+IPPORT/CLIENTID/USERPWD/MQTTSUB/MQTTPUB`、`AT&W`、`AT+CFUN=1,1`。
- COM52 实测发现 exact topic `gaochang/lift/f407zet6/command` 订阅查询正常但下行不稳定，固件改为订阅 `gaochang/lift/#`，并只处理 `type:"command"` 且 `device` 匹配本机的 JSON。
- COM52 复测发现 `gaochang_lift_f407zet6` 作为 ClientID 时曾出现 `MQTT CLOSED`，改为唯一会话 ID `gaochang_lift_f407zet6_dtu` 后恢复 `MQTT CONNECTED`。设备 ID 继续放在 JSON payload 的 `device` 字段。
- 为避免宽订阅下误屏蔽或回环误处理，DTU 配置使用 `AT+BLOCKINFO=0,0`，过滤职责放在固件 payload 层。
- 固件等待 `+STATUS: 1, MQTT CONNECTED` 后才进入透传 ready。
- 删除周期信号查询，正常业务期间不发送 `AT+CSQ` 或 `@DTU:0000:CSQ`。
- 固件命令回执补齐 `cmd/msg_id/result`，Web 端 `command_queue` 可以闭环。
- Web MQTT bridge 支持 `device` 和 `device_id`，兼容旧 `event` 回执映射，并支持一个 MQTT payload 内包含多条 JSON。
- Web 测试脚本主题切换到 `gaochang/lift/f407zet6/{telemetry,command}`。

验证结果：

- `node --check`：`server.js`、`mqtt-bridge.js`、`commands.js`、`tools/mqtt_full_test.js`、`tools/e2e_test.js` 均通过。
- `cmake --build build\Debug`：通过，生成 `Ball_And_Plant_System.elf/.hex/.bin`。
  - RAM：30976 B / 128 KB，约 23.63%。
  - Flash：76856 B / 512 KB，约 14.66%。
- Keil5：`MDK-ARM\gaochang_lift_2.uvprojx` 使用 ARMCC V5.06 编译通过。
  - 输出：`MDK-ARM\gaochang_lift_2\gaochang_lift_2.hex`。
  - 结果：0 Error(s), 0 Warning(s)。

仍需上机验证：

- USART3 与 TAS-LTE-892D_s4 电平和线序，特别是 RS232/RS485/TTL 子型号不能混接。
- 上电后观察 `+STATUS: 1, MQTT CONNECTED` 是否稳定出现。
- Broker 订阅 `gaochang/lift/#`，验证 telemetry/status/command 全闭环。
- 断网、断电、弱信号、SIM 欠费、Broker 重启、服务器离线等异常场景。
- 锁机/解锁必须结合安全联锁和电机停止动作实测，不能只看 MQTT 回执。

---

## 6. 2026-06-06 现场复测结论

已验证并烧录的阶段方案：

- USART3 优先 `9600,8,N,1`，启动按 `9600 -> 115200 -> 报错离线` 探测；9600 已可通信时不切 115200。
- 启动优先等待保存配置自动上报 `MQTT CONNECTED`；未收到时查询保存连接，已连接则不重配。
- `AT+ASKCONNECT?` 使用固定响应窗口收集，必须解析到 `+ASKCONNECT: 1,0` 才认为通道 1 已连接，不能只等 `OK`。
- 只有保存连接不可用时，才强制配置 MQTT、`AT&W` 保存并 `AT+CFUN=1,1` 重启 DTU。
- MQTT 启动失败不影响举升机本地控制、安全、按键任务；DTU 任务每 60 秒后台重试。
- 启动配置先发送 `+++` 进入命令模式，再执行 MQTT 配置，避免透传态下把 `AT` 当作业务数据发布。
- USART3/USART6 TX DMA 改为 `DMA_NORMAL`，RX DMA 保持 `DMA_CIRCULAR`。
- DTU 任务不再周期查询 CSQ，避免吞掉 Web 下行命令。
- AT 等待响应使用尾部滚动缓冲，避免模块重启长日志挤满缓冲后漏检 `MQTT CONNECTED`。
- 首次 MQTT connected 等待放宽到 120 秒，适配现场蜂窝网络慢启动。
- Web MQTT bridge 支持 DTU 合包：一个 MQTT payload 内多条 JSON 能逐条解析。

实测结果：

- Keil5 ARMCC V5.06：0 Error(s), 0 Warning(s)。
- CMake/GCC：通过，RAM 30992 B / 128 KB，FLASH 80648 B / 512 KB。
- J-Link 烧录：Programming Done，Verify OK。
- RTT：先等待保存 MQTT 状态，再 9600 探测保存连接；`AT+ASKCONNECT?` 收到完整 `+ASKCONNECT: 1,0` 后识别为已连接，跳过重配和 `AT+CFUN=1,1`，随后 5 秒周期稳定上报。
- Web API 下发 `get_status`，数据库 `command_queue.status=responded`，`result=reported`。
