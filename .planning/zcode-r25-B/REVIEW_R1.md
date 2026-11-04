# R25-B REVIEW_R1（fresh 子代理独立评审，2026-10-28）

评审者：未参与实现的 fresh 上下文 general-purpose 子代理；输入 = `git diff origin/master...HEAD` 全量 + 生产代码对照阅读。
结论：**REQUEST_CHANGES**（1×P0 + 1×P1 + 5×P2 + 6×P3）；同时确认 replay_deviation v2 字符串逐字节保持、`adaptersAccepting()` 纯增量、DataManager 线程亲和契约合规、GDAL RAII 无泄漏、append-only 注册与 .gitignore 模式正确、测试判别力真实（无空洞 CHECK）。

## 处置（全部 P0/P1/P2 已修，P3 除 F11/F13 外已修）

| id | 级别 | 问题 | 处置 |
|---|---|---|---|
| F1 | P0 | test_r25_b_inference_plan / end_to_end 无条件注册，no-OpenCV 构建配置（根 CMake 支持）下两 TU 编译链接失败 | **已修**：两注册块包进 `if(SICNU_HAS_OPENCV AND OpenCV_FOUND AND OpenCV_imgproc_FOUND)`（与 src/operators 编译条件及 test_model_runtime 先例一致），块内保持 append-only |
| F2 | P1 | 多输入批量计划用第一 feed 的波段数；引擎按 max(feeds 的 bands×DECLARED temporalLength) 预算 → temporal 合同下 plan.batchSize/batches/工作集全错 | **已修**：feed→contract 绑定（按名→位置，与 runMultiInput 一致）；`fedChannels = max over feeds(bands × temporalLength)`；超 CV_CN_MAX typed finding；feed 网格差异→info finding（引擎运行时校验/预对齐，不误报拒绝）；`tiling.fed_channels` 入 JSON；新增 temporal 计划测试断言 frames=3/fedChannels=9 |
| F3 | P2 | GeoError 可逃逸 plan 的 never-throws 契约（EO gate 只 catch RSOperatorError） | **已修**：EO gate 增加 `geo::GeoError` catch → `plan.eo_contract_violation` finding |
| F4 | P2 | EO gate 双向错：多输入被跳过（引擎逐 feed 强制）而 detection 被强制（引擎从不） | **已修**：多输入逐 feed 逐 path 带绑定 contract 强制；`asDetection` 时整体跳过（detection lane 无 EO）；scene lane 保持强制（classifyScene 2626 行有 enforce） |
| F5 | P2 | plan 漏引擎门：dtype/ band_roles 元数 / CV_CN_MAX / conf+nms 界 / manifest 派生 output mode 冲突 | **已修**：dtype 用引擎自己的 `inputDTypeMismatch`（含空选带=全带展开）；band_roles 元数；CV_CN_MAX（单+多输入×帧）；conf∈[0,1]、nms∈(0,1]；有效 mode=请求非 Probability?请求:`rasterOutputMode(model)`，其下 uncertainty/多头/mask_threshold 冲突全部入 plan（`plan.output_mode_conflict`） |
| F6 | P2 | GenericRaster.accepts 恒真 → 每个识别产品都判 adapter_ambiguous/NeedsUserChoice | **已修**：歧义=specific（非 generic）候选>1；generic 回退按文档化的首匹配特异规则不构成歧义 |
| F7 | P3 | 目录产品根不可直接打开 → 误导性 import.unreadable+Refused（SAFE/MTL 是正常形态） | **已修**：completeness 健康（非 invalid/unsupported_version）的识别产品 → info `import.directory_product_root`（走 collection 路径），verdict Importable |
| F8 | P3 | scene 请求给了虚构 tile 网格；工作集未计 to_input resize | **已修**：scene lane 跳过网格块 + info `plan.scene_single_pass`；resize 在 JSON 注释与 docs 明示为估算边界 |
| F9 | P3 | eoPreflightPresent 漏检多输入 ARRAY 载荷 | **已修**：非 null 即 present；object 计 advisories，array 逐 entry 累加 |
| F10 | P3 | quint64→double JSON 精度损失 | **已修**：`qint64` QJsonValue |
| F11 | P3 | env 差异双条目（pin 维度 + 每字段） | **保留**（v1 字符串/顺序逐字保持的必然结果；deviationDetails 与 deviations 一一对应是不变量，测试有钉） |
| F12 | P3 | ensemble 在 plan 走 runtime readiness 而 run 先路由 ensemble 引擎 → 无 artifact 的 ensemble manifest 误报 not_ready | **已修**：isEnsemble 时跳过静态 readiness（镜像 run 顺序），info finding 说明逐成员评估在 ensemble 引擎 |
| F13 | P3 | GREEN 未勾选 | 构建排队中（共享宿主锁）；GREEN 后填写 TEST_RESULTS.md |

## 评审确认无问题的部分（原文摘录）

- adaptersAccepting()：纯增量、与 adapterFor 同序、无既有调用者受影响。
- findByPath 符合 DataManager 只读快照线程契约；localIdentityToken fail-closed。
- GDAL 句柄 RAII 无泄漏。
- replay v1 字符串四族逐字节+顺序一致；deviationDetails==deviations 计数由单点 append 保证。
- 注册块 TIMEOUT 转发、目标拼写、.gitignore 模式全部有先例。
- metamorphic 测试是真 oracle（真实 engine.run、精确相等、session 计数/无输出文件双零断言）。

修复回归：`bash` 生产旗语法检查 3 生产 TU + 4 测试 TU 全 exit 0；新增 6 个 R1 收口 SECTION。等待锁构建后全套 GREEN×2 复跑。
