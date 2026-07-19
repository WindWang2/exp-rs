# Classification Workflow UX + Post-Process + v1.1 设计

**日期:** 2026-07-19  
**Phase:** 10A.3（像素分类流程化与生产闭环）  
**状态:** 设计已确认，待写实现计划  
**前置:**
- Phase 10A 像素分类（`docs/superpowers/specs/2026-06-04-classification-pixel-design.md`）
- Phase 10A.1 polish（`docs/superpowers/specs/2026-06-04-classification-10a1-polish-design.md`）
- Classification v1.1 生产补强（`docs/superpowers/specs/2026-07-16-classification-v11-production-hardening-design.md`）— **本设计吸收并交付**

---

## 1. 目标与范围

### 1.1 问题

当前 `QgsClassificationMainWindow` 是多 dock + 工具栏的自由工作区：ROI、光谱/JM、ClassifierBar、精度对话框、导出分散存在，**没有可感知的实验流程**。教学 Lab 期望的路径是：

1. 建立分类体系  
2. 选择样本  
3. 样本评价  
4. 训练-分类  
5. 精度评定  
6. 分类后处理  
7. 输出  

其中 **后处理几乎缺失**；精度以一次性对话框为主；操作逻辑（训练/验证角色、预览 vs Apply、脏关闭、SVM 缩放等）有已知债务（见 v1.1）。

### 1.2 产品决策（已确认）

| 决策点 | 选择 |
|--------|------|
| 交互模式 | **C 混合**：默认 7 步向导 + 可切专家模式 |
| 步骤门禁 | **A 软引导**：可进任意步；缺前置则主操作禁用并提示 |
| 后处理深度 | **B 实验课完整套**：Sieve、多数滤波、Clump、重编码、矢量化 |
| 本轮范围 | **C 一次做完**：流程 UX + 后处理 + v1.1 生产补强（多 PR） |
| 壳层方案 | **A 顶栏 Stepper + 当前步侧栏**；画布始终可见 |

### 1.3 范围内（必须交付）

| ID | 项 | 说明 |
|----|-----|------|
| W1 | 工作流状态机 | `RsClassifyWorkflowController`：7 步、完成条件、软门禁文案、向导/专家模式 |
| W2 | 顶栏 Stepper | `RsClassifyStepperBar`：步骤指示、完成勾选、专家模式开关 |
| W3 | 步骤面板编排 | `StepPanelHost` + 每步任务面板；向导下突出当前步 |
| W4 | 精度内嵌 | 第 5 步面板展示 OA/Kappa/混淆矩阵；可导出 CSV；可重跑 |
| W5 | 后处理五算子 | analysis 层 + `RsPostProcessTask` + 第 6 步 UI |
| W6 | 输出步 | 导出清单（栅格/ROI/模型/精度/项目）+ 加载到主窗口图层树 |
| W7 | 操作逻辑优化 | 训练/验证高亮、Preview≠最终结果、后处理默认输入、防重复提交 |
| V1–V6 | v1.1 全项 | Scaler、ROI CRS、Hungarian、GTiff options、视口预览、Session dirty（见前置 spec，不重复发明） |

### 1.4 明确不在范围内

- Random Forest / Mahalanobis / UNet 新后端（继续灰显）
- OBIA / SLIC / SAM（Phase 10B）
- 硬顺序强制不可跳步
- 后处理网格搜参 / 复杂拓扑清理
- 将主应用图层树与分类窗深度合并重构
- 完整拆分 `qgsclassificationmainwindow.cpp` 为多文件（本轮仅 **新增** 工作流/后处理文件；主窗口只接线，允许适度增长）

### 1.5 完成标准

1. 相关 ctest 全绿（v1.1 + 后处理 + 工作流纯函数/状态机）  
2. 手工 Lab 主路径（向导）：开图 → 体系 → 样本 → 评价 → SVM/NB 训练 → Apply → 精度面板 → Sieve/滤波 → 导出 → 脏关闭  
3. 专家模式：全部 dock/工具可用，步骤条仅作进度  
4. 软门禁：跳到「训练」无样本时 Apply 禁用并提示  

---

## 2. 架构

### 2.1 顶层结构

