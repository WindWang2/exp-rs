# DECISIONS — Classification & Object Intelligence 11.0

 autonomy=full 下所有裁决记录于此（格式：问题 → 候选 → 采纳 → 理由）。

## D-001 preset 名
- 问题：GOAL 写 `CMakePresets.json/build-dev`，实际 preset 名为 `dev-default`
  （binaryDir=`${sourceDir}/build-dev`）。
- 候选：a) 找名为 build-dev 的 preset；b) 用 dev-default；c) 手写 cmake -S -B。
- 采纳：b。`cmake --preset dev-default`，构建目录 `build-dev`，满足 GOAL 本意。
- 附带：worktree 首配时 pybind11/catch2 FetchContent 因 GitHub TLS 间歇故障
  克隆失败；为离线确定性，用 `-DFETCHCONTENT_SOURCE_DIR_PYBIND11/CATCH2`
  指向主仓已 populated 的 `_deps/*-src`（与 master 相同 commit，不引入版本漂移）。
  该 flag 只影响本地构建命令，不改仓库文件。

## D-002 不写 ADR，写 docs/processing/classification-intelligence.md
- 问题：设计记录放 `docs/adr/0158-*` 会与 open PR #1008 的
  `docs/adr/0158-radiometric-physics-state-system.md` 产生编号冲突。
- 候选：a) 抢 0158；b) 抢 0159；c) 不写 ADR，设计契约放
  docs/processing/classification-intelligence.md + 本文件。
- 采纳：c。ADR 编号是共享资源，#1008 先占 0158；本 track 避免号段竞争，
  采纳最小冲突方案。若 repo 规则强制 ADR，合并时由 reviewer 裁定补号（follow-up）。

## D-003 概率列序（class order）唯一权威
- 问题：RF/MLP 已用"升序去重训练标签"持久化 `<model>.labels.json`；
  NormalBayes 未持久化；无共享 helper。
- 候选：a) 各后端继续各自实现，仅补 NB；b) 新 `rs_class_order.h` 共享
  sorted-class-ids 权威 + JSON 序列化，NB 复用。
- 采纳：b（并保留 RF/MLP 现有旁车文件格式不变——它们的格式即
  {version, classIds[]}，helper 的 JSON 与之兼容；RF/MLP 代码不动，避免
  无谓 churn）。Oracle 2 的机器可验证锚点 = helper 的单测 + 各产出方列序断言。

## D-004 校准器形态：独立模块，不改后端训练
- 问题：概率校准放哪？
- 候选：a) 后端内部 holdout 自校准（fit 时切分内部校准集）；b) 独立
  `RsProbabilityCalibrator`（输入已产生的分数/概率 + 标签，输出可序列化
  校准参数），pipeline 侧可选应用。
- 采纳：b。理由：不改变任何现有后端训练行为/成本（最兼容）；D19 dataset
  域已可提供 holdout；校准参数进 sidecar 即可重放（WP-G）。a 改变所有调用方
  训练语义，风险不成比例。

## D-005 SVM 概率路径：opt-in 的 one-vs-rest decision scores
- 问题：SVM 无概率（rs_classifier_backend.h:42 注释明示）。
- 候选：a) 改用 OpenCV SVM `setProbability=true`（cv::ml::SVM 无此选项——
  OpenCV 不提供多类 SVM 概率，仅二类 RAW_OUTPUT 决策值）；b) fit 时总是训练
  K 个 OvR 二类 SVM 输出 margin→sigmoid；c) 同 b 但由构造 flag
  `enableOvRDecisionScores` opt-in，默认 false。
- 采纳：c。零行为变化（默认路径与现状完全一致），需要概率的调用方显式开启，
  训练成本 K× 只发生在开启时。多类概率 = OvR sigmoid 后归一化（文档化：
  与 libsigmoid Platt 的二类校准一致；OvR 归一化语义在 docs 中声明）。

## D-006 不确定性定义（机器可验证）
- entropy：H = -Σ p_i log2 p_i（p 由后端概率给出，浮点容差 1e-12；
  p_i=0 视作 0·log0=0）；范围 [0, log2(K)]。
- margin：M = p_(1) - p_(2)（降序前二之差）；范围 [0,1]。
- ensemble disagreement：D = (1/(K))·Σ_i Var_members[p_i]（逐类方差按类平均，
  成员数 m，除以 m 的方差定义用总体方差 1/m）；范围 [0, 0.25]（二值概率时）。
- reject：measure ∈ {entropy, margin, confidence(=1-p1)}，阈值语义统一为
  `rejected ⇔ measure >= threshold`（margin/confidence 用 `<=`，即
  "不合格"方向一致：值越小越不可信）；文档化每个 measure 的方向，测试双向锁定。
  rejected 像素在标签图输出为 -1（与 pipeline 现有"unclassified=0"不同——
  新输出栅格 `uncertaintyOutput` 的第三波段 0/1 掩码为主语义，标签图 reject
  写 -1 仅当显式开启，见 D-007）。

