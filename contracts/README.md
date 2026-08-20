# 举升机 IoT 兼容契约

本目录记录 `GcMultiLinkLift` 与 GcLiftIoT 的版本化接口，不复制控制板唯一实现。

每次可部署固件或平台发布至少登记：

| 字段 | 含义 |
| --- | --- |
| `hardware_revision` | 板卡/硬件修订号 |
| `protocol_version` | 设备消息与命令协议版本 |
| `firmware_version` | 固件构建版本 |
| `minimum_platform_version` | 兼容的平台最低版本 |
| `ota_rollback_target` | 可回退固件或平台版本 |
| `validation_evidence` | 测试设备、日期和验收记录位置 |

首个学校站点上线前，必须建立其设备清单、当前板卡/固件版本、MQTT/Web/API、部署主机、备份、负责人和升级回滚记录。
