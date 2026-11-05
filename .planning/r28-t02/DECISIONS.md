# DECISIONS — R28-T02（无人值守模式的选项与默认记录处）

| # | 决策 | 理由 | 备选（未采） |
|---|---|---|---|
| D-1 | 锁协议实测修正：git 元数据锁=**flock 文件**（~/.cache/exp-rs-agent/git-metadata.lock），不是 mkdir 目录锁 | 实测该路径是 0 字节文件；mkdir 目录锁与其互斥假死（排查 30min）。构建锁=resource_guard.py 双层（host-build 外层 + build dir 内层），与 T01/T03 一致 | 自造第三套锁（拒绝：违反"不新造资源平台"） |
| D-2 | ImportSession 取消及时性采用**有界分步调度**（checkSteps/proceedSteps + requestCancel），不迁移 scan pool，不加 processEvents | UI 的 queued cancel 卡在同步 runPendingItems 之后是 owning-thread 结构问题；分步让事件循环在步间旋转，取消在 ≤1 item 内落地。ImportPreflight::inspect 是 const/线程安全，未来可移 scan pool（T04 消费同一 API） | 任意 processEvents()（拒绝：重入风险）；把 registerSource 移出 owning thread（拒绝：违反 DataManager 契约） |
| D-3 | resume 事实复核=journal 携带 check 时 identity token + 文件存在性检查，输出到 item.message（verifyFacts() 公开，resume 自动跑） | catalog 无内容身份字段；journal 是 hint、catalog 是注册权威；替换/移动必须可见而非静默跳过。schema sicnu.import.session/1 加法扩展（identity_token），旧 journal 诚实降级（无 token→不比对） | 在 catalog 加身份列（拒绝：动身份系统=第二真相源风险，越权） |
| D-4 | 真实 CPU 推理（WP10）用仓库已提交 fixture（tests/data/test_infer_identity.onnx）+ 宿主共享 OpenCV 5 prefix（~/.cache/exp-rs-agent/opencv5-prefix，前轨安装） | rs:infer 的 SICNU_HAS_OPENCV 路径是现存真实 CPU 后端（cv::dnn）；prefix 是本机既有 sanctioned 工件，零系统改动 | pacman 装系统 opencv（拒绝：改用户系统）；下载 onnxruntime（拒绝：无必要+网络依赖） |
| D-5 | ccache 接线 + CCACHE_PREFIX=cmake/raise-compiler-stack.sh | -ffile-prefix-map 已让跨 worktree 缓存命中成立；GCC16 深模板 TU 需要 raise 包装器，经 CCACHE_PREFIX 组合不破坏缓存键 | 裸 launcher=ccache（T03 现状，miss 时有 ICE 风险） |
| D-6 | #1765 GUI 侧（stac_browser_dialog 裸 /vsicurl/）不改，写 HANDOFF 给 T04 | 本轨所有权明确排除 src/app/**；#1810 已划定同一归属 | 顺手改 src/app（拒绝：跨轨抢改） |
| D-7 | RIO 系列 6 issue（#1743/64/65/66/67/803）+ #1705 + #1558 + #1575：master 实码核验为 FIXED_OPEN——本轨复跑既有回归 + 必要补测后在 PR 用 Fixes 清偿 | #1810/#1812 修复已合但 PR body 未用 Fixes 关键字；"修复"必须以本轨实跑复核为准，不盲信 PR 自述 | 直接 Fixes 不复核（拒绝）；重写修复（拒绝：重复实现） |
| D-8 | WP01 先跑基线 13 套件复现取消簇，再写新交错测试 | 既有覆盖已广（watchdog/cancel race/storm/workflow matrix）；无失败清单就写测试=重复造。#1812 CI 日志已过期，本地复现是唯一路径 | 直接按矩阵写 8 个交错用例（推迟到基线证据之后） |
