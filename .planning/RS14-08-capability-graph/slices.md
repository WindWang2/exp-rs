# Slices — RS14-08-capability-graph

每个 slice 严格 RED → GREEN → REFACTOR → narrow test → commit。测试文件：`tests/test_capability_state_graph.cpp`（单注册多 TEST_CASE，轻量链接）。

## Slice A — graph schema + loader + validator
- 文件：`src/capability_state_graph/state_predicate.{h,cpp}`、`capability_node.h`、`state_transition.h`、`capability_edge.h`、`capability_state_graph.{h,cpp}`、`graph_loader.{h,cpp}`、`graph_validator.{h,cpp}`、CMake（新 STATIC 目标 + root 一行）。
- RED 用例：
  1. StatePredicate canonical 序列化确定（排序/去重/规范 id）。
  2. loader 拒绝缺 schema_version / 未来版本（typed `CAPSTATE_E_VERSION`）。
  3. loader 拒绝未知 fact / 未知值（radiometric_state 词表外）→ typed `CAPSTATE_E_FACT`；别名 dn/toa 归一为长形。
  4. loader 拒绝重复 transition id（`CAPSTATE_E_DUPLICATE`）。
  5. validator：dangling operator ref（authored 引用不在投影中）→ finding 而非 crash；produces 与 requires 全空 → finding；深度炸弹 JSON（>stackLimit）被拒。
- 边界：空 transitions 数组合法（空图）；谓词集 canonical id 在乱序输入下相同。

## Slice B — Registry projection adapter
- 文件：`src/capability_state_graph/operator_facts.h`（header-only `extractOperatorFacts(const AlgorithmDescriptor&)`）、`graph_builder.{h,cpp}`（OperatorFacts + AuthoredTransitions → CapabilityStateGraph：operator 节点 + 边展开）。
- RED 用例：
  1. 从最小 descriptor 投影出 operator 节点（id/label/group 正确）。
  2. 非 rs: 前缀算子（gdal:/qgis:）同样投影，节点 id 前缀保留。
  3. 展开：一个 requires=∅ produces={masked=true} 的转换从 3 个状态节点各生成一条边，to = merge 语义正确（替换/追加）。
  4. whenParams 条件生成多条平行边、id 确定性。
  5. 合并自 descriptor 的 declared facts（rsContract.radiometricState 存在时作为该算子节点 attributes，不臆造）。

## Slice C — state predicate matching
- 文件：`state_matcher.{h,cpp}`、`observed_state.h`（ObservedState value object = 事实→值 map，未来 observed-state provider 的 DTO）。
- RED 用例：
  1. AnyOf/IsTrue/IsFalse 匹配；observed 缺失 fact → unsatisfied 且 observed=unknown（fail-closed，无 silent true）。
  2. 多谓词合取：一票否决 + 全部明细返回。
  3. radiometric_state 值比较用规范形；observed 传别名同样归一（宽容输入，严格输出）。
  4. 边界：空 requires 匹配任何状态（含空 observed）。

## Slice D — authored transition metadata（representative families）
- 文件：`data/capability_state_graph/transitions.json`（v1 手写）；加载/校验接入；`tests` 内嵌 fixture 最小集 + committed 文件全量校验用例。
- 覆盖 family（每条含 why/assumptions/informationLoss/resource/caveats/evidence）：
  1. 光学定标三分支（rs:radiometric_calibration × whenParams → toa/radiance/bt，bt 走 radiance 中间态或直接系数路径按现状契约撰写）。
  2. 大气校正族（rs:atmospheric_correction/dos1/dos2/quac：toa→sr；与 exclusive 关系在 caveats 引用，不复写 capability_relations 数据）。
  3. QA 掩膜 rs:qa_mask / rs:apply_mask（masked=true，requires=∅）。
  4. rs:align（grid_aligned=true，grid fixer 教学语义）。
  5. rs:reproject（crs_projected=true；caveat：共享网格需重新建立）。
  6. SAR 定标 rs:sar_calibrate（sar_calibration_domain per params）。
  7. 指数族代表 rs:ndvi / rs:spectral_index（requires radiometric∈{toa,sr}，caveat：dn 可运行但质量降级 warn）。
  8. 模型推理代表 rs:classify / rs:infer（requires 含模态/grid 事实按现状契约）。
  9. 时间序列代表 rs:temporal_composite（requires modality=temporal）。
- RED 用例：committed 文件 validator 零 error；代表性链（calibrate→atmospheric→ndvi）经图可达；每条转换 evidence.metadataKey 非空。

## Slice E — path/query algorithms
- 文件：`graph_queries.{h,cpp}`。
- RED 用例：
  1. capabilitiesAccepting：DN 观测态 → 返回 qa_mask/align/calibration 等，不含 ndvi；逐谓词明细正确。
  2. producersOf：目标 masked=true → apply_mask/qa_mask；目标 sr → atmospheric 族。
  3. explainUnavailable：ndvi+DN 态 → unsatisfied[radiometric_state]，preparations 含 calibration（当观测=dn 时直接产出 toa）。
  4. shortestPath：DN→(radiometric∈{toa,sr}) 给出 1-2 步候选；structural_only=true；无路径时 typed `CAPSTATE_NO_PATH` 不抛异常。
  5. 边界：环不导致死循环（visited+hop 上界）；空图查询 typed 空结果。

## Slice F — human explanation projection
- 文件：`graph_explain.{h,cpp}`。
- RED 用例：
  1. explainWhyFirst(calibrate→ndvi) 存在依赖：A 产出 toa∈B.requires；结论字段 + 步骤字段。
  2. explainWhyFirst(qa_mask→align) → typed no_dependency（不编造关系）。
  3. explainPath 输出含 informationLoss/caveats 编排、zh-CN 文案、deterministic（两次调用逐字节一致）。
  4. teaching_mode 与 agent_mode 同图同语义：两者都从同一查询结果派生，仅投影差异（一致性断言）。

## Slice G — graph consistency / golden path tests
- 用例：
  1. committed transitions.json + 真实 registry 投影（若构建环境能拉起 AtomicAlgorithmRegistry 则用之；否则用 recorded OperatorFacts fixture）→ validator 零 error、零 dangling。
  2. 端到端教学场景：导入 DN → explainUnavailable(ndvi) → path → explainPath，断言科学反馈链完整（含 warn 语义）。
  3. deterministic replay：完整图 JSON 导出两次一致。
  4. schema version compatibility：v1 文件在当前 loader 下通过；伪造 v2 拒绝。

## Agent 工具 slice（thin adapter，随 F 后）
- 文件：`src/agent/spatial_tools/capability_state_tools.{h,cpp}` + `spatial_tool.cpp` 注册一行。
- 用例（若链接闭包可行）：schema 声明/execute happy+invalid；否则单 TU 编译检查 + F 层单测兜底，PR 中如实说明验证深度。

## Commit 粒度
每 slice 一个 commit（含 planning 文档更新）；slices A 前先单独 commit planning 文档。
