# EVIDENCE — Track 15 Model Runtime R4 验证证据链

全部命令在 worktree 根执行；构建目录 `build-gcc15`（gcc-15/Debug/ENABLE_TESTS=ON/-j2；
显式 `-DCMAKE_CXX_COMPILER_LAUNCHER=` 规避 raise-compiler-stack.sh 假绿陷阱）。

## 1. 基线（Phase 0）

- `origin/master` = `15e5c66b543ef3874cb929f17529ef456bd6c059`（2026-09-27 fetch，无漂移）。
- 基线构建：master 有预存断裂（#1335 在修）：`test_lab_data_pack`（缺 Qt include）、
  5 个 gdal/fault 链接断裂、`sicnu_agent` 链接图（见提交历史 build: link 一条）。
  以 `make -k` 绕行，未触碰这些目标（D-9 记账）。
- 基线 ctest 过滤面（`-R "provider|model|runtime|session|interpreter" -j1`）：
  **23 个失败**，其中本轨道 16 个 RED（预期缺陷的证据：SIGFPE 除零、SEGFAULT 空指针、
  诚实性/顺序/泄漏隔离等）+ 7 个 master 既有红（InSAR refusals、Dynamic enum sources、
  model manifest probe、session×3、mission runtime）+ 1 个 NOT_BUILT（#1335 域）。

## 2. 修复的 RED→GREEN 证据

| 缺陷 | RED 证据（基线） | GREEN（修复后） |
|---|---|---|
| chunk 分区算术 int UB | `SIGFPE`（tileWidth=0 除零；test_runtime_chunk_boundaries_r4 C1） | 同用例 typed invalid_argument/overflow_error，50 断言全绿 |
| chunk graph 空缓冲解引用 | `SEGFAULT`（C3） | typed logic_error |
| gpu 复用路径 typed 不诚实 | 用例 FAILED（Acquired vs 实际小档） | AcquiredReduced |
| exec 泄漏报告跨实例污染 | 用例 FAILED（A 泄漏→B 干净后报告非空） | 干净窗口 |
| publish stray-sidecar 采纳 | window A 后 final prov 缺失 | 恢复配 PROV-PARKED |
| publish 并发无双占 | 竞争用例无拒绝（D3 无 fence 时两 guard 均可构造） | typed AlreadyRunning |
| python stderr 不捕获 / 进程树残留 / 固定崩溃串 | 编译期即缺 API（capturedStderr 不存在） | 三类断言全绿 |
| WP-A 四元组/顺序/值诚实 | 12 类缺路径或缺实际值 | 35 断言全绿 |

## 3. 终局门禁（连续两轮，过滤面 382 例）

- Pass 1：98% passed, 8 failed —— **全部为 master 既有红**（与基线 §1 的 7 个 + 1 NOT_BUILT
  完全一致），本轨道 0 新增失败。
- Pass 2：（回填，同基线对照）
- 我方 10 套件 65 用例逐套件全绿（worker 64 断言/12、gpu 51/8、chunk 50/7、exec 31/6、
  oom 23/2、provider 11/2、manifest 35/14、publish 87/4、python 44/6、stress 9/1）。
- 压力套双跑：RUN_SERIAL + -j1，两遍全绿（线程违规计数 0、终态池余量 0）。
- provider 矩阵：默认 off/off 守护绿；configure 级 ON/无 SDK 两格优雅降级（PROVIDER_OOM_MATRIX.md）。

## 4. 资源纪律

- 构建 `-j2`（`CMAKE_BUILD_PARALLEL_LEVEL=2`）、ctest `-j1`；RSS 观测 35-39%，未超 70%。
- subagent：Phase 0 用 2 个只读扫读（context-sweeper），无递归；Phase 5 review 用 1 个。
- 未等待线上 CI；未触碰白名单外目录（例外：src/agent/CMakeLists.txt 一行链接，D-9 披露）。

## Round 2 证据（2026-09-28，PR #1353 合并后分支 hardening/r4-model-runtime-r2）

