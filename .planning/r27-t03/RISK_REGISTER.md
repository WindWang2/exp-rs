# R27-T3 Risk Register

| 风险 | 等级 | 缓解 |
|---|---|---|
| tests/CMakeLists.txt 重建引入语义漂移 | 高 | 多重集等价证明（master 零丢失、969 目标无缺无重）+ configure 绿 + 只追加自己块；备份于 /tmp/master_mangled_backup.cmake |
| #1733 wire 兼容：approve_repair 从 actions 移除影响旧 driver | 中 | 保留 action 名识别但 typed 拒绝 APPROVAL_HUMAN_CHANNEL_ONLY（非 UNKNOWN_ACTION），CLI/MCP 均可读错误 |
| 现有 test_agent_ops_core 使用旧 mint 签名 | 中 | 测试随契约更新（secret 参数化），语义断言不弱化 |
| #1742 改持久 worker 线程引入新死锁 | 中 | 单锁 + condition_variable，析构一次 join；100×重启 + GUI 不阻塞测试 |
| TSAN 不可用 | 低 | 如实记录 NOT_EXECUTED，以确定性 100×并发重入测试代替，不伪称 TSAN green |
| AS-016 symlink TOCTOU | 低(P3) | 深层文件系统改造超出本轨白名单收益，记录为已知残余（open-time 复核需改各 surface 打开点） |
| 大构建抢占宿主资源 | 中 | -j2 + flock 全局锁 + 内存监控 |