## D-007 管线 additive 参数零行为变化
- 问题：`RsClassificationPipeline::Config` 增加不确定性/校准字段的方式。
- 采纳：全部新字段带默认值（`uncertaintyOutput` 为空串=不输出；
  `rejectThreshold<0`=off；`applyCalibration=false`）；现有调用方
  （GUI/agent/算子）不传新参数时逐比特保持现状行为；回归由现有
  test_classification_pipeline 系列守护。

## D-008 sidecar version 升 2
- 问题：`.meta.json` 需要新增 classOrder/calibration/uncertainty/training/
  featureSchema 键。
- 候选：a) 保持 version=1 塞新键（旧读者会忽略未知键但无法区分
  "没有校准"与"校准被省略"）；b) version=2，loader 接受 1 与 2。
- 采纳：b。`kSidecarVersion=2`；loadModelSidecar 接受 {1,2}；v1 文件读取
  保持原语义（新增键视为缺席）。迁移测试：v1 JSON 字面量 → load 成功。

## D-009 spatial CV 与既有三套划分系统的关系
- 问题：已存在 classification_split（分层+groupIds）、core/spatial_split
  （块+buffer+Moran's I，无生产消费者）、dataset/split（13 种 SplitMethod）。
- 候选：a) 修改 dataset 域接入（违反 ownership——D19 域 read-only）；b) 直接
  改 core/spatial_split 成为生产库（跨域写 src/core，超出 primary scope 的
  收窄边界，且其 API 面向 Train/Val/Test 三分而非 k-fold）；c) 新
  `rs_spatial_cross_validation.h/.cpp`（analysis/classification 内），k-fold
  语义，fold 生成消费 groupIds/坐标，隔离不变量与 core/spatial_split.h:15-17
  一致（||p_train-p_eval|| > bufferDistance），audit 报告结构自持。
- 采纳：c。与 D19 dataset 存储、dataset leakage_audit 完全解耦（GOAL 要求），
  不改任何现有 seam；与 core/spatial_split 的关系在 docs 中说明
  （同源不变量、不同 API 面：k-fold 评估 vs 三分划分）。

## D-010 对象级后处理文件落点
- 问题：对象级 smoothing 放 `src/analysis/segmentation/` 还是 processing？
- 候选：a) segmentation 目录（但不在本 track primary scope）；b)
  `src/processing/algorithms/classification_object_postprocess.*`（匹配
  *classif* scope，与 D15 classification_postprocess 并列、不改动 D15 文件）。
- 采纳：b。输入是 label raster + segmentId raster（纯数组，无 GDAL），
  与 D15 ClassificationPostProcessor 同层；复用其"NoData=-1 永不吸收"语义。

## D-011 studio 面板全部收进 classification_studio_widget.*
- 问题：新面板是否新建文件。
- 采纳：不新建——GOAL primary scope 对 UI 只列了
  classification_studio_widget.*；面板类作为 widget 内部类/匿名命名空间类
  放同文件；纯数据准备函数可导出为 widget 的 public static，供
  test_classification_studio_widget.cpp 无头测试。

## D-012 feature pipeline 深度
- 问题：A 包要求 spectral/index/texture/terrain/temporal typed schema。
  spectral_indices.* 正被 PR #1008 修改（read-only）；terrain/temporal 各有
  独立 seam。
- 候选：a) 在本 track 内实现完整特征工程库（重算/包抄 #1008 → 违反
  ownership）；b) `RsFeatureSchema`/`RsFeatureAssembler` 只做**命名、顺序、
  fingerprint、missing/NoData 契约**与组装，特征值产生继续由既有 seam
  （glcm_texture.h 等）或调用方提供。
- 采纳：b。schema 是跨特征族的 authority，不与任何单族 kernel 耦合；
  与 #1008 无文件交集。vertical slice 用 [band 列 + glcm_texture 输出列]
  组装演示端到端。

## D-013 100k 规模证据形态
- 采纳：`test_classification_intelligence_scale.cpp`，RUN_SERIAL + TIMEOUT
  + 固定 seed；断言不变式（预测标签分布守恒、校准 bins 总数=N、内存
  逻辑上界 = O(样本×类数) 的量级注释），不断言 wall-clock；总运行时间
  预算 < 120s（NormalBayes 线性核）。

## D-014 PR #1008 文件回避清单
- 不触碰：`src/processing/algorithms/spectral_indices.*`、
  `spectral_unmixing.*`、`radiometric_calibration.*`、`src/core/spectral_library.*`、
  `src/core/radiometric_state.*`、`src/analysis/atmospheric/*`、
  `src/analysis/hyperspectral/*`、`src/app/widgets/spectral_profile_widget.*`、
  `src/app/widgets/band_composite_palette.*`、
  `src/agent/spatial_tools/spectral_spatial_tools.*`、其新增 tests、
  `docs/adr/`。共享文件交集仅：`tests/CMakeLists.txt`、`src/analysis/CMakeLists.txt`
  （不碰其改动区，append-only）、`.gitignore`（append-only 三行模式）。

## D-015 open issues disposition
- #1001–#1007 全部为 io/workflow/dataset/georef 域 R2 finding，与本 track
  业务主体（classification）无文件交集 → 不在本 track 修复（避免跨域大修），
  逐条记录于 EVIDENCE OUT_OF_SCOPE 节。