### 1. 提交清单（6 个原子提交，全部独立可编译）
| 提交 | 内容 |
|---|---|
| ae9fcf1616 | tests/CMakeLists.txt 冲突标记 P0（master 必断 configure） |
| 58b76ca1c8 | qt_lifecycle.h 类型修复（D-14，解锁 test_provider_http + teardown 族 17 TU） |
| aaec6433b5 | （被 ac26c2a86a 和解覆盖，见 D-15：单行 grep 漏 chunk_graph 多行钉子） |
| 419d1e2ad1 | publish fence 生命周期缺陷（D-17）+ 用例握手 fenceHeld 边 |
| 2fd7ce5f91 | python 池真实恢复路径端到端 3 用例（遗留#5）+ waitUntil helper |
| ef263ce196 | test_gpu_plane 合并拼接语义并集（D-16） |
| ac26c2a86a | chunk 契约双钉和解：impl 保持 #1056 overflow_error + contract_11 三断言更新（review P0-1/D-15）+ rig 崩溃余量 6s（P2-1） |

### 2. 基线 → 终局（过滤面 ctest -j1）
- 基线（修复前，master a726d17a6 + 本分支前 3 提交前）：396 例，394 过 / 2 红
  （GPU plane evicts stale model identities；test_runtime_publish_orphan_r4 fence 用例）。
  Round 1 基线的 8 个既有红中 7 个已被合并列车修复，1 个（mission runtime）转为 Skipped。
- 终局双跑：见 §3（回填）。

### 3. 终局门禁（连续两轮）
- Pass 1（提交 ac26c2a86a 前，过滤面不含 chunk 测试，结论仍有效）：399/399 100% passed，
  exit 0（151.86s）；mission runtime helper = Skipped（typed skip）。
- Pass 2（同前）：399/399 100% passed，exit 0（159.25s）。
- 和解后复跑（ac26c2a86a：tile_spec 回 master 态 + contract_11 更新）：见 §3b。

### 4. 单套件证据
- test_chunk_contract_11：56 断言 / 9 用例全绿（和解后，断言已随 #1056 契约更新为
  overflow_error；基线 3 红的性质=与 chunk_graph 的双钉矛盾，非 impl 缺陷）。
- test_chunk_graph：365 断言 / 31 用例全绿（#1056 钉子保持 master 态）。
- test_runtime_chunk_boundaries_r4：50/7 全绿（partition 面 overflow_error 不变）。
- test_execution_scale_fault_11：86/4 全绿（buildTileGrid happy-path 等价）。
- test_gpu_plane：22/4 全绿（held/drop 两段钉子）。
- test_runtime_publish_orphan_r4：87/4 全绿；fence 用例 10/10 连跑。
- test_runtime_python_channel_r4：75/9 全绿 ×4 连跑（6 既有 + 3 新真池恢复）。

### 5. 白名单外触碰披露（D-9/D-14/D-16 先例）
- tests/support/qt_lifecycle.h（D-14，解锁白名单内 test_provider_http）
- tests/test_gpu_plane.cpp（D-16，合并拼接产物修复）
- tests/CMakeLists.txt 的冲突标记删除属白名单内（文件本身在列）。
- src/operators/runtime/model_publish.cpp：Round 1 已实际触碰（fence/adoption 即在此文件），
  Round 2 的 fence 生命周期修复延续同一授权面；如需严格口径，见 PR 正文披露段。

### 6. master 既有断裂（白名单外，移交）
- src/app/panels/data_manager_panel.cpp:222 'tr' was not declared → sicnu_geo_rs_shell
  （主程序二进制）无法编译。精确修复建议：为该文件补 Q_OBJECT 语境或改用 QObject::tr/上下文
  枚举（i18n 域，#1339 系移交）。
- test_chunk_contract_11 的 3 红由本轨道收口（aaec6433b5），不再移交。

### 3b. 和解后过滤面复跑（ac26c2a86a 之后）
- Pass 1: exit 0（144.67s）；唯一未运行 = mission runtime fresh-process load helper（typed Skipped）。
- Pass 2: exit 0（140.18s）；同上。零失败。
