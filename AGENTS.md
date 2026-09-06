# GcLiftIoT 工作规则

本仓是面向外部客户的举升机 IoT 产品平台，当前包含四类 STM32F407 固件与 Web 管理端。它不等同于 `GcFactoryIoT`，也不替代 `GcMultiLinkLift` 的底层控制板和 OTA 主档。

## 边界

- `GcMultiLinkLift`：板卡、Bootloader、OTA 基础、硬件控制与设备协议主档。
- 本仓：客户/站点/设备管理、遥测、告警、远程支持、Web/API、产品固件 Profile 与受控部署。
- `GcFactoryIoT`：公司内部工厂设备的只读采集、语义模型和现场验收。

## 安全与现场

- 不提交客户个人信息、设备密钥、MQTT 凭据、数据库导出、生产日志、固件构建产物或学校现场配置原件。
- 任何协议、MQTT Topic、Web/API、升级路径或生产配置的改变，先完成测试设备验证、单机 canary 和可回滚验收。
- 站点 Profile 只保存脱敏结构和 `cred://` 引用；真实凭据只进入 Bitwarden。
- 在正式拆分共享代码前，使用 `contracts/` 记录 `GcMultiLinkLift` 与本仓之间的版本兼容关系；不创建 Git submodule。
