# R30-T07 用户操作指南：Studio 设计、可复现交付与教学证据

本页说明桌面端「Experiment Exploration Studio」「实验比较面板」「教学 Lab Cockpit」「教师控制台」在 R30-T07 之后的实际行为。所有结论都来自当前代码的真实路径；不包含未接通的功能。

## 1. Studio 会话：保存失败不会损坏旧记录

- 会话文档位置：AppData `/experiment_studio/studio_session.json`，另有 `.last-good` 冗余副本。
- 写入通过仓内 durable sidecar：临时文件 → fsync → 原子 rename → 回读校验 → last-good 轮转。**发布（rename）之前的任何失败**（临文件名冲突、短写、fsync 拒绝、rename 失败、容量超界）都保持旧文档字节不变，designer 摘要显示 typed 错误码，技术详情日志追加原因。
- **发布之后**的回读校验或 last-good 轮转失败时，主文档已经换成新内容；此时返回独立的 `session_publish_verify_failed` 并明确提示「从 last-good 恢复」，不谎称旧文档完好。
- 读取有 4 MiB 上限。超界、损坏 JSON、未来版本 schema、非法数值字段都有明确拒绝；主文档出现损坏 JSON、外来/未来 schema 或非法字段，而 `.last-good` 完好时，会恢复上一代并明确提示「last good copy was restored」，同时保留主文档的原始拒绝原因。没有可用 last-good 时给出精确字段名与拒绝码。
- 恢复 spinbox 时按字段真实契约校验（有限、整数、在范围内）；非法字段拒绝整份文档并列出摘要原因，**不静默夹紧成另一个设计**。旧版单文件会话（无 policy、无 store refs）照常恢复；policy 中的 `null` 与缺省字段按声明默认值处理。
- 打开**同一个** experiment store（按 canonical 路径判定）不会抹掉刚恢复的报告/refs；换 store 时按身份隔离清除全部 refs，并在日志说明原因。

## 2. 完整设计与导出身份

- Designer 页新增 **Advanced spec (JSON)** 编辑器：
  - `Load Fields To Spec` 把当前表单导出为 `ParameterStudySpec` v1 JSON；
  - `Apply Full Spec` 用严格的 `ParameterStudySpec::fromJson` 解析并复用真实 registry schema 的 designer validator。支持多个 **flat numeric** dimension、baseParameters、多个 metric/objective、seed/预算/空间选项。
  - nested 路径（含 `.` `/` `[`）、非 numeric sweep、未知字段、预算越界都会明确拒绝；v1 不扩展新实验语言。
- 任何表单字段编辑都会使旧的「Valid design」失效（`Design changed — re-validate…`），内存与磁盘 draft 同时清空。
- 运行时的**提交快照**（submission id、规范化 spec、spec digest、designer VM、store path）被冻结并**立即持久化**，运行中关闭窗口也能看到提交了哪一版设计。采样失败不会留下"幽灵提交"；一次投递消费一次身份，重复或过期回调不会覆盖新会话；身份不匹配或空报告的交付同样会收尾 worker、恢复按钮并清空本次冻结的身份，不会让界面永久停在「Study running…」。report 与 export bundle 显示的是**真正提交的那一版设计**，不是运行期间新敲的草稿。
- 提交身份随会话文档持久化，因此**运行 → 关闭 → 重开 → 导出**仍会带 `submission` 与 `metric_cells` 可选字段；旧 bundle 仍可离线读回。报告与设计 digest 不一致时 bundle 记录明确的 identity mismatch issue，恢复的提交身份读不回时记录显式 issue。

## 3. Run Matrix：真实指标与有界浏览

- 表头显示所选 metric 名与显示协议（`study/<algorithm>/<strategy>`）。
- 所选 metric 未记录时显示 `— (metric not recorded)`；null / per-class object / array / 字符串 / 布尔 / 非有限值各有独立状态，**绝不回退显示别的 metric 的数值，也不会把缺失写成 0**。真实测量值 0 才显示 `0`；整数指标与经 JSON 文本解析的整数指标都显示真实值。
- 每页 500 行；`Previous Page` / `Next Page` + 页码可浏览 >500 点的报告，filter 变更回到第 0 页。「preview limited to first 500」只在首页出现，翻页后不再显示自相矛盾的提示。选择按 `pointId#replicateIndex` 记忆，只有用户真实选择变化才更新记忆，跨页往返不丢失、程序化重建不会覆盖。

## 4. 实验比较：schema 证据与跨页 pins

