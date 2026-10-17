# R15-E Oracle 定义（每个工作包的 RED→GREEN 判据）

> 规则：能作负测试的工作包，先写 Catch2 断言证明旧版失败（RED），再最小修复（GREEN），
> 关键测试连跑两次。不适合 RED 的（静态覆盖/审计类）改为给出可复核证据，不伪造 RED。

## 通用规则

- 测试环境：`QT_QPA_PLATFORM=offscreen`，`CTEST_PARALLEL_LEVEL=1`
- 每个修复必须给出：旧失败输入 → 精确症状（哪个控件/哪条断言）→ 新期望 → 退出码
- 断言必须能从代码独立推导，不得依赖日志散文

---

## E05 · 异步任务面板真实性

**Oracle：** 失败/取消不会显示绿色完成；未存在的产物不可点击。

### E05-1（已复现，STATIC_CERTAIN）产物存在性未校验

| 项 | 内容 |
|---|---|
| 旧失败输入 | result JSON = `{"output":"<QTemporaryDir>/missing.tif"}`（文件从未写出）；或历史条目 outputPaths 指向已被删除的文件 |
| 精确症状 | A) `RsResultSummary::rebuildUi()`（`src/app/widgets/rs_result_summary.cpp:246-258`）把路径直接建成 `QListWidgetItem`，`itemDoubleClicked` 无条件 `emit openPathRequested(p)` → 主窗口 `loadRasterLayer(不存在路径)`。<br>B) `ProcessingHistoryPanel`（`src/app/workbench/processing_history_panel.cpp:276`）`m_openBtn->setEnabled(outputPaths.size()>=1)`，只看列表非空不看文件是否存在；`openSelectedOutput()` 取 `outputPaths.first()`（可能不存在而第 2 个存在） |
| 旧断言（RED） | `test_r15_E_result_artifact_honesty.cpp`：<br>1. 缺失路径的 item 必须 `!(flags() & Qt::ItemIsEnabled)`，双击后 `openRequests == 0`<br>2. 存在路径的 item 必须 enabled 且双击 `openRequests == 1`<br>3. 历史面板 `openBtn` 在唯一产物缺失时 `!isEnabled()`<br>4. `openSelectedOutput` 跳过缺失项、取第一个存在项 |
| 新期望 | 缺失产物：禁用 + tooltip 说明「输出文件不存在或未生成」+ 不发出信号；存在产物：行为不变 |
| 兼容性负面测试 | 存在路径的双击打开行为必须逐字保持（`test_rs_result_summary.cpp` 首用例改为真实临时文件后仍绿） |

### E05-2（静态确证）状态行是否无条件绿色
`RsResultSummary::rebuildUi()` 原本硬编码 `✓ Processing finished`，不看 result 自带 `status`。
当前 `WorkflowSessionController` 仅在成功分支调用 `showResult`（`src/app/shell/workflow_session_controller.cpp:643`），
所以主要危害来自**上一次运行的成功卡残留**（E05-3）。本轨同时让组件自身按 result 的 `status` 选择
`✓`/`✗` 与 `state` 属性，使它独立于调用方也诚实。

### E05-3（已复现）成功卡残留 / 失败态显示百分比 / 隐藏条目双计
| 项 | 内容 |
|---|---|
| 旧失败输入 | ① 先用参数 A 跑成功（结果卡出现），再用参数 B 跑到失败/取消；② 任务在 50% 处 Failed/Canceled；③ 根级隐藏文件 `.leaked_answer.tif` |
| 精确症状 | ① 红色错误行与绿色 `✓ Processing finished` + 可点击旧产物同屏；② 列表列显示 `Failed \| 50%`；③ 隐藏条目被「学生目录 pass」与「根级扁平 pass」**各记一次**（一次 studentId 错填成文件名、一次为空） |
| 新期望 | ① `setRunning(true)` / `setFailed()` 清并隐藏结果卡（回归测试 `test_r15_E_task_panel_state`，§L01）② 列表/列视图对终态失败返回 `—`，详情面板保留观测值（§L05，`test_r15_E_progress_text`）③ 隐藏目录只在目录 pass 记账、隐藏文件只在扁平 pass 记账，`skipCount(path) == 1`（§L03/L04） |
| 依据 | 模块自述不变量（`batch_assessment.h`：refusals "are ACCOUNTED, never a silent vanish"）+ oracle「未存在的产物不可点击」「失败/取消不显示绿色完成」|

---

## E06/E07 · 大表与 Experiment Studio

**Oracle：** 大数据分页/虚拟化真实 bounded；10 万条不会一次渲染所有 widget；筛选统计不得误用屏幕行。

