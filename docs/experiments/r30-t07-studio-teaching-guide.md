# R30-T07 用户操作指南：Studio 设计、可复现交付与教学证据

本页说明桌面端「Experiment Exploration Studio」「实验比较面板」「教学 Lab Cockpit」「教师控制台」在 R30-T07 之后的实际行为。所有结论都来自当前代码的真实路径；不包含未接通的功能。

## 1. Studio 会话：保存失败不会损坏旧记录

- 会话文档位置：AppData `/experiment_studio/studio_session.json`，另有 `.last-good` 冗余副本。
- 写入通过仓内 durable sidecar：临时文件 → fsync → 原子 rename → 回读校验 → last-good 轮转。**任何一步失败，旧文档保持字节不变**，designer 摘要显示 typed 错误码，并在技术详情日志中追加原因。
- 读取有 4 MiB 上限。超界、损坏 JSON、未来版本 schema、非法数值字段都有明确拒绝；主文档语义损坏但 `.last-good` 完好时，会明确提示「Recovered from the last good session copy」。
- 恢复 spinbox 时按字段真实契约校验（有限、整数、在范围内）；非法字段跳过并列入摘要原因，**不静默夹紧成另一个设计**。旧版单文件会话（无 policy、无 store refs）照常恢复。
- 打开**同一个** experiment store 不会抹掉刚恢复的报告/refs；换 store 时按身份隔离清除，并在日志说明原因。

## 2. 完整设计与导出身份

- Designer 页新增 **Advanced spec (JSON)** 编辑器：
  - `Load Fields To Spec` 把当前表单导出为 `ParameterStudySpec` v1 JSON；
  - `Apply Full Spec` 用严格的 `ParameterStudySpec::fromJson` 解析并复用真实 registry schema 的 designer validator。支持多个 **flat numeric** dimension、baseParameters、多个 metric/objective、seed/预算/空间选项。
  - nested 路径（含 `.` `/` `[`）、非 numeric sweep、未知字段、预算越界都会明确拒绝；v1 不扩展新实验语言。
- 任何表单字段编辑都会使旧的「Valid design」失效（`Design changed — re-validate…`），内存与磁盘 draft 同时清空。
- 运行时的**提交快照**（submission id、规范化 spec、spec digest、designer VM、store path）被冻结。过期回调不会覆盖新会话；report 与 export bundle 显示的是**真正提交的那一版设计**，不是运行期间新敲的草稿。
- Export 按钮写出的 bundle 额外带 `submission` 与 `metric_cells` 可选字段；旧 bundle 仍可离线读回。报告与设计 digest 不一致时 bundle 记录明确的 identity mismatch issue。

## 3. Run Matrix：真实指标与有界浏览

- 表头显示所选 metric 名与显示协议（`study/<algorithm>/<strategy>`）。
- 所选 metric 未记录时显示 `— (metric not recorded)`；null / per-class object / array / 字符串 / 布尔 / 非有限值各有独立状态，**绝不回退显示别的 metric 的数值，也不会把缺失写成 0**。真实测量值 0 才显示 `0`。
- 每页 500 行；`Previous Page` / `Next Page` + 页码可浏览 >500 点的报告，filter 变更回到第 0 页。选择按 `pointId#replicateIndex` 记忆，翻页不丢失。

## 4. 实验比较：schema 证据与跨页 pins

- 比较报告新增 `schema_evidence`：从 `run → datasetVersionId → manifest.label_schema → DatasetStore::labelSchema` 解析**真实** label schema，调用 `compareLabelSchemas` 并把结果传给 `pairedRunComparison`。
- 拿不到 schema 时明确 `checked: false` / `not_checked_missing_evidence`，并把 `paired_summary.schemas_compatible` 写为 `null`；新增权威字段 `comparable_with_schema_evidence` 此时为 false。旧的 `comparable` 字段语义冻结，供已合入的消费者继续使用。
- **Pin A / Pin B / Clear Pins** 显式固定两个 run ID，跨页/搜索/重建后仍按 ID 解析；换 store、换实验、记录被删除都会清空 pins。报告同时给出协议兼容性、per-class/support delta、dataset lineage。
- `Open Recorded Run…` 在 Studio designer 页按分页列出 open store 的 runs，展示 replay readiness、缺依赖、Created 证据语义；**Imported Created 永远不会显示为已执行**，也不抹掉当前 matrix 选择。

## 5. 便携 bundle 导入

- Import 对话框新增：有界分页的 target experiment 检索、fresh id / keep-original id 策略、relocation 目录、是否覆盖已存在文件、容量上限。
- Dry-run 的 run ID 明确标为 **CANDIDATE**（未提交）；真实导入结果显示 relocatedFiles / lineageEdgesReattached / idempotent / **Created evidence (imported, never executed here)**。
- 默认仍是「不覆盖 + fresh id + 同 execution fingerprint 幂等」；所有完整性/容量/事务/回滚仍由既有 importer 权威负责，UI 不重造解包器。

## 6. 教学：完成账本、草稿与反馈

- CompletionLedger 现在核对 `entry.labId == session.labId`、sessionId、courseId；有效 A session 被错接为 B 的条目会被降级并给出理由。两个课程共用同一 lab id 时各自保存、按课程投影进度；旧条目按**已复验会话**的课程迁移，升级不会清空真实完成证据。产物漂移、缺 session、摘要不一致仍诚实降级。
- Guided Lab Workspace 的本地草稿按 `course / lab / stable step id` 持久化（原子写、有界读）。前进/后退、切 lab、提交手工证据、关闭重启都会保留正文；规则文件 sha256 或产物路径变化会把恢复的草稿标为 stale 并说明原因。**自动保存草稿不等于完成或通过**；旧 `human:<stepId>:<length>` 引用保持可读，但明确标注「长度不能恢复正文」。
- 教师控制台 Feedback 页新增本地目的地与 `Deliver guarded feedback packs + summary locally`：
  - 只把 guard 通过、来自**真实批次报告**的 packs 写成本地文件；finding/score 不造，缺失就留空。
  - 同时写 `class-summary.json` 与 `feedback-manifest.json`（批次报告 sha256、blocked students、written/conflicts/failures、partial 状态、`no_upload: true`）。
  - 默认不覆盖已有文件（冲突显式列出）；单个学生失败留 truthful partial manifest，绝不记为成功。
  - **不上传、不发信、不自动交给学生**。

## 7. 本轮已知边界

- `Refresh Sensitivity` 与 spatial compare 仍基于完整 report 的 curves，不消费 matrix 的当前 filter/page；matrix、export CSV 与 metric cell 证据已共享同一过滤/身份语义。
- 大包校验/复制仍在既有 importer 的同步阶段执行；底层不支持即时取消的阶段不会伪造取消成功。
- release preflight 的课程/规则/数据快照 digest 由既有 `release_preflight` 权威产出；反馈交付使用真实批次报告文件 sha256 作为批次身份。

## 8. 验证入口（本轨）

```
# 纯 Qt Core 叶子回归（会话/指标/分页/提交身份/账本/草稿/交付）
./build-dev/tests/test_r30_t07_core --reporter compact

# GUI 回归（真实 dock 控件、真实 store、真实 label schema）
QT_QPA_PLATFORM=offscreen ./build-dev/tests/test_r30_t07_gui --reporter compact

# 既有回归（必须保持绿）
ctest --test-dir build-dev -R "test_r29_t04_(studio_spec|studio_session|compare|ledger|steps)::" -j1 --output-on-failure
```
