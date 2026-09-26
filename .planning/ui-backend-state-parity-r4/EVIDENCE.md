# EVIDENCE — ui-backend-state-parity-r4

验证证据链。全部命令在本轨道 worktree `/home/kevin/project/exp-rs-ui-state-parity-r4` 执行；
构建 `ninja -j2`（显式 `/home/kevin/toolchain/ninja`）、`CTEST_PARALLEL_LEVEL=1`、offscreen 由 CTestCustom 注入。
本机直跑惯例（TEST_INFRA "belt-and-suspenders"）：`LD_LIBRARY_PATH=/home/kevin/pwb-sdks/root/usr/lib:/usr/lib`。

## 1. 基线红证据（未修源码上的 oracle 首跑）

构建：全新构建目录 `build/`（worktree 内首次 configure：Debug / ENABLE_TESTS=ON / Ninja / pwb-sdks 前缀），
定向闭包 3037 边（`cmake --build build -j2 --target <6 parity targets>`）。

`ctest -R "parity.*r4::" -j1` 于 15e5c66b5 pristine 源码 + 测试提交 `aa3eabdd4`+`c871b7989`：
**40 个用例中 10 个红**（其余 30 个为契约钉住用例，born green）：

| 用例 | 竞态类/漂移 | 红的证据（pristine 行为） |
|---|---|---|
| AS-1 | 类 4 乱序 | 晚到的旧应答（7 features）覆盖了新查询结果：rowCount==7≠3 |
| AS-2 | 类 6 超时后落地 | 真实 10s 传输超时的错误二次进入完成通道：deliveries==2 |
| AS-3 | 类 2 宿主关闭 | 关闭后到达的结果写进隐藏对话框：rowCount==5≠0 |
| AS-4 | 类 1 会话切换 | 死会话的图层落进新项目画布：canvas +1 |
| AS-5 | 类 3 另存后晚到 | SaveAs 后晚到自动加载进新身份：canvas +1 |
| PD-2 | 类 5 取消后落地 | 取消后迟到 max 进度以 Accepted 自闭合 |
| SP-2 | 类 5 池取消锚丢失 | 溢出清集后 isStale(victim)==false |
| RL-2 | F-01 源变 UI 不刷 | 工程增删图层后 combo 仍 1 行 |
| S3 | 压测（每步探针） | 同 RL-2 的 combo 漂移被随机风暴逐步抓住 |
| HS-1 | F-08 脏真相 | **变异法补证**（首跑红被 fixture 缺 GDALAllRegister 掩盖）：revert F-08 hunk → isDirty==false 红；恢复 → 绿。记录于 commit "register GDAL drivers…" |

其余 3 项修复（F-09 收敛/F-12 reset 窗口/F-13 去重）的红证据：
- F-12：PD-3 新增用例在修复提交前写好，pristine 上 `result()==Accepted`（PD-3 随 F-11 同批提交转绿）。
- F-09：幂等同值双写无行为可观察差异（SC-5 行为 oracle 在收敛前后均绿），正确性证据为代码审查（S-2，REVIEW_LOG）+ 收敛后套件保持绿。
- F-13：CW-1 扩展断言（同值重设不重发）随修复同批提交，pristine 行为由代码路径直接可证（setFlickerInterval 无 early-return）。

## 2. 修复转绿证据

13 项修复 = 11 个提交（F-02/03/04 合一个原语提交；F-05/06 合一个会话纪元提交；F-11/12 合一个）：
`95e3481c6, 4e09e0f18, 0aab187c2, 7d9c460a1, 3bdf35e75, a5fe044bd, e6d292fcf, e6eb16f24` 等（见 git log）。

**完成门禁双跑**（修复后，`ctest -R "parity.*r4::" -j1`）：
- RUN 1：`100% tests passed, 0 tests failed out of 40`，CTEST_EXIT=0（/tmp/r4-green-run1.log）
- RUN 2：`100% tests passed, 0 tests failed out of 40`，CTEST_EXIT=0（/tmp/r4-green-run2.log）

压测：S1+S2+S3 共 **1430 个断言/步进探针**全绿（种子 20260927；`SICNU_PARITY_STRESS_SEED` 可复跑）。

## 3. 邻接回归（本轨道触及面的既有套件，全部 rc=0 / 全绿）

| 套件 | 触及面 | 结果 |
|---|---|---|
| test_workbench_full_shell_lifecycle | F-05/06（main_window_project/docks 的工程开合/SaveAs/story boundary） | **126/126 断言全绿** |
| test_scan_pool | F-07（cancel 剪枝） | 全绿 |
| test_progress_dialog | F-11/12（setValue 防护） | 全绿 |
| test_selection_context | F-09 邻接（权威未改动） | 全绿 |
| test_active_view_host_data_context / test_active_view_host_viewport | F-09（删除第二写者） | 全绿 |
| test_layer_sync_contract | F-09 邻接 | 全绿 |
| test_guided_workflow_sync | 邻接 | 全绿 |
| test_guided_workflow_widget | — | **无法链接：#1335 记录的 master 预存缺陷**（sicnu_agent→agent_loop 未定义，约 161 个测试的已知根因），非本轨道引入；该套件的 GW 面由本轨道 test_parity_state_mirror_r4 的 GW-1/2/3/5 oracle 覆盖 |

注：为使 full-shell 套件可构建，其 CMake 块补了与 parity 目标相同的显式 `sicnu_agent_loop`+`Sicnu::agent_ops`
（tests/CMakeLists.txt 注释；#1335 合并后可与 parity 目标的链接一并移除）。

## 4. gate 漂移演练（活文档）

已演练（2026-09-27）：删除 PARITY_MAP.md 的 RL-2 行 → `test_parity_map_gate_r4` 红
（"parity tag not cited by PARITY_MAP: [parity-rl2]"，exit 非 0）→ 恢复该行 →
"All tests passed (5 assertions)"，exit 0。删除映射行、或给用例改 tag 而不更新 map，均会立刻红。

## 5. 资源红线审计

- 并行度：全程 `CMAKE_BUILD_PARALLEL_LEVEL=2` / `-j2`；无 -j1 降级事件（62Gi 机器，构建期峰值 ~50%）。
- subagents：2 个只读普查代理（Phase 0）+ 1 个评审代理（Phase 5，见 REVIEW_LOG）= 3 封顶。
- CI：未等待线上 CI。master 仓库只读；全部改动在独立 worktree 分支 `hardening/r4-ui-state-parity`。
- 与在途 PR 重叠：tests/CMakeLists.txt 为纯追加块；src/app/main_window.cpp 未触碰（#1334 领地）。
- 环境注记：#1335（未合并）修复的预存链接缺陷（sicnu_agent→agent_loop/OpsDriver 未定义）在 parity
  测试目标上以显式链接 `sicnu_agent_loop`+`Sicnu::agent_ops` 承接（tests/CMakeLists.txt 注释），#1335
  合并后可移除。

## 6. 压测可复现性

种子固定 20260927（S1）/+1（S2）/+2（S3）；复现：`SICNU_PARITY_STRESS_SEED=<seed> ctest -R test_parity_stress_r4 -V`。
S3 的对象生命周期规则（被项目删除的图层永不复用）在测试内 enforced（available 池）。
