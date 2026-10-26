# fix(r22-infra): 跨平台构建修复 + 离线打包路径安全 + 安装可重定位 + 测试可信度（#1696 #1697 #1698 #1681 #1682 #1699 #1683）

## Base / Head

- base: `master` @ `911e0b17a3533ca0d79c1b68b000d6052c4c3c61`
- head: `agent/r22-infra-20261025T133318Z`
- 单一 PR；不自动合并；**不等待线上 CI**（按轨道规则，本地证据为准；CI 首跑若 macOS 安装冒烟因 brew @rpath 依赖红，按 PR 内说明处理）。

## 变更清单（按 issue）

- **#1696（P1 三平台编译破损）**：`src/lab/session_state.cpp` 补 `#include <algorithm>`（`std::sort` 用而未含）；`src/verification/availability.h` → `verification_availability.h`（macOS 大小写不敏感 FS 下本地头遮蔽 SDK `<Availability.h>`），更新全部 12 处引用（10 include + 2 注释），全树 grep 零残留。
- **#1697（P1 离线打包 `--version` 路径穿越删除）**：`.sh` 与 `.cmd` 双侧加白名单校验（首字符字母数字，其后仅 `[A-Za-z0-9._-]`；解析后置位+git-describe 回填后再校验）+ `.sh` realpath 包含性断言（同模式回退，删除前拒绝任何逃逸 `--out` 的目标）；`.cmd` 用 PowerShell env-var 探针防注入、fail-closed。新增 `tests/test_r22_infra_bundle_guard.sh`（10 输入穿越弹幕 + 合法版本/带空格 `--out`/symlink `--out`/incremental 腿）并注册 ctest。
- **#1698（P1 安装树不可用）**：根 `CMakeLists.txt` 设置 `CMAKE_INSTALL_RPATH`（Linux `$ORIGIN/../lib;$ORIGIN`，macOS `@loader_path` 对），早于全部 `add_subdirectory`；`sicnu_worker` 增加 install 规则；tier1/tier3 安装冒烟加强（worker 存在性；Linux `LD_LIBRARY_PATH` 显式供给 aqt Qt、macOS `DYLD_FALLBACK_LIBRARY_PATH` 供给 brew、Windows 开 `X_VCPKG_APPLOCAL_DEPS_INSTALL`——项目自有库仍由 RPATH 解析，可重定位证明不受影响）。
- **#1681（P1 clang-tidy 误阻断）**：lint 步骤移到构建之后（生成头已存在）；lane 改用 merge-base（浅克隆大声回退），CI fetch depth 500；header-filter 收窄到**变更头文件集合**（对齐 lane 自己"只 lint 变更文件"的契约，消除未变更旧头阻断他人 diff 的漏洞）。
- **#1682（P2 平台零测试）**：macOS tier3 加守卫测试 + 安装冒烟；Windows tier3 加 `.cmd` 守卫冒烟（钉住 exit 2 + 拒绝文案）+ 安装冒烟；新增 `platform-tests-optin.yml`（workflow_dispatch 手动 macOS 测试子集，复用 tier3 ccache；Windows 需先抽出可复用依赖工作流，见 CROSS_TRACK #5）。
- **#1699（P2 墙钟 flaky）**：9 处断言分型处理——agent_ops 改结构分离（worker 500ms/上限 200ms，async≈0 vs 阻塞≥500ms）；governor/两处 remote 改 hang-guard（500ms/10s，行为由紧随的结构断言承担）；4 处 PERF 预算接入新 `tests/sicnu_test_time_budget.h`（Release ×1.0 不变、sanitizer ×4、`SICNU_TEST_TIME_SCALE` 有界放大；敌意值拒绝）。**已知的检测力取舍**：三处 hang-guard 不再是该窗口的延迟回归 oracle（任何 lane），已在注释与本文档声明。
- **#1683（P3 卫生）**：workflow 顶层 `permissions: contents: read`；qca v2.3.10 / qtkeychain v0.14.0 克隆钉 commit SHA（fail-closed 校验；SHA 经 GitHub API 解析并二次独立核实）；lint merge-base 修复（同 #1681）。

## 关联/关闭 Issues

