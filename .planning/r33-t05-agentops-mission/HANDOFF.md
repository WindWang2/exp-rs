# R33-T05 HANDOFF — 跨轨交接

## → T06（桌面与 i18n）

- 本轨未新增用户可见文案（全部改动为模型/存储/脱敏/交付层，无新 tr() 字符串）→ 无 TS 同步需求；`resources/translations/sicnu_zh_CN.ts` 本轨未触碰。
- 若 T06 后续改 MissionTimelinePanel/视图层：`MissionTimelineModel::appendNewTasks` 的可见性语义已变更（R33-T05 WP02）——**分页状态下 append 不再立即揭示全部历史**；视图想"跳到最新任务"应滚到 `model->rowCount()-1`（fetchMore 后可达）而不是依赖 append 自动全量揭示。详情见 mission_timeline_model.cpp 内注释与 test_r33_t05_mission_paging.cpp。

## → T04（执行与业务后端）

- `SessionJournal::fromJson` 现在校验并采用持久化 `next_seq`（必须严格大于最后 entry seq、非 INT64_MAX）；任何手工构造 journal 文档的生产/测试代码必须写一致形状（`toJson` 的输出天然满足）。R30-T05 的 read_bound fixture 已同步修正。
- `compactProjection` 现在保留 terminal 条目的 string payload（stop reason）。依赖"压缩后 payload 全部变 {compacted,...}"的消费者不存在（已检索）；replay 语义只会更完整。

## → T03（data/portable/atomic_fs 维护方）

- 本轨在 `mission_context_store.{h,cpp}` 新增 `readMissionSidecarBytes`（stat+chunked 有界读，上限= `sicnu::platform::sidecar::kDefaultMaxSidecarBytes` 32MiB）。若 T03 调整 sidecar 写权威上限，读取预算需同步（单一事实来源是 durable_sidecar.h 的常量，已直接引用）。

## → 后续 R33-T05 批次（本轨未完成项）

- WP03 剩余：同 missionId 同任务数的"替换"文档检测（当前仅防 shrink/异 id；权威 append-only 下不可达，需在 shell/bridge 层做 revision 回退仲裁——见 main_window_workbench.cpp:1193-1201 incremental 判定，revision 回退+同尺寸场景仍会走 incremental 并整体降级）。
- WP04/WP07/WP10/WP12/WP14–WP20 未启动（见 PLAN）。
- #2078/#2079（operations_coordinator/decision_record）未处理——复核 #2134 是否已修；未修则属 T04/后续轨（本轨写集排除）。
- #1932 swarm 12 条未逐子项核验。

## 平台注意（所有轨）

- **Qt 6.12.0 的 QAbstractItemModelTester 与"fetchMore 同步插行"模型不兼容**（changeInFlight 拒绝其自身 rowsInserted 内 fetchMore 探测的嵌套 begin）。任何想用 tester 验证分页模型的轨都会踩到；规避方式见 tests/test_r33_t05_mission_paging.cpp（RowContractWatcher + 版本门控）。