```
QgsClassificationMainWindow
├── RsClassifyWorkflowController     ← 步骤 / 完成条件 / 软门禁 / 模式
├── RsClassifyStepperBar (顶)        ← 7 步 + 专家模式
├── QgsMapCanvas (中)                ← 始终可见
├── StepPanelHost (右，主)           ← 按 step 切换任务面板
│   ├── Step1 ClassSystemPanel
│   ├── Step2 SamplesPanel
│   ├── Step3 EvaluatePanel
│   ├── Step4 TrainClassifyPanel     ← 内嵌/迁入 ClassifierBar 控件
│   ├── Step5 AccuracyPanel
│   ├── Step6 PostProcessPanel
│   └── Step7 ExportPanel
├── Expert docks（专家模式全开；向导模式可按步折叠）
│   ├── 类别管理 / 类别快览
│   ├── JM 分离度 / 光谱曲线
│   └──（可选保留底栏 Classifier 作为专家快捷入口）
└── 数据 / 任务层
    ├── RsClassifySessionState       ← v1.1
    ├── RsFeatureScaler              ← v1.1（部分可能已存在，需接线）
    ├── RsClassificationProject      ← 扩展：当前步、产物路径、精度快照
    ├── RsClassificationTask         ← v1.1 Config 扩展
    └── RsPostProcess* + Task        ← 新
```

### 2.2 依赖方向

- `RsClassifyWorkflowController` → Qt Core only（无 GDAL/OpenCV）；便于单测  
- Step 面板 → 主窗口 signals/slots；不直接拥有 Task  
- `RsPostProcess*` → GDAL/OpenCV + 标签栅格路径；不依赖 GUI  
- v1.1 组件保持前置 spec 的分层  

### 2.3 新文件（建议）

| 路径 | 职责 |
|------|------|
| `src/app/classification/rs_classify_workflow_controller.{h,cpp}` | 步骤枚举、完成位、门禁、模式 |
| `src/app/classification/rs_classify_stepper_bar.{h,cpp}` | 顶栏 UI |
| `src/app/classification/rs_classify_step_panels.{h,cpp}` | StepPanelHost + 各步轻量面板（可单文件起步，过大再拆） |
| `src/app/classification/rs_classify_session_state.{h,cpp}` | v1.1 脏关闭 + QSettings |
| `src/app/classification/rs_accuracy_panel.{h,cpp}` | 内嵌精度（可从 dialog 抽共享模型） |
| `src/analysis/classification/rs_post_process.{h,cpp}` | Sieve/多数/Clump/重编码 纯函数或薄封装 |
| `src/app/classification/rs_post_process_task.{h,cpp}` | QgsTask 链 |
| `tests/test_classify_workflow_controller.cpp` | 门禁与完成条件 |
| `tests/test_post_process.cpp` | 合成标签图算子 |
| v1.1 测试 | 见前置 spec §6 |

### 2.4 修改的现有文件

| 路径 | 变更 |
|------|------|
| `qgsclassificationmainwindow.{h,cpp}` | 挂 Stepper/Controller；接线 7 步；模式切换 dock 显隐；Preview/Apply 与完成态 |
| `rs_classification_task.{h,cpp}` | v1.1 Config（scaler、creationOptions、pixel window） |
| `rs_roi_io.{h,cpp}` | 源 CRS 默认 |
| `rs_hungarian_assignment.{h,cpp}` | 矩形安全 pad |
| `rs_accuracy_dialog.*` / 评估结果结构 | 与 AccuracyPanel 共享数据模型 |
| `rs_classification_project.*` | 持久化：workflowStep、result paths、accuracy summary |
| CMakeLists（analysis / app_classify / tests） | 注册源与测试 |

---

## 3. 工作流状态机

### 3.1 步骤枚举

```cpp
enum class RsClassifyStep {
  ClassSystem = 0,  // 1 建立分类体系
  Samples,          // 2 选择样本
  Evaluate,         // 3 样本评价
  TrainClassify,    // 4 训练-分类
  Accuracy,         // 5 精度评定
  PostProcess,      // 6 分类后处理
  Export,           // 7 输出
  Count
};
```

### 3.2 完成条件

