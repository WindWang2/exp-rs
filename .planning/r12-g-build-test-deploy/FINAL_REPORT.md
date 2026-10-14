# R12-G FINAL_REPORT — 构建 / 本地验证 / 离线交付

- 分支：`hardening/r12-g-build-test-offline`　worktree：`exp-rs-r12-g-build-test`
- 基线：`origin/master = 28abef358217520dfbb802efdfdb11a2bf850465`
- 变更：**9 文件 / +357 −29**（全部落在 `scripts/**`、`packaging/**`、`tests/CMakeLists.txt`，**未触碰 `src/**`**）

## 1. 验收矩阵（逐条真实状态）

| WP | 状态 | 证据（真实命令/输出） |
|---|---|---|
| G01 构建拓扑普查 | ✅ PASS | BASELINE.md（7 worktree / 2 open PR / 20 issues 三层核对） |
| G02 单进程测试退出资源 | ⚠️ PARTIAL | 仅审计；`_exit` 属其他方向所有权；复核后更正：19/20 不适用 #1468，仅 2 个 TU 真泄漏 |
| G03 HTTP/COG 夹具可取消 | ⚠️ PARTIAL | 审计出 G03-1~6（含 close-then-wake 死锁 10061、0 处 SO_SNDTIMEO），未修（属实现方向） |
| G04 CTest 注册漂移 | ⚠️ PARTIAL | G04-1 等已定位；`tests/CMakeLists.txt` 仅做 netcdf 短租改动，未做注册面重构 |
| G05 可选依赖精确失败 | ✅ PASS | `find_library`+`find_path` 成对；**configure6/7 实测 `cmake_exit=0` 且 `NETCDF_INCLUDE_DIR` 入缓存**；R1-03 漏的第 4 个 TU（`test_io_roundtrip_matrix`）已补齐 |
| G06 跨平台构建锁收敛 | ✅ PASS | `build.sh` 接入跨进程租约；`selftest: ALL PASS`；`SICNU_BUILD_REQUIRE_GUARD` 由 R1-01 修复后真实拒绝（exit 1，payload 未跑） |
| G07 离线教学 profile | ⚠️ PARTIAL | `check_presets.py ALL PASS`；`offline-lab` 预设存在，但**全量离线构建未跑** → NOT_RUN |
| G08 Windows 打包 | ⚠️ PARTIAL | G08-1(=G09-2) 已修；G08-2 `RUN.cmd` 未修（cmd 不可执行、无法验证） |
| G09 离线包清单/哈希安全 | ✅ PASS | G09-1 junction 逃逸（原 PASS→现 FAIL）、G09-2 pwsh7 误判 FAIL→PASS、G09-5 路径规范化；**附加修掉 2 个审计未报的缺陷**；conformance **18/18** |
| G10 一键实验室脚本 | ❌ NOT_RUN | 仅审计（G10-1~6 均为 P2，且 .cmd 在本机无法执行验证） |
| G11 临时目录隔离 | ❌ NOT_FIXED | 审计出 G11-1（非 pid 隔离的 scratch root），属其他方向所有权 |
| G12 分层验证阶梯 | ✅ PASS | G12-1 `executed_items==0` 判负；G12-2 `--no-tests=error` + 字符串双判据 |
| G13 安装/卸载一致性 | ⚠️ PARTIAL | G13-2 已修（0 文件复制不再谎报成功，RED/GREEN×2 全过）；**G13-1 AppImage 校验和 PENDING 需联网 → BLOCKED** |
| G14 性能门禁 | ✅ PASS | `perf_observatory_verify.py` 假绿已修：旧版 `PASS O1 run 1: 13/13 green`（0 测试），新版 `FAIL O1`；O3 空集恒等已加证据下限 |
| G15 反作弊 + 独立审查 | ⚠️ PARTIAL | 反作弊红线已在 G12/G14 落地；**独立两轮审查：R1 完成（3 P1 已修）；R2 已补做但由实现者本人执行 → 独立性未满足** |

## 2. 验证阶梯（L0–L4）

| 层 | 内容 | 结果 |
|---|---|---|
| L0 静态 | `bash -n scripts/build.sh`；`py_compile` 三个 py | ✅ OK |
| L1 hermetic | `scripts/build.sh selftest`、`check_presets.py` | ✅ ALL PASS（见 §4 待复核项） |
| L2 configure | 真实 `cmake -S . -B build-dev` ×2 | ✅ `exit=0`，`NETCDF_INCLUDE_DIR` 命中 vcpkg include |
| L3 编译 | `cmake --build build-dev --target test_paths` | ✅ `test_paths.exe` 2,393,600 B 产出（日志 02:56） |
| L4 ctest | `ctest -R test_paths` | ❌ BLOCKED：`0xc0000135`（DLL 未找到）。已试 3 种 PATH 补全（Qt bin / vcpkg debug bin / VC DebugCRT）均无效 |

