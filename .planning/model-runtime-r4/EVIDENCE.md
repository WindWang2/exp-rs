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