| Step | 完成（打勾）条件 |
|------|------------------|
| ClassSystem | 类定义 ≥ 2，且每类有非空名称与有效颜色 |
| Samples | 训练层中 ≥ 2 个不同 classId 有像元（或要素）；建议阈值 UI 提示但不强制完成态 |
| Evaluate | **仅**用户点击「标记已审阅」→ 完成（自动算过 JM/光谱不算完成，避免误勾） |
| TrainClassify | 存在有效 **全图 Apply** 分类栅格路径（Preview 临时层 **不** 计完成） |
| Accuracy | 存在最近一次有效 `RsAccuracyMetrics`（来自 Apply 的 hold-out 或基于验证层重算） |
| PostProcess | **可选步**：用户点「跳过」或存在后处理输出路径 → 完成 |
| Export | 本会话至少一次成功导出或「加载到主图」 |

### 3.3 软门禁（主操作可用条件）

任意步可导航。下列 **主操作** 在条件不满足时 `setEnabled(false)` + 面板/statusBar 提示「还需：…」：

| 主操作 | 需要 |
|--------|------|
| 数字化样本 / 魔棒 | 源影像已开 + 当前类有效 |
| 计算 JM / 光谱刷新 | 源影像 + 训练像元 |
| CV / 预览 / Apply | 源影像 +（监督）训练像元 ≥ 10；KMeans 沿用现规则 |
| 写入精度面板 / 导出矩阵 | 有 metrics 或可重算的验证来源 |
| 后处理运行 | 存在分类栅格输入路径 |
| 输出清单执行 | 存在最终栅格（分类或后处理） |

**不阻止** 打开后续步骤页浏览说明与禁用态按钮。

### 3.4 向导 vs 专家模式

| | 向导（默认） | 专家 |
|--|-------------|------|
| Stepper | 显示，驱动 StepPanelHost | 显示，仅进度；点击仍可切面板 |
| Docks | 按当前步推荐显示/折叠 | 全部显示（用户布局可记 QSettings） |
| 工具栏 | 与当前步相关工具优先 | 全部样本/分类工具 |
| 门禁 | 同上软门禁 | 同上软门禁（不因专家而跳过安全检查） |

模式键：`Classification/workflowMode` = `wizard` | `expert`。  
当前步键：`Classification/lastStep`；并写入项目文件。

---

## 4. 各步 UI 与操作逻辑

### 4.1 布局

```
MenuBar
────────────────────────────────────────────────
[Stepper: 1…7]                    [专家模式 开关]
────────────────────────────────────────────────
│ 画布（源影像 + 样本层 + 可选结果叠加） │ 当前步面板 │
│                                        │ 完成条件   │
│                                        │ 主操作     │
│                                        │ [上一步][下一步] │
────────────────────────────────────────────────
StatusBar: CRS | 训练/验证计数 | 当前类 | 项目脏 | 门禁提示
```

### 4.2 逐步职责

**1 分类体系**

- 类别表 CRUD（复用 `RsClassTableWidget` 嵌入或联动）
- 默认 6 类模板（现有林地/草地/水体/建成区/耕地/裸地）
- 明显入口：打开源影像；可选「从项目加载类」
- 不要求先开图再建类（可逆序）

**2 选择样本**

- 训练 / 验证层切换（工具栏互斥高亮 + 状态栏文案）
- 面编辑 / 节点 / 选择 / 移动 / 删除 / 魔棒（复用现工具）
- 每类像元/要素统计；类别间样本不均衡弱提示（不阻断）
- 未选类禁止数字化（保留现状）
- 加载 / 导出 ROI（CRS 走 v1.1）

**3 样本评价**

- 光谱曲线 + JM 矩阵同屏（dock 或面板内嵌）
- JM 低于阈值（如 &lt; 1.0）红标，建议返回样本步
- 按钮「标记已审阅」→ 完成 Evaluate

**4 训练-分类**

- 分类器选择、波段、训练比例、分层、CV、加载模型
- **Quick preview** 与 **Apply classification** 分区：
  - Preview：视口裁剪（v1.1）；不设置「最终分类路径」；不强制弹精度
  - Apply：全图；写输出路径；更新 workflow 产物；可触发精度计算
- 运行中禁用重复提交；任务 cancel 可追踪

**5 精度评定**

- 内嵌 `RsAccuracyPanel`：混淆矩阵、OA、Kappa、per-class P/R/F1
- 导出 CSV
- 验证来源记录：`valid_layer` | `holdout_split` + 比例
- 可在更换验证样本后「重新评估」（需分类结果 + 标签来源）
- 现有 `RsAccuracyDialog`：可保留为「弹出大图」或逐步弃用为 thin wrapper