## 3. 未完成 / 阻塞（如实列出）

1. **独立 R2 审查的独立性未满足** —— 子代理配额 429（重置 2026-10-14 23:00 UTC+8）导致 R2 未能由未参与实现的 reviewer 执行，后由主 Agent 自行补做（见 `REVIEW_R2.md`）。该轮以**独立复现**方式验证了 15 项声明（含 junction 逃逸的 HEAD/PR A/B 对照），但按 R12-G 规程**不能替代真正的独立 R2**；R1 已由 general-purpose-4 完成并给出 3 个 P1，**均已修复**。合并前仍需一名未参与实现的 reviewer 复核。
2. **L4 ctest 全红（环境）** —— `0xc0000135`，与本次改动无关（改动不涉及运行时 DLL 解析），3 次修复尝试失败。
3. **G13-1 AppImage 校验和 PENDING** —— 修复需联网抓取固定 tag 产物做 sha256，本机离线，只给步骤不代做。
4. **G10 全部 P2 未修** —— .cmd/.bat 在本机无法执行（cmd.exe 被安全策略阻断），改了无法验证，按纪律不动。
5. **G13-2 的边界保留** —— OTB 源目录**不存在**时仍只 WARNING 不失败（既有契约），仅修"存在但复制 0 个文件却报成功"。
6. **`build-dev.agent-build.lock` 未纳入 .gitignore** —— R1-04 指出；本次未修（`.gitignore` 属共享文件）。

## 4. 本轮红灯的最终处置（已闭环）

| 红灯 | 判定 | 处置 |
|---|---|---|
| `selftest: busy build dir must exit 75, got 1` | **我的改动暴露的既有竞态**（R1-07 已预告）：持锁 payload 只 sleep 8s，探针却只等锁文件出现，机器一慢持锁者就先退出 → 守卫拿到锁并去跑 payload | 改为等待 **payload 自己写的 marker** + 30s 持锁 + 30s 轮询预算；同时把失败时的 wrapper 输出打印出来（此前"拒绝/缺解释器/夹具坏"三种情况长得一样）→ 复跑 `selftest: ALL PASS` |
| `test_resource_guard` 1/16（`0xC0000005`） | **既有 flaky**，非本次引入：用 `git show HEAD:scripts/dev/resource_guard.py` 同条件对照，**HEAD 基线同样 16 跑 1 失败** | 未修（避免污染回归基线），如实记录 |

## 4b. 交付

- 5 个主题提交：`615a59392`（构建/租约 + R1 P1）、`68e4e3f20`（零证据不报绿）、`19c53455d`（netcdf 头探测 + R1-03）、`55a1a03d6`（离线交付不说谎）、`b4ecc96fa`（R2 文档发现 F1/F2）
- 已 `git push -u origin`，PR：**https://github.com/WindWang2/exp-rs/pull/1498**（OPEN / 非 Draft / base=master）
- **R2 审查发现的 4 个合并前事项已全部处置**：

| 事项 | 处置 |
|---|---|
| F5 版本基线 | 已**合并** `origin/master`（28 commits）。用合并而非 rebase：仓库自身规则 3 禁止 force-push。合并前核验唯一重叠文件 `tests/CMakeLists.txt` 的 hunk 零重叠，合并干净；合并后一致性回归复跑 **18 checks, 0 failed** |
| F1 文档示例 | **我的原判有误，已留痕更正**：示例 `-- cmake --build build-dev --parallel 8` 中的 `--parallel` 属 cmake payload，守卫将其**钳制**为 2 后放行（桩 payload 收到 `--parallel 2`，exit 0），示例一直可用。真正没写清的只是"守卫自身 `--parallel` 超 `--max-parallel` 是拒绝、payload 的是钳制"这两套语义未区分 → 已在 `docs/development/README.md` 步骤 4 区分 |
| F2 开关零文档 | 已在 `docs/development/README.md` 新增规则 5（四个 `SICNU_BUILD_*` 开关 + 二者同设即矛盾退 1）、`scripts/dev/README.md` 表格行点名 `--max-parallel` 提额路径。三条声明均实测相符（`--parallel 8`→退 2、`--parallel 0`→退 2、`--max-parallel 4 --parallel 4`→payload 被钳到 4） |
| F3 证据不可见 | 已按 master 上的 **R12-E 先例**（`9c3272ed8`，同样未改 `.gitignore`）用 `git add -f` 纳入：13 份 markdown 证据 + 4 个可跑 oracle 随 PR 提交 |

