# R25-B CROSS_TRACK_FINDINGS

## 交接（供其他轨消费的契约；本轨不改 D/C 轨文件）

### D 轨（src/app UI）——导入向导可消费
- `sicnu::data::ImportPreflight::inspect(path, manager, options)`（src/data/import_preflight.h）→ `ImportPreflightReport.toJson()`（schema `sicnu.import.preflight/1`）。
- 用法：导入对话框在 commit 前对每个候选文件调用一次，渲染 `findings[]`（code/severity/message/remediation）与 `recognition`/`raster`/`vector`/`identity`/`dedup` 块；`verdict != importable` 时用 finding 的 remediation 引导用户。
- 零副作用：不注册、不网络、不改目录；可安全在 UI 线程重复调用。
- 文档：docs/datasets/import-preflight.md。

### C 轨（Agent/MCP）——干跑可暴露
- `sicnu::operators::runtime::planModelInference(request)`（或 `(request, model)`）→ `InferencePlan.toJson()`（schema `sicnu.inference.plan/1`）：模型身份、runtime/device 判定、输入网格、tile/批量/工作集估计、输出模式×head 契约、EO 契约、全部 typed findings。
- `summarizeInferenceQuality(result)`（schema `sicnu.inference.quality/1`）：运行后质量摘要，CLI/Agent 可直接打印。
- 均只读：不 acquire session、不写输出。
- 文档：docs/inference/plan.md。

### 复现/实验侧（任何渲染复现差异的 UI）
- `ReplayDeviationReport.deviation_details[]`（schema_version=2）：dimension/domain(data|model|experiment|environment)/identity_pin/detail/remediation。v1 字段不变。
- 文档：docs/experiments/replay-deviation.md。

## 共享文件检查

- `src/geospatial/products/product_registry.{h,cpp}`：仅新增只读方法 `adaptersAccepting()`；`adapterFor/describe` 语义与排序不变；无既有调用者改动。
- `tests/CMakeLists.txt`：仅末尾追加 `# BEGIN R25-B` … `# END R25-B` 块（append-only），其中两个 OpenCV 目标有与 src/operators 相同的条件门。注册完整性门实测 1067 targets（base 1063 +4），lost 0、markers 0、duplicates 0。
- `.gitignore`：仅末尾追加本轨 `.planning/zcode-r25-B/*.md` allow-list（与 r22/r23/r24 同模式）。
- 其他 B 轨写区文件（src/data、src/operators/runtime、src/experiment）均为新增文件或既有文件的受控修改；未触碰 A/C/D/E 轨与 src/core、src/gui、external。

## 未解决的跨轨观察（非本轨阻塞）

- 无。若 R2 提出跨轨项，登记于此。
