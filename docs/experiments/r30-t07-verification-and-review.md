# R30-T07 验证与独立 Review 摘要

本页是可入仓的验证/review 记录，配合 `r30-t07-studio-teaching-guide.md`（用户操作说明）。所有结果来自本轨 worktree 的真实命令，未等待线上 CI。

## 1. 构建与资源

- 主机总上限 `-j4`：所有 configure/build/link 与重型回归走双层 `scripts/dev/resource_guard.py`（canonical host lock `exp-rs-agent/host-build` + 本轨 `build-dev` 锁）；编译显式 `--parallel 4`，CTest `-j1` 且 guard `--parallel 1`。
- 首次 configure 撞 T01 的活锁返回 **75**：记录后先做非锁工作再重试，未删除活锁、未终止其他 agent。
- 当前 HEAD 可重新 `cmake --preset dev-default` 并构建；本轨 17 个目标与三链补充 13 个目标均 `EXIT=0`。

## 2. 真实测试结果（最终 HEAD）

| 套件 | 结果 |
|---|---|
| `test_r30_t07_core` | 281 assertions / 16 cases，PASS |
| `test_r30_t07_gui` | 157 assertions / 9 cases，PASS |
| 合并 CTest 集（R29-T04 / R18-E / R28-T05 / teaching / studio / 本轨 / teaching_admin_core / experiment consistency+txn / r22 / live E2E） | **150 / 150 PASS**，33.71 s |
| 无 `TEST_PREFIX` 二进制直跑 | `test_experiment_studio_core` 155/20、`test_study_spec` 63/9、`test_r18_e_studio_session` 15/2、`test_r18_e_studio_projections` 41/3、`test_study_runner` 133/13、`test_study_analysis` 1374/10、`test_study_spatial` 69/7、`test_study_export` 3942/10、`test_r22_teaching_batch_regrade` 183/10、`test_r22_teaching_guard_hygiene` 107/8、`test_teaching_restore_r16_c_t05` 32/4、`test_studio_live_e2e` 115/2，全部 PASS |
| 隔离 `TMPDIR/XDG_CACHE_HOME` 干净目录复验 | core 271/16、gui 127/8（R2 前版本）PASS；R2 修复后 core 281/16、gui 157/9 再次全绿，排除缓存/旧产物假通过 |
| base 变化后重新配置/构建 | 开发期 `origin/master` 从 `d67cd37002` 前进到 `7538c0d667`（PR #2060 Python runtime），以非破坏 merge 更新本轨分支并逐段核对 `tests/CMakeLists.txt` 两块注册共存；`RECONFIGURE_EXIT=0`、28 目标 `FINAL_HEAD_BUILD_EXIT=0`，重跑全部回归 |

测试保证行为/数值/状态/持久化后果：真实 SQLite ExperimentStore、真实 GDAL 小栅格、真实 committed DatasetStore version + UUID label schema、真实 OutputVerifier/verdict stamp、真实 batch report 文件与 sha256、真实 QTableWidget/QPushButton/QLineEdit 操作。不使用源码字符串断言、恒真断言或仅退出码判定。

## 3. 中途真实失败与修复（未掩盖）

| 失败 | 根因 | 修复与复验 |
|---|---|---|
| 首轮 GUI fixture 失败 2 处 | `LabelSchema::validate()` 要求 UUID-shaped `schema_id`；`Completed` 是 terminal 状态，round-trip gate 要求 `finishedAtUtc` | 改用合法 UUID 并同步 manifest ref；去掉无 finish stamp 的 terminal 状态，与 R29 fixture 一致 |
| 构建 `FIX_BUILD_DONE_EXIT=2` | 测试把 `std::filesystem::path` 误用 `.filePath()` | 改用 `dir / "..."`，重建 54 targets `EXIT=0` |
| CTest 86/87 → `test_r29_t04_compare::D3: run selection survives a table rebuild by run id` 失败 | 本轨把「换 store 清 pins/选择记忆」写成无条件，重开同一 store 也清空 | 按 `QFileInfo::canonicalFilePath()` 判定，仅不同 store 清；87/87 PASS |
| `test_studio_live_e2e` 1/115 断言失败（refs-only session 含 `digest`） | 本轨第一版把空的 `submitted_spec_digest` 无条件写入 session JSON | 空值不写盘，保持 R18/R29「capsule body 不进 session」契约；115/115 PASS |
| R1 的 P0-1（Qt 整数 `isDouble()` 漏判） | 本机 Qt 6.12.0 实测 `QJsonValue::Type` 无 Integer，整数 `isDouble()=1`，前提不成立 | 有依据反驳；仍新增 `studio_json_numeric.h` 统一排除法消除跨 Qt minor 风险，并补 digest/整数指标/文本往返回归 |
| R2 的 P1-A（拒绝交付让 dock 永久 busy） | stale / mismatch 出口未收尾 worker、未消费提交身份 | 统一幂等 `finishLiveStudyThread()`，mismatch 消费身份、expired-id 保护 newer；新增真实 store + 栅格的 dock 生命周期回归 |
| R2 的 P1-B（落盘 manifest 状态为默认值） | 状态计算晚于 `result.toJson()` | 状态计算前移，manifest 与返回值逐字段一致；新增断言 |
| R2 的 P1-C（发布后失败摘要仍称旧文档完好） | dock 摘要未按 IO 层 code 分支 | 分支文案；core 用 sidecar `Verify` 注入证明主文档确已被替换 |

