# RESUME — R28-T02 会话恢复（每次中断后先读本文件）

## 当前状态（最后更新：2026-11-05 12:10 +0800）

- BASE_SHA = 50cc3255c5（origin/master；提交前需重新 fetch 核对）
- worktree = /home/kevin/projects/rs-studio/exp-rs-r28-t02-20261104T180429Z
- 分支 = agent/r28-t02-data-runtime-20261104T180429Z（未推送，尚无 commit）
- build = build-r28-t02（Debug + ccache + CCACHE_PREFIX=raise-compiler-stack.sh + OpenCV 5 prefix）
- **17 个目标已全部编译通过**（WP01 基线 12 + 共享 io 3 + 新增 3）
- 新增测试首轮：12 例中 10 过；2 失败已定位并修复（queued-cancel 需 QCoreApplication 事件分发器；infer 需 GDALAllRegister），重编+重跑在 host 锁队列中

## 已写代码（未提交）

1. `src/data/import_session.h/.cpp`：checkSteps/proceedSteps/hasPendingItems/requestCancel/verifyFacts + identityToken journal 字段
2. `src/data/import_preflight.h/.cpp`：ImportPreflightBatchReport + inspectBatch
3. `src/cli/cli_commands.cpp`：`data preflight` 子命令 + qPreflightBatchToJson
4. `src/geospatial/fabric/object_store.cpp`：fabricCachedPath RemoteHttp 未装 cache → typed GeoError（#1765 后端收口）+ tests/test_io_fabric_object_store.cpp 对应 case 更新
5. `src/jobs/job_engine.cpp`：shutdown stuck>0 → trace `shutdown_abandoned_workers`
6. tests/test_r28_t02_import_session_runtime.cpp（7 case）、test_r28_t02_geospatial_interop.cpp（4 case）、test_r28_t02_infer_cpu_loop.cpp（1 case，SICNU_HAS_OPENCV 门控）
7. tests/CMakeLists.txt 追加 2 个 r28-t02 注册块

## 下一条命令

1. 等后台任务（rebuild+rerun 两套件）→ TESTS_OK 后：
2. WP01 基线跑：`QT_QPA_PLATFORM=offscreen ctest --test-dir build-r28-t02 -j1 --output-on-failure -R "test_(job_engine|r27_t02_job_engine|task_center|task_center_12|execution_plane_[789]|concurrency_stress|workflow_cancel|agent_ops_task_center_executor|r27_t02_(import_session|tail_fault)|io_fabric_object_store|io_fabric_identity_11|io_uri)$"`（注意 prefix 形态——用实际 ctest -N 输出校准）
3. 分类失败 → 修复/交接；然后 WP12 场景 A/B；然后 commit 首个里程碑

## 未解失败

- 新增 2 例（修复已写，待重跑确认）
- WP01 取消簇复现未开始（构建已就绪）

## 工具链/锁状态

- git 元数据锁 = flock 文件 ~/.cache/exp-rs-agent/git-metadata.lock
- host 构建锁 = resource_guard --lock-dir ~/.cache/exp-rs-agent/host-build（外层）+ build-r28-t02 锁（内层）
- 同机并行：R28-T01（science）、T03（harness）、T05×2（redbase + workflow-teaching）——锁轮转频繁，构建命令都带 --wait 14400 排队
- 每阶段命令带 QT_QPA_PLATFORM=offscreen、ctest -j1

## review 状态 / PR

- 未开始 / 未创建
