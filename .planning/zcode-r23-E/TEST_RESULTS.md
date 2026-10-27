# TEST_RESULTS — R23-E（运行证据，持续更新）

环境：WSL archlinux GCC 16.2.1 / CMake 4.4.3 / Ninja -j2 / Release / SICNU_EMBED_PYTHON=OFF（build-E）；
Windows 侧 Git Bash + cmd.exe（真机）+ anaconda python（YAML 解析）。基线 SHA `ad98a7a70`。

## T1 · WP3 merge-integrity gate（scripts/check_merge_registration_integrity.py）

模拟 sibling 合并验证（临时克隆 /home/kevin/r23e-wp3-scratch，事后删除；master 不受影响）：

| 场景 | 期望 | 实测 exit |
|---|---|---|
| live 工作树（HEAD=base=ad98a7a70） | 0（1038 目标双侧一致） | **0** |
| 兄弟轨 R 落 master + 本轨合并保并集 | 0，added=1 lost=0 | **0**（1040=1038+R+本轨 demo） |
| LOSSY 合并丢 R | 1，指出 `test_sibling_r_demo` lost | **1** |
| 同上 + `--allow-removed test_sibling_r_demo`（有意移除白名单） | 0 | **0** |
| 冲突标记残留（<<<<<<< HEAD/=======/>>>>>>>） | 1，3 处 marker | **1** |
| 同一 target 注册两次 | 1，duplicate | **1** |
| static 降级（--no-git，标记在工作树） | 1（磁盘扫描可见） | **1** |

ctest 注册：`r23_e_merge_registration_integrity`（Python_EXECUTABLE 门控，TIMEOUT 120）。

## T2 · WP2 python worker 兼容层（tests/test_r23_e_python_worker_typed.py）

真 daemon（AF_UNIX socket，python3.14 + numpy 2.5.4 用户目录 wheel）：**Ran 5 tests — OK**（多次运行）。

突变 RED（本地 patch 后还原，`git status` 零残留）：
- 回退 #1609 callback-pop（unload 后 callbacks.pop 删除）→ **FAILED (failures=1)**（marker 文件判别器命中）
- 回退 #1608 _json_safe（identity）→ **FAILED (failures=2)**（np 标量/ndarray 序列化两测失败）
- 还原后 → OK

GREEN×2：初始运行 OK + 突变脚本首尾两次 OK（三次独立进程运行全绿）。
环境注记：WSL 系统 python3.14 无 pip/无 numpy——numpy 2.5.4 cp314 manylinux wheel 由 PyPI 下载解包至 `~/r23e-pysite`（PYTHONPATH 注入，仅测试宿主环境，不入库）。ctest 门控：numpy/AF_UNIX 缺失时 loud skip。

## T3 · WP4 Windows 真机 .cmd 守卫腿（#1708 遗留 NOT_EXECUTED 补证）

真 cmd.exe（Windows 10.0.26200，经 MSYS_NO_PATHCONV=1 + per-case driver 批处理）：

| 输入 --version | 期望 | 实测 |
|---|---|---|
| `1.0/../../victim` | exit 2 + refusing | **exit 2 + "refusing unsafe --version"** |
| `..` | 同上 | **✓** |
| `1.0;del x` | 同上 | **✓** |
| `1.0 & echo PWNED` | 同上 | **✓** |
| `1.0 beta`（空格） | 同上 | **✓** |
| `1.0.2_beta-3`（合法） | 过版本门 → `--build-dir is required` | **✓（exit 2，消息正确）** |
| canary（victim/keepme.txt） | 存活 | **CANARY_ALIVE** |

cmd.exe 突变腿：sed 生成的突变体自身损坏（CRLF 处理），记 NOT_COMPLETED；判别力证据由 #1708 的 sh 侧 RED（canary 被删实锤）与本轨 WP2 python 突变承担。

## T4 · WP7 资源上限/脚本卫生扫描

- `scripts/check_presets.py` → **EXIT=0**（JOBS_CAP=2 全绿）。
- `-j5+/-jN/--parallel 5+/-j$(nproc)` 全仓工程面扫描：仅命中守卫代码/自测/文档示例（scripts/build.sh 自检、resource_guard 及其测试），**零违规**。
- 根目录遗留 .cmd 工程脚本 14 个（build.cmd/build_r6.cmd/…/test_wb7.cmd）：盘点记录于 CROSS_TRACK_FINDINGS #4（不删除——E09 规则禁止随意删未标记文件，且并行 epic 期间他人可能引用）。
- CI YAML 离线解析（Windows anaconda PyYAML）：ci.yml（5 jobs：tier1-fast-required、tier2-full-integration、tier3-windows-deps、tier3-platform-windows、tier3-platform-macos）与 platform-tests-optin.yml（macos-tests）**解析 OK**；不等待线上运行。