## 4. 独立 Review

- **R1**：由未参与实现的独立 code-explorer 子 agent 只读审查 `BASE..HEAD` 白名单 diff，结论「阻塞」。共 1 个 P0、8 个 P1、10 个 P2。
- **处置**：P0 一致性风险已修（见上）；P1-1/2/3/4/5/6/7/8 全部修复并补回归；P2-1/2/3/4/5/7/8/10 修复；P2-6 经核对为安全（维护注意项）、P2-9 核对 PASS。
- **有依据的反驳/降级**：
  1. P0-1 的 Qt 前提在本机不成立（实测探针结论）；一致性风险仍修。
  2. P1-7 建议「重复 digest 只丢弃冲突条目、保留 `ok=true`」**不采纳**：`test_r18_e_completion_ledger.cpp` T103c 把「复制判定文档伪造两个 lab」钉死为 `CHECK_FALSE(refused.ok)`，局部丢弃会破坏既有防伪造契约。
- **R2**：另一个未参与实现/R1 的独立 reviewer 复查 `7538c0d667..HEAD` 完整 diff，原结论「需修 3 条 P1 后复核」——**0 个 P0、3 个 P1**（拒绝交付让 dock 永久 busy、落盘 manifest 状态为默认值、发布后失败摘要仍称旧文档完好）及若干 P2。三条 P1 全部修复并新增行为回归；P2 中 8 项修复、3 项有依据记录为限制。
- **R2 最终差异复核**：同一 reviewer 对 `a8791918..ad638a79` 的修复差异做只读复核，逐条确认三条 P1 闭环、8 项「是否引入新问题」检查通过，结论 **PASS（可交付）**。逐条见 `.planning/r30-t07-experiment-teaching/REVIEW_R2.md`（本地账本）。
- 审查后新增的代码（R2 修复）已全部纳入上表最终 HEAD 的重建与复验；无「旧 HEAD 审查冒充最终 HEAD」。

### 4.1 R2 最终复核保留的 P2 / 限制（不阻塞交付）

1. **post-publish `VerifyFailed` 的 manifest**：极端情况下 sidecar 回读校验失败但主文件已 rename，磁盘 manifest 会带着写入前的 `ok=true`，而返回值已 typed 为 `delivery_manifest_failed` 且绝不记为成功。durable sidecar 契约规定 main 不回滚、由 caller surface；后续可补「manifest 可能已落盘、状态待复核」诊断。
2. **worker 运行期强制 finish**：仅在测试 seam / 异常报告路径下，mismatch 交付会在 worker 未结束时把它移入 reaper，dock 析构可能在 GUI 线程等待该 reaper。正常生产路径投递由 worker 结束后发出，reaper 立即完成；`liveBusy()` 守卫与 `QPointer`/`alive` 双保护保证无 UAF 和并发提交。后续可让析构统一走 detached supervisor。
3. **发布后失败文案缺 GUI 端到端注入**：dock 的 `Re-open the Studio` 分支由 IO 层 code 驱动，已由 core 的 sidecar `Verify` 注入（实测 `verify_failed` + 主文档被替换）与代码审阅支撑；GUI 未做端到端注入（不为测试在 dock 增加生产 hook）。
4. **头文件注释措辞**：`finishLiveStudyThread()` 注释称「EVERY delivery exit」，`submission_expired_id` 是刻意例外，实现处已就地解释；语义正确，仅措辞可更精确。
5. **执行结果由主 agent 运行**：R2 与最终差异复核均为只读复核，未独立复跑构建/测试；上表数字由主 agent 的真实命令产生并附日志路径。
6. **ledger 边界**：`digestNamespace` 已按课程分域，跨课程共享同一判定文档由 `loadVerified` 的 session 复验兜底；R18-E T103c 的整本拒绝回归保持。`__legacy__` token 与 `courseId` 含 `::` 属可逆性边界，本轮不扩展编码方案。

## 5. 三条完整链

1. **设计→证据**：完整 `ParameterStudySpec` → 真实 registry validator → 真实小 study → missing/真实零/整数 metric 与 800 行两页 matrix → 真实 label schema/protocol/support 比较 → 关闭恢复 → export bundle（设计/提交/报告/导出身份一致）。
2. **离线复现**：portable bundle → 另一 store dryRun（CANDIDATE，写入为零）→ id/relocation/冲突/容量策略 → 导入与幂等重导（Created evidence）→ recorded-run 浏览/readiness → 源搬移后诚实降级。
3. **教学交付**：authoring/preflight → 真实产物评分 → 正确 lab/course 的 ledger → 前置解锁 → 草稿/重启 → 漂移降级 → 真实批次 partial/cancel/resume → guarded 本地反馈包 + 班级汇总（零写入/坏源/冲突均不记为成功）。

## 6. 未执行的环境通道

未启用 QGIS 真机渲染、GPU/TensorRT/OpenVINO、OTB/ITK 全编译、多路 sanitizer；本轨变更不需要且依赖组合未就绪。未主动触发/等待线上 CI。
