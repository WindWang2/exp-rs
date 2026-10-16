# R15-A BASELINE — 实时真相冻结（M0）

> 本文件记录执行开始时**实际探测到**的事实，不是 Prompt 快照的转抄。所有 SHA 由当次命令输出获得。
> 探测时间：2026-10-16 17:00–17:10 (GMT+8)。

## 1. 仓库与分支状态

| 项 | 值 | 证据 |
|---|---|---|
| 源仓库（主 checkout） | `C:/Users/wangj.KEVIN/projects/exp-rs`，分支 `master` | `git branch --show-current` |
| 主 checkout HEAD | `8a08642760e2e8ffd2c4b965fc4683c01d3e4758` | `git rev-parse HEAD` |
| 主 checkout 工作区 | 1 个跟踪文件删除（`resources/icons`）+ 约 90 个未跟踪临时文件（历史会话遗留，**不清除**） | `git status --short` |
| 主 checkout vs origin/master | behind 239 / ahead 0 | `git rev-list --left-right --count` |
| **origin/master（BASE_SHA）** | **`46791bc65c6f552ebbcf723bb91892736d56f319`**（2026-10-16 10:59:21 +0800，Merge PR #1509） | `git rev-parse origin/master` + `git log -1` |
| 本轨 worktree | `C:/Users/wangj.KEVIN/projects/exp-rs-r15-a-20261016` | `git worktree add -b hardening/r15-A-science-processing <path> 46791bc6` |
| 本轨分支 | `hardening/r15-A-science-processing` @ `46791bc6`（创建时无同名本地/远程分支） | worktree add 输出 |

**与 Prompt 快照（2026-10-16 编写时）的差异：origin/master 已从 `8b856721` 前进到 `46791bc6`（+1 merge：#1509）。**

## 2. PR 矩阵（探测时点：零 open PR）

Prompt 快照称 3 个 open PR（#1505/#1509/#1512）——**实际全部已合并**：

| PR | 标题 | 状态 | 合并时间 (UTC) | 与本轨关系 |
|---|---|---|---|---|
| #1505 | fix(security,concurrency): SSRF/temporal cube/python pool/GPU/broker | **MERGED** | 2026-10-16 02:58:58 | 含 `src/core/temporal_cube.cpp`（只读锁的文件已合并；本轨仍不写该文件） |
| #1509 | fix(json): JsonCpp 类型安全（#1447,#1488） | **MERGED** | 2026-10-16 02:59:21 | 含 `src/operators/rs/rs_spectral_index_operator.cpp`（+4/-4，**永久避让锁解除**，需先读 diff 再审计） |
| #1512 | hardening(r14-d): Qt/QGIS/MapSpec | **MERGED** | 2026-10-16 02:35:45 | 无文件重叠（E/D 轨） |

**本轨创建时无任何 open PR，不存在与其他 Agent 的正面文件冲突。**

### 已合并 PR 的文件清单（dedup 依据，`gh pr view --json files`）

- **#1506（R14-A，本轨直接前身）**：`src/analysis/classification/rs_classifier_kmeans.cpp`、`src/operators/rs/{rs_continuum_removal,rs_obia_common,rs_obia_features,rs_obia_label,rs_radiometric_qa,rs_sam_classify,rs_segment_stats,rs_temporal_index}_operator.cpp`、`src/processing/algorithms/{image_fusion,spectral_local_rx}.cpp` + 7 个测试文件。→ **这些修复禁止 revert/重写**，其 FINAL_REPORT 残留项是本轨候选（见 ORACLES.md O1–O3）。
- **#1509**：另改 `src/agent_ops/*`、`src/science_context/*`、`src/verification/canonical_json.cpp`（均非本轨可写范围）。
- **#1505**：另改 `src/geospatial/remote/*`、`src/python/isolated/*`、`src/runtime/gpu/*`、`src/science_context/broker.*`（均非本轨可写范围）。

## 3. Issue 矩阵（25 open，逐条分类见 ISSUE_PR_REVIEW_MATRIX.md）

与本轨可能相关的 open issues：**#1467**（算法侧子项已有着落，见矩阵）、**#1454**（仅科学算子回调/计算副作用侧）、**#1488/#1447**（#1509 已合并修复，待编译验证）。
其余 22 个经文件清单比对归 CI/教学/i18n/geospatial/runtime/agent 等轨，OUT_OF_SCOPE。

## 4. 分支与 worktree 矩阵

| Worktree | 分支 | HEAD | 状态 |
|---|---|---|---|
| `projects/exp-rs` | `master`（本地） | `8a08642` | **只读**（含未提交用户变更，禁 reset/clean） |
| `projects/exp-rs-r10-algorithms` | `hardening/r10-algorithm-processing-quality` | `8616585e` | **只读**（未合入 master 的本地分支，禁碰） |
| `projects/exp-rs-r15-a-20261016` | `hardening/r15-A-science-processing` | `46791bc6` | **本轨** |

远程分支 18 个（origin/master、fix/all-open-issues、r12–r14 各 hardening 分支）。无 `r15-A` 同名远程分支。

## 5. 构建/测试环境事实

- Windows、MSVC（VS 2022 Community）、Ninja、CMake（`C:\Qt\Tools\CMake_64`）、Qt 6.8.0 msvc2022_64、vcpkg toolchain（`C:/deps/vcpkg`）、Catch2 源码（`C:/deps/catch2-src`）——配方来自仓库既有 `configure_dev.cmd`/`build_dev.cmd`。
- CMake preset `dev-default`（Debug + ENABLE_TESTS=ON）；本轨使用独立构建树 `build-r15-a`（不共用任何其他 Agent 的 CMakeCache/ninja/生成目录）。
- 探测时点无活跃重型构建（`build-dev/.ninja_log` 停留 2026-10-09；R10 worktree 无构建树）。
- 磁盘：C: 可用 968G/1.9T。
- 9 个优先回归目标全部在 `tests/CMakeLists.txt` 注册（行号已记录：test_radiometric_qa:1551、test_nodata_utils:1604、test_spectral_classification:2358、test_image_fusion:4593、test_classifier_kmeans:5852、test_obia_operators:6404、test_rs_operators:6592、test_temporal_algorithms:6940、test_resampler:12028）。

## 6. 已知环境坑（来自 build-env skill 记忆，本轮继续防范）

- Git Bash + wsl 路径转换会把 `rev:path` 变成 `rev;path` → 用 `MSYS_NO_PATHCONV=1`。
- `tasklist`/`Get-Process` 被本机安全策略按 LOLBin 拦截 → 用构建日志时间戳推断活跃构建。
- git push 凭据弹窗被沙箱拦截 → push 时用 `GIT_ASKPASS` + `GIT_TERMINAL_PROMPT=0` + `GCM_INTERACTIVE=never`。
- vcvars64.bat 阻断 cmd → 构建命令封装为 `.cmd` 脚本执行。
