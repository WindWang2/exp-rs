# R30-T07 验证与独立 Review 摘要

本页是可入仓的验证/review 记录，配合 `r30-t07-studio-teaching-guide.md`（用户操作说明）。所有结果来自本轨 worktree 的真实命令，未等待线上 CI。

## 1. 构建与资源

- 主机总上限 `-j4`：所有 configure/build/link 与重型回归走双层 `scripts/dev/resource_guard.py`（canonical host lock `exp-rs-agent/host-build` + 本轨 `build-dev` 锁）；编译显式 `--parallel 4`，CTest `-j1` 且 guard `--parallel 1`。
- 首次 configure 撞 T01 的活锁返回 **75**：记录后先做非锁工作再重试，未删除活锁、未终止其他 agent。
- 当前 HEAD 可重新 `cmake --preset dev-default` 并构建；本轨 17 个目标与三链补充 13 个目标均 `EXIT=0`。

## 2. 真实测试结果（最终 HEAD）

| 套件 | 结果 |
|---|---|
| `test_r30_t07_core` | 271 assertions / 16 cases，PASS |
| `test_r30_t07_gui` | 131 assertions / 8 cases，PASS |
| 合并 CTest 集（R29-T04 / R18-E / R28-T05 / teaching / studio / 本轨 / teaching_admin_core / experiment consistency+txn / r22 / live E2E） | **150 / 150 PASS**，33.71 s |
| 无 `TEST_PREFIX` 二进制直跑 | `test_experiment_studio_core` 155/20、`test_study_spec` 63/9、`test_r18_e_studio_session` 15/2、`test_r18_e_studio_projections` 41/3、`test_study_runner` 133/13、`test_study_analysis` 1374/10、`test_study_spatial` 69/7、`test_study_export` 3942/10、`test_r22_teaching_batch_regrade` 183/10、`test_r22_teaching_guard_hygiene` 107/8、`test_teaching_restore_r16_c_t05` 32/4、`test_studio_live_e2e` 115/2，全部 PASS |
| 隔离 `TMPDIR/XDG_CACHE_HOME` 干净目录复验 | core 271/16、gui（当时 127/8）PASS，排除缓存/旧产物假通过 |

测试保证行为/数值/状态/持久化后果：真实 SQLite ExperimentStore、真实 GDAL 小栅格、真实 committed DatasetStore version + UUID label schema、真实 OutputVerifier/verdict stamp、真实 batch report 文件与 sha256、真实 QTableWidget/QPushButton/QLineEdit 操作。不使用源码字符串断言、恒真断言或仅退出码判定。

## 3. 中途真实失败与修复（未掩盖）

| 失败 | 根因 | 修复与复验 |
|---|---|---|
| 首轮 GUI fixture 失败 2 处 | `LabelSchema::validate()` 要求 UUID-shaped `schema_id`；`Completed` 是 terminal 状态，round-trip gate 要求 `finishedAtUtc` | 改用合法 UUID 并同步 manifest ref；去掉无 finish stamp 的 terminal 状态，与 R29 fixture 一致 |
| 构建 `FIX_BUILD_DONE_EXIT=2` | 测试把 `std::filesystem::path` 误用 `.filePath()` | 改用 `dir / "..."`，重建 54 targets `EXIT=0` |
| CTest 86/87 → `test_r29_t04_compare::D3: run selection survives a table rebuild by run id` 失败 | 本轨把「换 store 清 pins/选择记忆」写成无条件，重开同一 store 也清空 | 按 `QFileInfo::canonicalFilePath()` 判定，仅不同 store 清；87/87 PASS |
| `test_studio_live_e2e` 1/115 断言失败（refs-only session 含 `digest`） | 本轨第一版把空的 `submitted_spec_digest` 无条件写入 session JSON | 空值不写盘，保持 R18/R29「capsule body 不进 session」契约；115/115 PASS |
| R1 的 P0-1（Qt 整数 `isDouble()` 漏判） | 本机 Qt 6.12.0 实测 `QJsonValue::Type` 无 Integer，整数 `isDouble()=1`，前提不成立 | 有依据反驳；仍新增 `studio_json_numeric.h` 统一排除法消除跨 Qt minor 风险，并补 digest/整数指标/文本往返回归 |

## 4. 独立 Review

- **R1**：由未参与实现的独立 code-explorer 子 agent 只读审查 `BASE..HEAD` 白名单 diff，结论「阻塞」。共 1 个 P0、8 个 P1、10 个 P2。
- **处置**：P0 一致性风险已修（见上）；P1-1/2/3/4/5/6/7/8 全部修复并补回归；P2-1/2/3/4/5/7/8/10 修复；P2-6 经核对为安全（维护注意项）、P2-9 核对 PASS。
- **有依据的反驳/降级**：
  1. P0-1 的 Qt 前提在本机不成立（实测探针结论）；一致性风险仍修。
  2. P1-7 建议「重复 digest 只丢弃冲突条目、保留 `ok=true`」**不采纳**：`test_r18_e_completion_ledger.cpp` T103c 把「复制判定文档伪造两个 lab」钉死为 `CHECK_FALSE(refused.ok)`，局部丢弃会破坏既有防伪造契约。
- **R2**：另一个独立 reviewer 复查修复与最终完整 diff，结论见 PR 正文；R2 绑定最终 HEAD，审查后如有新增代码再补差异复核。

## 5. 三条完整链

1. **设计→证据**：完整 `ParameterStudySpec` → 真实 registry validator → 真实小 study → missing/真实零/整数 metric 与 800 行两页 matrix → 真实 label schema/protocol/support 比较 → 关闭恢复 → export bundle（设计/提交/报告/导出身份一致）。
2. **离线复现**：portable bundle → 另一 store dryRun（CANDIDATE，写入为零）→ id/relocation/冲突/容量策略 → 导入与幂等重导（Created evidence）→ recorded-run 浏览/readiness → 源搬移后诚实降级。
3. **教学交付**：authoring/preflight → 真实产物评分 → 正确 lab/course 的 ledger → 前置解锁 → 草稿/重启 → 漂移降级 → 真实批次 partial/cancel/resume → guarded 本地反馈包 + 班级汇总（零写入/坏源/冲突均不记为成功）。

## 6. 未执行的环境通道

未启用 QGIS 真机渲染、GPU/TensorRT/OpenVINO、OTB/ITK 全编译、多路 sanitizer；本轨变更不需要且依赖组合未就绪。未主动触发/等待线上 CI。