- 比较报告新增 `schema_evidence`：从 `run → datasetVersionId → manifest.label_schema → DatasetStore::labelSchema` 解析**真实** label schema，调用 `compareLabelSchemas` 并把结果传给 `pairedRunComparison`。
- 拿不到 schema 时明确 `checked: false` / `not_checked_missing_evidence`，并把 `paired_summary.schemas_compatible` 写为 `null`；新增权威字段 `comparable_with_schema_evidence` 此时为 false。旧的 `comparable` 字段语义冻结，同时新增 `comparable_unverified` 明确标记「协议相同但 label schema 未核对」，避免旧字段被误引为已验证。
- **Pin A / Pin B / Clear Pins** 显式固定两个 run ID，跨页/搜索/重建后仍按 ID 解析；换 store（连同跨页选择记忆）、换实验会清空 pins；记录被删除在下一次比较或 settle 时按 `runById` 失效。报告同时给出协议兼容性、per-class/support delta、dataset lineage。
- `Open Recorded Run…` 在 Studio designer 页按分页列出 open store 的 runs，展示 replay readiness、缺依赖、Created 证据语义；**Imported Created 永远不会显示为已执行**，也不抹掉当前 matrix 选择。

## 5. 便携 bundle 导入

- Import 对话框新增：有界分页的 target experiment 检索、fresh id / keep-original id 策略、relocation 目录、是否覆盖已存在文件、容量上限。
- Dry-run 的 run ID 明确标为 **CANDIDATE**（未提交）；真实导入结果显示 relocatedFiles / lineageEdgesReattached / idempotent / **Created evidence (imported, never executed here)**。
- 默认仍是「不覆盖 + fresh id + 同 execution fingerprint 幂等」；所有完整性/容量/事务/回滚仍由既有 importer 权威负责，UI 不重造解包器。

## 6. 教学：完成账本、草稿与反馈

- CompletionLedger 现在核对 `entry.labId == session.labId`、sessionId、courseId；有效 A session 被错接为 B 的条目会被降级并给出理由。两个课程共用同一 lab id 时各自保存、按课程投影进度；旧条目按**已复验会话**的课程迁移，升级不会清空真实完成证据。无 course 身份的 legacy 条目会被另一课程的新会话**另建条目**而非覆盖，其命名空间键可逆；按课程投影进度时不再把无课程身份的条目算进任意课程。产物漂移、缺 session、摘要不一致仍诚实降级。
- Guided Lab Workspace 的本地草稿按 `course / lab / stable step id` 持久化（原子写、有界读）。前进/后退、切 lab、提交手工证据、关闭/隐藏/重启都会保留正文；**草稿文件不可读时拒绝写盘**，保留原文件及其他 lab/course 的正文。规则文件 sha256 或产物路径变化会把恢复的草稿标为 stale 并说明原因；有效性复核只读，不覆盖正在编辑的正文。**自动保存草稿不等于完成或通过**；旧 `human:<stepId>:<length>` 引用保持可读，但明确标注「长度不能恢复正文」。
- 教师控制台 Feedback 页新增本地目的地与 `Deliver guarded feedback packs + summary locally`：
  - 只把 guard 通过、来自**真实批次报告**的 packs 写成本地文件；finding/score 不造，缺失就留空。指定但不可读/不可 hash 的批次报告会在创建任何目录之前拒绝交付。
  - 同时写 `class-summary.json` 与 `feedback-manifest.json`（批次报告 sha256、blocked students、written/conflicts/failures、与返回值一致的 `ok`/`partial`/`code`/`message`、`no_upload: true`）；报告 hash 为有界流式读取。
  - 默认不覆盖已有文件，冲突策略同样适用于班级汇总与 manifest；零写入的重复投递、单个学生失败、manifest 写入失败都留 truthful partial 状态且**绝不记为成功**。
  - **不上传、不发信、不自动交给学生**。

## 7. 本轮已知边界

- `Refresh Sensitivity` 与 spatial compare 仍基于完整 report 的 curves，不消费 matrix 的当前 filter/page；matrix、export CSV 与 metric cell 证据已共享同一过滤/身份语义。
- 大包校验/复制仍在既有 importer 的同步阶段执行；底层不支持即时取消的阶段不会伪造取消成功。
- release preflight 的课程/规则/数据快照 digest 由既有 `release_preflight` 权威产出；反馈交付使用真实批次报告文件 sha256 作为批次身份（`batch_report_digest`）。教师控制台传入的 `batch_config_digest` 目前是**同一批次报告文件的有界流式 sha256 别名**，不是 release preflight 的课程/规则快照 digest；二者不可混读。
- 草稿存储是单文档原子写，没有 last-good 代际；损坏文件只能保留并明确报错，不能自动恢复上一代。

## 8. 验证入口（本轨）

```
# 纯 Qt Core 叶子回归（会话/指标/分页/提交身份/账本/草稿/交付）
./build-dev/tests/test_r30_t07_core --reporter compact

# GUI 回归（真实 dock 控件、真实 store、真实 label schema）
QT_QPA_PLATFORM=offscreen ./build-dev/tests/test_r30_t07_gui --reporter compact

# 既有回归（必须保持绿）
ctest --test-dir build-dev -R "test_r29_t04_(studio_spec|studio_session|compare|ledger|steps)::" -j1 --output-on-failure
```