**6 分类后处理**

- 输入默认 = 最近 Apply 路径；可浏览替换
- 算子勾选链（顺序固定，见 §5）：Sieve → 多数滤波 → Clump → 重编码 → 矢量化
- 每步可「仅运行到此」预览叠加；写出中间/最终 GeoTIFF
- 「跳过后处理」标记完成并进入输出（最终栅格 = 分类结果）

**7 输出**

- 勾选清单：分类 GeoTIFF、后处理结果、ROI、模型(+scale.json)、精度报告、分类项目
- 「导出所选」「加载到主窗口图层树」
- 至少一次成功操作 → Export 完成

### 4.3 横切操作逻辑

1. **Dirty：** 类定义 / ROI / 参数 / 未保存项目变更 → `RsClassifySessionState::markDirty`；关闭提示 Save/Discard/Cancel  
2. **Preview ≠ 完成：** 仅 Apply（或用户在输出步确认的路径）推进 TrainClassify 完成态  
3. **精度可复现：** metrics 与验证来源一并写入项目/会话  
4. **后处理输入：** 默认最终分类；用户改选后更新 controller 的 `classifiedRasterPath`  
5. **中文/英文 UI：** 步骤名与门禁提示中文优先（对齐现有分类窗中文文案），菜单可保持双语现状不强制本轮统一  

---

## 5. 后处理算法

### 5.1 算子

| 算子 | 实现 | 参数 | 输出 |
|------|------|------|------|
| Sieve | GDAL `GDALSieveFilter`（或等价） | `threshold` 像元；`connectedness` 4/8 | 标签 GeoTIFF |
| 多数滤波 | 整数标签滑动窗口众数 | `kernel` ∈ {3,5,7}；nodata 忽略 | 标签 GeoTIFF |
| Clump | 同值连通分量标记 | 4/8 连通 | 分量 ID 栅格（属性可映射回 class） |
| 重编码 | 查找表 old→new | 映射表；同步 ColorTable | 标签 GeoTIFF |
| 矢量化 | GDAL Polygonize | 输出 path（GPKG/SHP）；字段 `class_id` | 矢量图层 |

### 5.2 任务封装

```cpp
struct RsPostProcessConfig {
  QString inputPath;
  QString outputRasterPath;       // 链终点栅格（矢量化前）
  QString outputVectorPath;       // 可选
  bool runSieve = false;
  int sieveThreshold = 10;
  int connectedness = 8;
  bool runMajority = false;
  int majorityKernel = 3;
  bool runClump = false;
  bool runRecode = false;
  QMap<int,int> recodeMap;
  bool runPolygonize = false;
  QStringList creationOptions;    // 默认同 v1.1 GTiff
};
```

`RsPostProcessTask::run`：按固定顺序执行已启用算子；中间结果可用临时文件；失败返回 `errorMessage`；成功后主窗口加载结果层并更新 workflow。

### 5.3 ColorTable

- 从输入分类栅格复制 ColorTable  
- 重编码后按 new id 重写表项  
- Clump 输出若为分量 ID，预览可用随机色或保留「类色」双模式；**默认**：Clump 后若无类映射则伪彩色，UI 说明清楚  

---

## 6. v1.1 生产补强（吸收）

以下条款 **以** `2026-07-16-classification-v11-production-hardening-design.md` **为准**，本设计仅声明交付绑定，不另立冲突规则：

| ID | 项 |
|----|-----|
| P1 | `RsFeatureScaler` z-score；split 后 fit train；tile transform；`model.scale.json` |
| P2 | ROI save/load 默认源影像 CRS |
| P3 | Hungarian 非方阵安全 pad |
| P4 | GTiff `TILED=YES,COMPRESS=DEFLATE,PREDICTOR=2`，失败回退 |
| P5 | 视口 → `PixelWindow` 裁剪预览 |
| P6 | `RsClassifySessionState` dirty + 窗口/快照 settings |

**与本设计的交叉点：**

- P5 仅服务 Step4 Preview；不推进完成态  
- P1 在 Step4 训练路径强制接线（SVM/NB/KMeans 一致）  
- P6 与 workflow dirty 共用同一 session 对象  
- Apply 后 metrics → 填入 Step5 面板（替代「仅弹窗」为默认路径）

