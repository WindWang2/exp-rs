# R18-E PERFORMANCE

基线机器: Linux 7.2.9-1-cachyos, 40C/62G(构建 -j2), 无 ccache。（所有数字实测，命令可复现。）

| 基准 | 输入 | 命令 | 次数 | 结果 | 峰值RSS |
|------|------|------|------|------|---------|
| R18-E 收尾: help 搜索性能 gate（5k 语料，含 build/query/topK 全套） | tests/test_r16_E_help_search_perf（E09 改动后二进制） | 直接运行 ×3 | 3 次全 PASS；15787ms / 16369ms / 15857ms（冷启动含进程装载；同宿主 -j1 环境无其它负载时测量，两次间隔重跑） | 进程级（套件含 RSS 断言未触发） |
| E09 ErrorDiagnosticsBridge 缓存 | 结构性改动：每次 helpIdFromMessage/helpIdForCode 查询从「全 registry 扫描重建表」变为「互斥锁下惰性建一次 + 哈希查询」 | — | **未测量加速比（D-14：不声称未测数字）**；效果为算法级（O(registry)/次 → 一次构建），正确性由 test_help_core/help_contracts/search_perf 全绿背书；专用微基准 NOT_EXECUTED（需独立 harness，后续维护建议） | — |
