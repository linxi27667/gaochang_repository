# 客户站点部署 Profile

每个客户站点使用 `<site-code>/` 目录保存脱敏部署 Profile、兼容矩阵与验收记录索引。不得保存客户个人信息、数据库导出、设备密钥、MQTT 凭据、私钥或生产日志。

推荐最小结构：

```text
<site-code>/
├── README.md
├── inventory.example.yaml
├── compatibility.yaml
└── cred-refs.md
```

生产连接与密钥仅以 `cred://bitwarden/corp/...` 引用表示。任何远程升级先经测试设备与单机 canary，并保留固件和平台回滚目标。
