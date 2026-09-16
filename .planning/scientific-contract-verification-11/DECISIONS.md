# DECISIONS — scientific-contract-verification-11

## D-1 基线刷新与 rescope（Phase 0）

Prompt snapshot 假设 #991/#992 仍 open → 实际均已合并；open PR 实为 #1008/#1009；open issues 实为 #1001–#1007。contract census 现状：**115/115 rs: 已全覆盖并被测试强制**，因此 package A 的增量不在 rs: 补记录，而在 (a) 非 rs: 一等算子（io:/otb:/opencv:/cartography: ~25 个）的 census 可见性 + exemption 机制，(b) 三方权威（contract/stamp/sidecar）冲突检测的深化。Oracle-1 的 "first-party operator" 解释为：live registry 全前缀 + cartography 适配器，rs: 保持 full contract，其余前缀给 typed exemption 记录（理由+证据），不伪造 rs:-style contract。

## D-2 determinism 债务的收敛机制：exemption 文件 + census gate，而非大规模盲改

**候选**：(1) 把全部 ~80 个未覆盖算子盲改显式 override —— 违反"逐算子验证、禁止批量拍脑袋"；(2) census 机制：census 模块对每个算子标注 determinism 声明来源（explicit-override / default-fallback / exempted），data/contracts 放白名单式 exemption 表；gate 强制"default-fallback 集合 == ∅ 或逐条有 exemption 且经执行证据核实"。**选 (2)**：保守、可归因、增量收敛；真改的每一条都基于执行证据（跑两遍比对）。rs_operator.h 默认值本身不动（向后兼容，他人 own 的框架文件）。

## D-3 不建立第二 schema/真值：census 是投影不是新权威

determinism 权威仍是 schema stamp + sidecar（ADR 0124/0154 纪律）；census 从 live registry/sidecar/stamp 源**读取并投影**，exemption 表只记录"无 stamp 且允许"这一新事实维度。避免与 10.0 的 D-1（中心 C++ 表）冲突。

## D-4 metamorphic oracle 的不变量选择

按族选适用不变量（每条附独立 oracle，不复用被测实现）：spectral index 的 band-scale/offset 传递性、band-reorder 等价性；warp 恒等变换 byte 语义；zonal 统计平移不变；NoData 传播单调；temporal 时间轴平移下统计量不变；线性滤波叠加分解。全部 bounded logical scale（≤256×256 / ≤16 波段），不引入 wall-clock gate。

## D-5 failure/cancel lane 不依赖 #1009 的新 runtime API

PR #1009 在改 execution runtime（chunk/governor/lease）。本 track failure contract 只消费 master 已有 seam：RSOperator typed error、ErrorCode taxonomy、atomic publication 声明。#1009 合入后由其 own 测试覆盖其行为；本 track gate 只测 contract 层语义。避免强冲突。

## D-6 ladder 的 lane 归属与 skip 语义

`scripts/verification_ladder.py` 本就属于本 track write scope（`scripts/*verification*`）。capability-aware 化保持 schema `exp.verification.ladder.v1` 兼容（additive 字段），缺依赖 → `skipped` + reason，绝不静默 pass。

## D-7 Windows host 特化

load average 不可测（Git Bash）→ 按 GOAL 记 not-executed，-j2 恒定上限；CPU/RSS 用 PowerShell Get-Process 抽样记录。测试统一 `QT_QPA_PLATFORM=offscreen`。

## D-8 planning 文件入库方式

沿用 10.0 D-7：`.gitignore` 追加三行白名单 + `git add -f .planning/scientific-contract-verification-11/*.md`（`.git/info/exclude` 共享 git dir 的 `.planning/` 条目会压过白名单，故用 -f）。

## D-9 P0 host-portability 修复越出 write scope 的裁决（commit 042428ca）

**事实**：`src/workflow/pipeline_run_coordinator.cpp`（c5d4aafe/#991 引入）在 Q_OS_WIN 分支使用 `_O_WRONLY|_O_BINARY`，但 `<fcntl.h>` 只在 `!Q_OS_WIN` 分支 include → 本机 MSVC 14.38/SDK 22621 无法编译该 TU。`origin/master` 现状即坏（`git diff origin/master` 证实非本 track 引入；两次全量构建均停在同一 TU）。**每个测试可执行文件经 sicnu_workflow_runtime PUBLIC sicnu_workflow 传递依赖它 → 整个本地验证平台被阻塞。**

**候选**：(1) 不修，全 track not-executed —— 违反 Oracle-4/6，不可接受；(2) 只在 PR_BODY 记录 —— 同样导致零本地证据；(3) 最小 1 行 include 修复（`<fcntl.h>` 移入 Q_OS_WIN 分支），全额记录。

**选 (3)**：这是与 #993 同类的 host-portability 修复；虽然该文件属于 open PR #1009 的 changed files（read-only 约定），但 1 行 include 的 rebase 冲突风险接近零且可平凡重放。PR_BODY 顶部 P0 通告 + REVIEW_LOG disposition 已登记。
