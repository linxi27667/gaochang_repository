# Gc_Iot_Lift

高昌物联网举升机项目集合。仓库内包含四个 STM32F407 固件项目和一个 Node.js Web 管理端。

## 项目列表

| 目录 | 项目 |
|---|---|
| `GC_Big_Scissor` | 大剪举升机固件 |
| `GC-Two_Pillars` | 两柱举升机固件 |
| `GC_Small_Scissor` | 小剪举升机固件 |
| `GC_Thin_Scissor` | 超薄小剪举升机固件 |
| `Gaochang_Iot_Web` | 物联网 Web 管理端 |

## 重要文档

- `所有控制逻辑.md`：按当前源码整理的四类举升机控制逻辑总览。
- 各子项目 `README.md`：说明该项目目录结构、关键文件和调试入口。

## 清理约定

- `RTT_easylog` 是日志库源码，必须保留。
- `*.log`、`rtt_capture*.txt`、`rtt_logger*.txt`、`jlink_*.txt`、测试 `out` 目录属于临时构建/调试输出，不应长期保留。
- 固件工程文件、源码、脚本、文档、测试源码不应作为清理对象。
