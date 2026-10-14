## 第二轮审查（R2）— 结论：✅ 建议合并，合并前处理 4 项小事

> **独立性声明**：本轮由实现者本人执行，**不是**未参与实现的第三方 reviewer。R12-G 要求的两轮独立 review 中，R1 由独立 agent（general-purpose-4）完成；R2 因子代理配额 429 未执行，后由主 Agent 自行补做。因此本结论的定位是「实现者自查 + 独立复现验证」，**合并前仍建议由一名未参与实现的 reviewer 复核**。
>
> 审查方式：不采信 PR 正文的任何声明，全部用独立构造的夹具/命令复现。

### 已独立证实的声明（15/15）

| 声明 | 独立复现结果 |
|---|---|
| **junction 逃逸（P1 安全）** | 构造精确攻击夹具：真 junction `jlink → 包外目录`，manifest 以 `jlink/secret.txt` 登记且 size/sha256 均取自**包外真实文件**。HEAD 版 → `BUNDLE VERIFY PASS exit=0`；本 PR → `FAIL exit=1` + 两条 typed 诊断。**安全缺陷真实存在且已修复** |
| pwsh7 `Int64` 误判 | 同一正确包：修复前 5.1=PASS / pwsh7=FAIL；修复后均 PASS |
| `--jobs 8` 被拒 | 亲跑 exit=2；`SICNU_BUILD_JOBS_CAP=99 --jobs 8` 仍正确放行 |
| `REQUIRE_GUARD` 真实拒绝 | exit 1 且 payload 从未运行（R1-01 闭合） |
| netcdf configure | exit=0，`NETCDF_INCLUDE_DIR` 入缓存（改动前缓存中只有 `NETCDF_LIBRARY`） |
| `13/13 green` 是硬编码谎言 | `_green_summary` 用 4 种真实 ctest 日志形状测试：全过→`13/13`、1 败→`12/13`、空匹配/无汇总行→`count unproven`（诚实降级） |
| OTB 复制 0 文件仍报成功 | 空源→exit 1 + 明确 ERROR；有内容源→exit 0 + 真实计数 |
| conformance 18/1 → 18/0 | 亲跑 `18 checks, 0 failed` |
| `selftest: ALL PASS` | 亲跑通过 |
| **不会打红 CI** | Tier1/Tier2（Linux，`ENABLE_TESTS=ON`）装 `libgdal-dev`，其在 Ubuntu 24.04 的依赖列表**明确包含 `libnetcdf-dev`**（launchpad/packages.ubuntu.com 实证）→ 库与头都在；Tier3 Windows/macOS 均为 `ENABLE_TESTS=OFF` → CMake 块不可达 |
| `--no-tests=error` 无误报 | 读 item 循环：先 `find_binary`，二进制缺失→`not-built` 且**不调 ctest**；只有"二进制在、注册丢了"才触发 |
| `executed==0` 不误伤 `--resume` | resumed-pass 计入 `passed` → executed>0 |
| `note`/`fail` 不进 argv | 两者均写 stderr（我原本怀疑的缺陷不存在） |
| rebase 干净 | `merge-tree` 无冲突；master 对 `tests/CMakeLists.txt` 的 48 行在 1615/4131/11071，本 PR 在 424/579/617，**零重叠** |

### 发现的问题

| ID | 级别 | 问题 |
|---|---|---|
| F1 | 🟡 P3 | `docs/development/README.md:19` 的示例正是 `resource_guard.py ... --parallel 8`，新守卫会**拒绝**它（exit 2）。该示例其实从未可用（旧版 argparse choices 直接报错），且文档声称 parallelism 被 "clamped"，对显式 `--parallel` 现在是"拒绝"而非"钳制"，描述双重失真 |
| F2 | 🟡 P3 | `SICNU_BUILD_GUARD_WAIT` / `SICNU_BUILD_REQUIRE_GUARD` / `SICNU_BUILD_NO_GUARD` 三个新旋钮在**任何被跟踪的 .md 中都没有说明** |
| F3 | 🟠 P2 | `.planning/*` 被 gitignore，BASELINE / ORACLE_LEDGER / 三份审计 / FINAL_REPORT / 验收矩阵**全部不在 PR 里**，其他 reviewer 只能看到摘要转述 |
| F4 | 🟡 P3 | 租约只包住 `scripts/build.sh` 的 payload；`verification_ladder.py`、`perf_observatory_verify.py`、`build-examples.sh` 与直接 `cmake`/`ctest` 均绕过，"一目录一写者"对这些路径未成立 |
| F5 | 🟡 P3 | PR 落后 master 4 commits，合并前应 rebase |
| F8 | 🟡 P3 | PS 侧两个修复（Int64、`Get-RelPath` 归一化）**无自动化覆盖**——conformance 的 PS lane 在无 pwsh 的机器上是 `not-executed`，回归无人守护 |
| F6/F7/F9 | ℹ️ | 1/16 flaky 为既有且 CI 爆破半径为零；`--no-tests=error` 会翻出潜在注册漂移（设计意图）；`_unsafe_bundle_rel` 有一个死代码子句（无害） |

### 合并前最小清单

1. rebase 到最新 master（F5）
2. 修 F1 文档示例为 `--parallel 2`
3. 补 F2 三个环境变量说明
4. 决定 F3 如何处理（证据目录破例入版本控制，或在 PR 附关键原始输出）
5. 由一名**未参与实现**的 reviewer 复核（独立性缺口）

### 反作弊自查

未删除/禁用任何测试；未放宽超时；未改 golden；未把 NOT_RUN 记为 PASS；所有新增判据方向都是**收紧**。唯一"变绿"（conformance 那条既有 red）系真实缺陷修复所致。