Closes #1696, #1697, #1698, #1681。Progresses #1682（PR-lane 冒烟 + opt-in 路径；Windows 编译型测试子集需依赖栈重构，记 CROSS_TRACK）、#1699（9 处已修；其余墙钟点未在本批）、#1683（可范围内项已做；根目录遗留脚本/actions SHA pin 需跨轨，记 CROSS_TRACK）。

## RED–GREEN 证据（本地实跑，详见 `.planning/r22-infra/TEST_RESULTS.md`）

- **#1697 RED**：master 脚本 + 预置 `out/sicnu-lab-1.0`（issue 的两步复现）+ `--version '1.0/../../victim'` → canary 被删（`CANARY_DELETED_BY_OLD_SCRIPT`）。**GREEN ×2**（+R1 修复后 ×2、UTF-8 locale ×1）。
- **#1698 RED**：同一安装产物 `patchelf --remove-rpath` 后净环境运行 → `libsicnu_agent.so: cannot open shared object file`（RC 127）；带 RPATH → RC 0。**GREEN ×2**：`env -i … --help` RC=0 ×2；readelf RUNPATH 三处；`env -u LD_LIBRARY_PATH ldd` 全部项目库经 `bin/../lib` 解析；`bin/sicnu_worker` 在树且可执行。
- **#1696**：Release 构建四目标 BUILD_EXIT=0（宿主 GCC 16.2.1 Debug 存在 dwarf2out ICE 环境回归，记录于 BASELINE/RISK；master 未改 TU 同样复现，非本 diff 引入）。
- **#1699**：7 个受影响套件 + verifier 套件全 PASS（agent_ops 12/5、governor 146/10、data_manager 5/1、providers 6/1、asset_catalog 317/8、foundation_scale 952/1、execution_plane 174/18、verifier 608/19）。

## 性能

无产品热路径改动（PERF_BASELINE.md：N/A）。#1699 的 Release 基准值不变（×1.0 实测）。RPATH 为链接期中性（全目标构建不变）。

## 评审闭环

- R1（独立 subagent）：REJECT → 10 项修复（Windows 冒烟钉 exit2+文案、CI 冒烟外部依赖显式化、UNIX+sh 注册、ccache 键复用、symlink 腿、strtod 边界、同模式 containment、注释/说明）→ 全部复验证。见 `REVIEW_R1.md`。
- R2（第二个独立 subagent，不知 R1 结论）：APPROVE（0 P0/P1）→ 采纳 5 项廉价加固（LC_ALL=C、--verify 对齐、semver+说明、DYLD fallback、opt-in permissions/env）。见 `REVIEW_R2.md`。

## R21 差异核对 / 冲突排查

- push 前重查：master 仍 `911e0b17`，open PR 仍 #1673–#1677，无新增交叉。
- 文件交集仅两处：`tests/CMakeLists.txt`（与 #1673/#1674 的 append-only 块互不相交，尾部追加）；`tests/test_data_source_providers.cpp`（与 #1673 的新增用例区域无关，本轨只动 503 行墙钟区）。详见 `PR_COLLISION_MATRIX.md`。

## 未执行项（诚实声明）

- Windows `.cmd` 守卫未在真实 cmd.exe 执行（无 Windows 宿主）：静态结构审查 ×2 轮 + CI Windows 冒烟钉行为。
- macOS/MSVC 编译与全部 CI lane：按轨道规则不等待、不作为完成门；预期观察项——macOS 安装冒烟若 brew @rpath 依赖不足会大声红（缓解与后续动作见 REVIEW_R2 #1）。
- 本机 clang-tidy 完整通过不可得（GCC 专属旗标拒绝 + 非确定 SIGSEGV，宿主工具问题；lane 逻辑已本地验证，CI 为权威）。

## 遗留风险

RISK_REGISTER.md（11 项）：宿主工具链约束、`.cmd` 未实跑、CI 首跑观察项、header-filter 收窄的覆盖取舍（需 lane owner 知悉）、#1699 hang-guard 检测力取舍（本文已声明）等。

## 不等待 CI

本 PR 不阻塞于线上工作流；本地可执行证据如上。CI 结果仅供参考与后续修复。