## T5 · 定向构建（D7 重规划后）

- 配置 EXIT=0（2026-10-26T17:03:20Z）；全量 9525 边实测 ~7 边/分（-j2）→ 终止（22h 不可行）。
- 定向构建（exprs_plugin_host_worker + isolation_plugin + 两个新测试目标）1234 边：首跑 EXIT 1 —— 两个**测试侧**编译错误（Catch2 断言内 `||` 禁止分解；缺 `sicnu::plugins::` using），修复后 EXIT 0，二进制落 build-E/ 根（sicnu_add_sdk_test 约定）与 build-E/tests/。
- ctest 发现机制事实：PRE_TEST 发现 + D15 TEST_PREFIX 约定 —— 新测试必须带 `TEST_PREFIX "<target>::"` 才能被 `ctest -R` 选中（英文用例名不可选）；首次 ctest 求值时生成 `_tests.cmake`。

## T6 · WP1 plugin host 类型门（tests/test_r23_e_plugin_typed_gates.cpp）

- `ctest -R r23_e`（7 测全绿总时长 2.58s）中含两用例：load 面 84 断言、execution 面 34 断言，全 PASS。
- 突变 RED（本地 patch→重编→跑→还原，`git status` 零残留）：
  - 移除 services 对象门（#1551）→ **1 failed**；
  - 移除 peerString 类型检查（#1592）→ **1 failed**；
  - 还原 → load 84/exec 34 全 PASS。
- GREEN×2：ctest 7/7 一次 + 突变脚本首尾两次独立进程运行。

## T7 · WP5 native terminal（tests/test_r23_e_native_terminal.cpp）

- 3 用例全 PASS（0.04/0.04/0.02s）：终端选择 + **cwd=目标目录** + **零 --working-directory 参数**（探针三行 $0/$@/pwd）；KDE 选择器腿；capabilities 位。
- 宿主 archlinux（非 debian）→ xterm/konsole 腿实测生效；debian 分支腿以分支顺序确定性处理。

## T8 · WP4 Linux 实机安装/可迁移冒烟（当前 master + 本轨，12/12 PASS）

构建链：定向 1234 边（WP1/WP5 目标）→ cli+worker 1886 边 → install 迭代补建（sicnu_core、layer_tree_plugin、processing_plugin、pybind _sicnu_operators、主程序 sicnu_geo_rs）→ `cmake --install` **INSTALL_OK**。

| 断言 | 结果 |
|---|---|
| bin/{sicnu_geo_rs, sicnu_geo_rs_cli, sicnu_worker} 在树 | PASS ×3（#1698 worker 回归成立） |
| bin/resources/translations/sicnu_zh_CN.qm 在树 | PASS（**#1623 回归成立**：.qm 落在可执行文件旁） |
| 净环境（env -i）cli --help / --version | PASS ×2（RC=0） |
| worker --help | PASS（RC=2 + typed JSON `"op":"error"`——按 kExitUsage 契约） |
| GUI 二进制独立启动 | PASS（offscreen 下进入事件循环，60s cap RC=124；GUI 无 --help 契约，help/version 归 CLI——诚实标注） |
| **relocate**（整树 mv 后 cli --help/--version） | PASS ×2 |
| RPATH | 三二进制均 `[$ORIGIN/../lib:$ORIGIN]`（readelf RUNPATH 留证）；PASS |
| ldd（无 LD_LIBRARY_PATH）零 not-found | PASS |
| r22 bundle guard 复跑 | exit 0（前述） |

**SUMMARY: PASS=12 FAIL=0**。GUI --help 在裸 env 下挂起进事件循环（Vulkan check + 事件循环）→ 以"启动成功"为 GUI 断言、CLI 为 help/version 契约持有者，如实记录。

## T9 · R1/R2 评审轮证据摘要

- R1（APPROVE-WITH-FIXES）：8 项（F1 P1 .md 排除无效等）→ 修复 commit b22f7ddd，验证见 REVIEW_R1.md 附录（.md 净/非 .md 检出、add_test 丢失检出、--require-refs fail-closed、waitForProbe 完整性、junk/长括号、分支感知去重三用例、两模式面一致）。
- R2（APPROVE-WITH-FIXES）：独立复现 R1 全部突变主张 + 确认 R1 修复真实闭合 + 新发现（N1 P2 引号 add_test 等）→ 修复 commit cf3bfd04，验证见 REVIEW_R2.md 附录（'Q Loss Probe' 丢失检出、.MD 排除、单行 if/共存边界、exit 2、temp 零泄漏、7/7 复绿 3.10s）。
- 两轮 reviewer 均为独立 fresh subagent（互不继承、不采信对方结论），全程只在 scratch 克隆做破坏性实验，工作树保持干净（双方各自核实 `git status` clean）。