- **仍未满足的一项（用户已知情并授权合并，留痕备查）**：R12-G 要求两轮均由**未参与实现**的 reviewer 执行。R1 满足（general-purpose-4，3 个 P1 已修）；**R2 因子代理配额 429 改由主 Agent 自行补做**，方法是完全独立的复现（不采信 PR 正文任何声明、全部自建夹具），但**身份上不满足规程**。已在 `REVIEW_R2.md`、本节与 PR 评论三处显式声明。

## 5. 变更文件

| 文件 | 改动 |
|---|---|
| `scripts/build.sh` | 租约接入 + R1-01/02/06 修复（进程替换改为临时文件+主 shell 决策；`--jobs` 过 cap 校验；开关互斥冲突；`--max-parallel` 传递） |
| `scripts/dev/resource_guard.py` | `--parallel` 放宽为 int + `<1` 拒绝；新增 `--max-parallel` 护栏 |
| `scripts/verification_ladder.py` | `--no-tests=error`（带能力探测）+ "No tests were found" 判负 + `executed_items==0` 判负 |
| `tests/CMakeLists.txt` | netcdf `find_path` 成对探测 + 4 个 TU 的 include dir（含 R1-03 补的 `test_io_roundtrip_matrix`） |
| `scripts/verify_bundle_manifest.py` | junction/重解析点逃逸检测、realpath containment、路径规范化、rooted 路径判 unsafe |
| `scripts/bundle_manifest.ps1` | `size_ceiling_mb` 接受 Int64；`Get-RelPath` 路径归一 |
| `packaging/bundle/VERIFY.ps1` | 同上两项 + `$Root` 归一（修掉短名路径下的未处理异常） |
| `packaging/build-appimage.sh` | OTB 复制计数，0 文件即失败；GDAL 消息打印真实计数 |
| `scripts/bench/perf_observatory_verify.py` | `run_ctest` 空匹配判负；O1 打印真实计数；O3 加证据下限 |
| `docs/development/README.md`、`scripts/dev/README.md` | R2 发现 F1/F2：区分两种 `--parallel` 语义；文档化四个 `SICNU_BUILD_*` 开关 |
| `.planning/r12-g-build-test-deploy/`（16 个文件） | 证据链（R2 发现 F3）：13 份 markdown + 4 个可跑 oracle，按 R12-E 先例 `git add -f` 纳入 |

## 6. 线上 CI 状态与合并决定（合并前归因）

合并前最后一次线上运行 `38544560480`（head = `6d335eaf0`）三个 Tier 失败。**逐条归因后判定：全部为 master 既有失败，与本 PR 改动面无关。**

| Job | 失败步骤 | 根因 | 归属 |
|---|---|---|---|
| Tier 1 - Fast Required (Linux GCC) | Compile Core & Test Targets | `tests/test_science_context_broker.cpp:941` `std::atomic<bool>` 不完整类型、`std::thread` 非 `std::` 成员 —— 缺 `#include <thread>` / `#include <atomic>` | **R8 / PR #1453**（`246408bed`）引入，非本 PR |
| Tier 2 - Full Integration & Sanitizers | Build Sanitizer Test Targets | 同上（同一 TU 的 sanitizer 变体） | 同上 |
| Tier 3 - macOS Clang Configure & Build Seam | Verify macOS Build | Xcode 26.6 SDK 头 `MacOSX.sdk/usr/include/_stdio.h` / `_time.h` 报 "expected function body after function declarator" / "__OSX_AVAILABLE" 重定义 —— 工具链/SDK 级 | **环境级**，非本 PR |

**判定依据（可复核）**：
1. `git diff origin/master...HEAD --name-only` 的 30 个文件中**不含** `tests/test_science_context_broker.cpp`；`git diff origin/master...HEAD -- tests/test_science_context_broker.cpp` 输出为空（与 master 逐字节一致）。
2. master 自身的运行 `38515287149`（head = `892295685`，即本分支 `3a418ab60` 合并进来的**同一个** master 提交）呈现**完全相同的三处失败**。
3. `gh api repos/WindWang2/exp-rs/branches/master/protection` 返回 `Branch not protected` —— master 无必需状态检查，`mergeStateStatus: UNSTABLE` 不阻塞合并。

**决定**：按用户显式授权（"嗯，处理后合并"）执行合并，合并方式 `--merge`（与仓库既有 #1497 等一致的 merge-commit 风格），**不删除分支**（保留可追溯）。合并前的 4 项 R2 事项已全部处置（见 §4b）。