若仓库中 `rs_feature_scaler` 等文件已部分落地，PR1 以 **接线 + 单测补齐** 为主，避免重复实现。

---

## 7. 项目持久化扩展

`RsClassificationProjectData`（或等价）增加：

```text
workflowStep: int
workflowMode: wizard|expert
classifiedRasterPath: string
postProcessRasterPath: string
postProcessVectorPath: string
accuracy: { oa, kappa, source: holdout|valid_layer, ... }
evaluateReviewed: bool
```

加载项目时恢复步、路径与面板状态；缺失字段兼容旧项目。

---

## 8. 错误处理

| 场景 | 行为 |
|------|------|
| 跳步无前置点主操作 | 按钮禁用 +「还需：…」 |
| 训练样本 &lt; 10 | 拒绝训练（现状） |
| Preview 视口不相交 | 拒绝 + 提示（v1.1） |
| 后处理输入不可读 | Task 失败，messageBar |
| Sieve threshold ≤ 0 | UI 校验拒绝 |
| 重编码映射空且勾选 | 拒绝或 no-op 并警告 |
| Polygonize 失败 | 栅格链仍可成功；矢量项报错 |
| dirty 关闭 Save 失败 | 不关闭窗口 |
| scale.json 损坏 | 不缩放 + 警告（v1.1） |

---

## 9. 测试计划

| 测试 | 断言 |
|------|------|
| `test_classify_workflow_controller` | 完成位、软门禁、跳过 PostProcess、Preview 不完成 Train |
| `test_feature_scaler` 等 | v1.1 §6 全表 |
| `test_post_process` | 合成 3 类图：Sieve 去碎斑；majority 平滑；recode 映射；polygonize 多边形数 &gt; 0 |
| `test_classification_e2e` | Apply 输出存在；可选压缩元数据 |
| `test_classify_session_state` | dirty + snapshot |
| 手工 | §1.5 Lab 路径 + 专家模式切换不丢 ROI |

---

## 10. PR 拆分与实现顺序

| PR | 内容 | 依赖 |
|----|------|------|
| **PR1** | v1.1 数据层：Scaler 接线、ROI CRS、Hungarian、GTiff、preview window、session | 无 |
| **PR2** | 工作流壳：Controller + Stepper + StepPanelHost + 7 步骨架 + 软门禁 + 专家模式；现有功能迁入对应步 | PR1 建议先合，可弱依赖 |
| **PR3** | 后处理五算子 + Task + Step6 UI | PR2（面板宿主） |
| **PR4** | Accuracy 内嵌 + Step7 输出 + 项目字段 + 操作逻辑 review 收尾 + Lab 走查 | PR2–PR3 |

每 PR 独立可测；禁止单 PR 混合大 UI 与全部算法。

---

## 11. 风险与缓解

| 风险 | 缓解 |
|------|------|
| `qgsclassificationmainwindow.cpp` 已很大（~2k+ 行） | 新逻辑放新文件；主窗口只 connect |
| 向导/专家 dock 布局抖动 | 保存两套 window state；切换 restore |
| Clump 语义与「类图」混淆 | UI 文案区分分量图 vs 类图；默认链可选不启用 Clump |
| 范围 C 过大 | 严格 4 PR；PR1/PR3 可并行于不同 worktree |
| 精度从 dialog 迁面板回归 | 共享 metrics 模型；保留 dialog 导出路径 |

---

## 12. 与旧 spec 关系

- **10A / 10A.1：** 功能基线，本设计不回退分类器与 ROI 能力  
- **v1.1：** 全文吸收为 PR1 交付物；若与本文冲突（例如精度仅弹窗），**以本文「内嵌为主」为准**  
- **10B OBIA：** 不受影响；未来可复用 Stepper 模式  

---

## 13. 开放实现细节（计划阶段再定，不阻塞设计）

- Step 面板用 `QStackedWidget` 单宿主 vs 多个 `QDockWidget` 按步 show/hide：实现计划默认 **QStackedWidget 右侧面板** + 专家模式额外 docks  
- 多数滤波自写 vs `cv::medianBlur` 仅适用于单通道有序值：标签图必须用 **众数**，不用中值模糊冒充  
- 矢量化默认 GPKG（单文件）优于 SHP（多文件）；UI 默认 `.gpkg`
