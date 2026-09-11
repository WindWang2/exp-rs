# VERIFICATION — 本地可复现证据

实测时间：2026-09-11/12；主机 Linux 6.18 x64 / 16C / 62GB / GCC 16.2.1；
构建 Release + Ninja + ccache；共享主机上有多个并行 track 同时构建（记录为负载背景）。

## 构建命令（可复现）

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON \
  -DENABLE_LTO=OFF -DENABLE_SANITIZERS=OFF -DUSE_PRECOMPILED_HEADERS=ON \
  -DSICNU_BUILD_OTB=OFF -DSICNU_WITH_ONNX_RUNTIME=OFF -DSICNU_EMBED_PYTHON=OFF \
  -DCMAKE_CXX_FLAGS=-fpermissive -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
ninja -C build -j4 test_task_center test_job_engine test_worker_host \
  test_workflow_run_coordinator test_execution_plane_7 test_execution_plane_8 \
  test_execution_plane_9 test_concurrency_stress test_workflow_resume_provenance \
  test_temporal_workspace test_fault_registry sicnu_worker
```

`-fpermissive` 原因：origin/master 的 geospatial-data-fabric-8 引入的
`canonical_metadata.cpp` 在本机 GDAL 3.13.3 下有 `GUInt64*`→`size_t*` 隐式转换错误；
主构建目录即用此 flag；src/geospatial 非本方向 ownership，不改代码。

## 测试证据（全部本地实测，2026-09-12）

| 套件 | 结果 |
|---|---|
| **test_execution_plane_9（新增）** | **864 assertions / 14 cases 全绿** |
| test_task_center（含 #851 回归） | 382 / 32 全绿 |
| test_job_engine（含 #798 回归） | 446 / 34 全绿 |
| test_workflow_run_coordinator | 176 / 10 全绿 |
| test_worker_host（worker 进程 e2e） | 56 / 12 全绿 |
| test_temporal_workspace（#852 回归） | 267 / 21 全绿 |
| test_workflow_resume_provenance（e2e resume） | 67 / 2 全绿 |
| test_execution_plane_7（含 10k 压测） | 50 / 9 全绿 |
| test_execution_plane_8（8.0 回归） | 103 / 13 全绿 |
| test_concurrency_stress | 2152 / 6 全绿 |
| test_fault_registry（故障注入契约） | 31 / 9 全绿 |
| **合计** | **≈4,592 assertions 全绿** |

### 门控压测（SICNU_EP9_STRESS=1）

- 100k 逻辑任务准入结构：**6.075s，100,005 assertions，通过**
  （命令：`SICNU_EP9_STRESS=1 ./test_execution_plane_9 "[logical]" -d yes`；
  主机当时有 ≥12 个其他 track 的编译进程在竞争 CPU）。

### 故障注入证据（M6）

- `workflow_checkpoint.publish`（temp→rename 崩溃窗口）NextN 注入：
  发布失败 typed（空路径）、无 partial/tmp 文件残留、先前 checkpoint 可解析、
  下一次保存正常发布 —— test_execution_plane_9
  "checkpoint publish fault point: crash between write and rename leaves no partial state"。

## 开发过程中被测试当场抓住的真实缺陷（修复有效性证据）

1. **通知 drain 在 fold 锁作用域内调用 → 自死锁**（#860 修复的第一版自身引入）：
   `test_workflow_run_coordinator` 全套挂死；gdb 栈显示 worker 线程
   `onTaskUpdated → drainRunNotifications → pthread_mutex_lock` 自等。
   修复：fold 改为显式作用域，drain 严格锁外。已加固回归。
2. **globalMax/RSS hold 的 break 遮蔽堆中深处的 transient child**（自审发现）。
3. **engine 通用 Cancelled 记录覆盖级联已盖的 StructuredJoin 类型戳**（join 测试发现）。

## Sanitizer

- ASan/UBSan：`build-asan`（Debug + ENABLE_SANITIZERS=ON，-j2）后台构建中，
  范围限定 execution 相关套件；结果回填（或如实声明未完成——全树 sanitizer 重编译
  3160 targets，与 10 个并行 track 共享主机时成本过高）。

## 合并 master 后的回归（2026-09-12，merge origin/master = f316dfdbb4）

冲突三文件（task_center.cpp、workflow_run_coordinator.{h,cpp}）保留本分支根因方案、
弃用 master band-aid（见 ISSUE_TRIAGE 更新节）；合并后全量重跑：
382+446+174+50+103+**865**+2152+67+267+56 assertions 全绿。

## 未运行 / 环境不支持（如实声明）

- Windows Job Object 分支：本机 Linux，编译级审查（沿 8.0 声明）。
- OTB / ONNX Runtime lane：SICNU_BUILD_OTB=OFF / SICNU_WITH_ONNX_RUNTIME=OFF。
- 线上 CI/CD：按 track 约定不等待、不作为完成条件。
