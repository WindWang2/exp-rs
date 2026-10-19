# R15-C Baseline — 实时事实快照（执行前重新核对，非 Prompt 快照）

- **记录时间（host）:** 2026-10-16（本地时钟，晚间）
- **Worktree:** `C:\Users\wangj.KEVIN\projects\exp-rs-r15-c-20261016`
- **Branch:** `hardening/r15-C-model-runtime`（本轨专用，从下列 BASE_SHA 新建）
- **BASE_SHA (origin/master):** `46791bc65c6f552ebbcf723bb91892736d56f319`
  — 2026-10-16 "Merge pull request #1509 from WindWang2/hardening/r13-k-json-type-safety"
- **主 checkout 状态:** 本地 `master` @ `8a08642760e2e8ffd2c4b965fc4683c01d3e4758`（2026-10-12，落后 origin/master）；主 checkout 有未提交用户改动（untracked 辅助脚本 + ` D resources/icons`），**本轨不触碰主 checkout**。
- **HEAD_SHA（开工）:** 同 BASE_SHA（worktree 干净，`git status --porcelain` 空）

## 远端分支与 worktree 实况

| 项 | 值 |
|---|---|
| remote branches | origin/master、fix/all-open-issues、r12-a/b/c/e/f、r13-g/h/i/j/k/l、r14-A/B/C/E、r15-A（共 18） |
| 本地 worktrees | 主 checkout(master) + exp-rs-r10-algorithms + exp-rs-r15-a-20261016 + **本轨 exp-rs-r15-c-20261016** |
| 本轨分支远端状态 | 尚未 push |

## Open PR（gh pr list --state open，执行时实况）

| # | 状态 | headRefOid | 与本轨所有权重叠 |
|---|---|---|---|
| #1514 hardening/r15-A-science-processing | 非 draft | 7fc4382fe880cc9d59848ef3e6e3d16c0cdf589a | **无重叠**（其 tests 改动为 test_image_fusion/test_obia_segmentation/test_spectral_classification + .planning/r15-a） |
| #1513 hardening/r15-B-geodata-persistence | draft | bddf36abb3afef6f16662f7252614bf0cded816a | 仅 `tests/CMakeLists.txt` 追加块（文件尾部 9818 行后，+46）；`src/data/**`、`.planning/r15-b/**` 不在本轨范围 → **CMakeLists 需短租约、错开插入点** |

注意：Prompt 快照中的 open #1505 / #1512 **均已合并**（见下），不再构成文件锁定。

## 最近 merged PR（gh pr list --state merged，节选）

| # | mergedAt | 主题 | 与本轨关系 |
|---|---|---|---|
| #1509 | 2026-10-16 2:59 | JsonCpp typed safety（#1447/#1488） | 本 BASE 已含；勿重做 |
| #1505 | 2026-10-16 2:58 | security/concurrency：SSRF、temporal cube cache、python worker pool、**GPU sessions**、science broker | 已合并 → `src/runtime/gpu/gpu_plane.cpp` 锁定解除，但按任务约束仍"优先不改"；其修复需回归验证而非重写 |
| #1512 | 2026-10-16 2:35 | Qt/QGIS/MapSpec（D 轨） | 无重叠 |
| #1511 / #1510 | 2026-10-15 | teaching evidence / workbench UI durability | 无重叠 |
| #1508 | 2026-10-15 | **R14-B 模型运行时**（本轨直接前驱） | 其 ORACLES/FINAL_REPORT 在 `.planning/r14-B/`；遗留项：model_catalog 注册期 square-head 禁入缺口（属 framework owner=本轨 C02）、性能实测缺失（C15） |
| #1507 / #1506 | 2026-10-15 | Pi/MCP、科学算法 | 无重叠 |
| #1504 / #1503 / #1502 / #1501 | 2026-10-14/15 | geodata durability、cartography、build/test、全量 issues | #1501 修复了 #1455 的 corpse-session ledger 等 |

## Open Issues 分类（执行时 25 个；逐条按最新 master 代码判定，不轻信 GitHub 状态）

完整逐条矩阵见 `ISSUE_PR_REVIEW_MATRIX.md`。本轨相关结论摘要：

- **REPRODUCED_OPEN / 本轨候选:** #1455 中属本轨所有权的残余（ corpse-session 已 FIXED_ON_MASTER；io/OTB 部分不在本轨可写范围 → 只读记录）；#1460 的模型参数只读协调部分（C14 审计）。
- **FIXED_ON_MASTER（勿重做）:** #1447、#1488（#1509）；#1485（#1505）；#1486（#1505 触及 python worker pool，待编译+代码复核确认）；#1499（R14-B 已验 FIXED，待本轨 configure 编译再确认）。
- **OUT_OF_SCOPE（他轨/CI）:** #1495、#1494、#1491、#1490（CI/build）、#1489（i18n）、#1458（experiment）、#1448/#1449 大部分、#1463-#1468 各 cluster 中非模型运行时部分。

## 宿主环境事实

- OS: Windows，16 逻辑核，物理内存 31.2 GB（执行时空闲 ~8 GB，多 agent 并行 → 严格遵守 -j2，必要时 -j1）。
- 工具链: MSVC 2022 Community 14.38.33130（vcvars64）、Ninja（C:\Qt\Tools\Ninja）、CMake（C:\Qt\Tools\CMake_64\bin）。
- gh CLI 2.89 已认证（WindWang2）。`/teamwork-preview` 与 `/goal-loop` 在本宿主未注册（沿用 R14-B 的 DEGRADATION 记录，用等价多 agent + Oracle-Ledger 结构，reviewer 不写生产代码）。
- 构建树: `build-r15-c`（独立 CMakeCache/ninja，不共用他轨）；共享依赖树沿用仓库惯例 `build-dev/vcpkg_installed`（只读语义依赖，非构建树）。