| 项 | 现状（master `46791bc65`） | 结论 |
|---|---|---|
| `ExperimentStudioDock::rebuildMatrixTable()` 显示上限 | `kMaxMatrixDisplayRows = 500`（`src/app/experiment_studio/experiment_studio_dock.cpp:441`），超出时状态栏追加「(preview limited to first 500 of N rows)」 | **FIXED_ON_MASTER（#1510）**，不重写 QAbstractTableModel |
| 统计是否用屏幕行 | `projectRunMatrix(report, filter)` 返回的 `vm.totalPoints/recordedCount/failedCount/...` 在 `setRowCount` 之前算出，来自完整 report | **无缺陷** |
| 待验证 | 10k / 100k 记录的 `rebuildMatrixTable` 墙钟时间与 RSS | 走 E16 实测，不凭空断言 |

---

## E08/E09 · 教学闭环 / 课堂异常恢复

**Oracle：** 离线一班多份作业无丢失、跳过原因明确、评分统计真实；破损报告必须 fail-closed。

### E08-1（已复现，STATIC_CERTAIN）隐藏/嵌套条目静默蒸发
| 项 | 内容 |
|---|---|
| 旧失败输入 | `submissions/.张三/hw.tif`（隐藏学生目录）；`submissions/王五/.hw.tif`；`submissions/2021级/孙七/hw.tif` |
| 精确症状 | 三处裸 `continue`（`batch_assessment.cpp` 学生目录层 / 学生目录内层 / 根级扁平层）→ 该学生不出现在 `items`、`report.skippedFiles`、`report.total`、checkpoint 中任何一处。控制台对 6 人班报「graded 5 / 5」。 |
| 旧断言（RED） | `test_r15_E_batch_refusal_accounting` 用例 1–3：断言 `skips` 中出现对应路径且 reason 含 `hidden` / `nested` |
| 新期望 | 三类拒绝都成为带 reason 的 typed `DiscoverySkip`；根级普通文件仍交给扁平 pass（不重复记账） |
| 依据 | 模块自身不变量（`batch_assessment.h`：refusals "are ACCOUNTED, never a silent vanish"） |

### E08-2（已复现，STATIC_CERTAIN）损坏批改报告被渲染成合法空汇总
| 项 | 内容 |
|---|---|
| 旧失败输入 | `<prefix>.json` 被截断、或写入的是任意其它 JSON 对象 |
| 精确症状 | `QJsonDocument::fromJson(...).object()` 无 parseError 检查 → `total=0`、`rows` 空 → 仍然调用 `buildClassSummary()`，产出格式完全合法的 `{"total":0,"completion_rate":0,...}`，教师看到「这个班真的没作业」 |
| 旧断言（RED） | 用例 4–5：截断 JSON / 非 batch JSON 必须返回 false 且 error 非空 |
| 新期望 | 新增 `sicnu::teaching_admin::loadBatchReportFromFile()`：bounded read(64 MiB) + parse error + `rows` 数组 + `total` 一致性告警；控制台两处改用它 |
| 兼容性负面测试 | 用例 6–7：真实报告 round-trip 仍成功；`total` 与行数不一致时**报出**而不是静默替换 |

### E09-1（已复现，RED 实锤）lab pack 解析异常逃逸
| 项 | 内容 |
|---|---|
| 旧失败输入 | `[[[[…`（5000 层嵌套） |
| 精确症状（RED 输出） | `due to unexpected exception with message: Exceeded stackLimit in readValue().` —— 异常从 `PackVerifier::loadFromBytes()` 逃逸成进程级失败，而不是声明中的 typed `lab.pack_schema` 拒绝 |
| 新期望 | 8 MiB 字节上限 + `stackLimit=64` + `try/catch(Json::Exception)` → typed failure |
| 状态 | ✅ GREEN（3 cases / 8 assertions） |

## E10 · 帮助覆盖

### E10-1（已复现）`algorithm.keyParameters` 与算子 schema 漂移
| 项 | 内容 |
|---|---|
| 检测方法 | 全量比对：31 个含 `keyParameters` 的帮助条目 / 84 个参数名 vs `docs/generated/help/parameters.md`（由活跃算子注册表生成） |
| 命中 | `rs:pca` → `componentCount`（真名 `numComponents`，见 `rs_pca_operator.cpp:24`）；`rs:band_math` → `bands`（该算子只有 `expression`，见 `rs_band_math_operator.cpp:22-24`）；`rs:morphology` → `window`（morphology 走 `connectivity/op/iterations`，`window` 属 windowFamily，见 `rs_raster_spatial_operators.cpp:480-499` + `RsMorphologyOperator::schema()`） |
| 备注 | 子代理另报 `rs:atmospheric_correction.band` 与 `rs:mosaic.inputs` 漂移，**逐条复核后证伪**（源码中确实存在该名），未做修改 |
| 新期望 | 修正 3 处 JSON；`operator_help_provider.cpp` 增加 keyParameters ⊆ schema 参数集的一致性校验，走既有 `report()` 通道 |
| 复验 | 同一脚本 drift = 0 |

## E01–E04 / E11–E17
以审计证据推进；无实证者列入 `NOT_REPRODUCIBLE` / `OUT_OF_SCOPE`，不做推测性修复。

