# R19-03 RISK_REGISTER

| # | 风险 | 触发源 | 缓解 | 残留 |
|---|---|---|---|---|
| R1 | 共享机资源冲突（多轨并发编译） | 全局 -j2 纪律 | git-common-dir 全局锁 + resource_guard；测试 -j1 | 低 |
| R2 | 与 #1663/#1664 rebase 冲突 | PR 前合并 | 写集零交集（已核）；.gitignore 各自 append 块，冲突人工式逐行解析 | 低 |
| R3 | 测试"伪绿"（恒真断言、墙钟门槛） | #1558/#1621/#1622 | 修断言本身即为任务；每处修复先构造必红反例 | — |
| R4 | produce legend 守卫触发宿主 legend paint 挂死（r18-d 已知） | T4 | 守卫抽纯函数单测，不真调 paint；渲染探针带 timeout 护栏 | 中（#1655 关联） |
| R5 | #1571 锁重构引入新竞态 | T3 | 对齐 commit() 既有锁外模式；部分失败语义单测；重入用例 | 中 |
| R6 | Edit 菜单 gate 收紧误伤可达路径 | T2 | 沿 #1661 19 命令同模式（FeatureFilter 掩码）；r14D 回归全量 | 低 |
| R7 | 教学域越界（#1664） | 所有任务 | src/app/teaching/** 只读；#1565 明确跳过 | — |
| R8 | vendored/弱桩 ABI 误改 | #1638 类 | 只诊断进 CROSS_TRACK，不动 src/gui/antigravity_* | — |
| R9 | 墙钟断言替换后丢失性能回归信号 | T6 | 耗时保留为 INFO 输出；行为断言锚定输出正确性 | 低 |
| R10 | 网络阻断（github 间歇不可达，recon 首次 fetch 超时 135s） | push/PR | 重试已验证可行；gh API 走同域（recon 期间正常）；失败则本地保存 ready-to-submit 内容 | 低 |
