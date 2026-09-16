# CURRENT_ARCHITECTURE — spectral-intelligence-11

权威/seam 图（启动时 master = a5b11b7f；行号以该基线为准）。

## 算法层（sicnu_processing, src/processing/CMakeLists.txt 扁平清单）
- 光谱内核：`algorithms/spectral_{anomaly,unmixing,classification,detection,library,table,roi,wavelength,resampling,derivative,indices}.cpp` + `endmember_extraction.cpp` + `mnf_transform.cpp`。
- 共享原语：`algorithms/primitives/dense_linalg.{h,cpp}`（`sicnu::primitives::invertDenseMatrix`，Gauss-Jordan + 1e-12 pivot）；`algorithms/primitives/window.h`（`sicnu::rs::primitives::WindowSpec/EdgePolicy` 窗口/halo 契约，流式默认 Replicate）。
- FCLS 内部（cholesky/NNLS）是 `spectral_unmixing.cpp` 匿名 namespace 私有 → 本 track 稀疏解混自带求解器（该文件为 #1008 contested，不可触碰）。
- 算法错误约定：`bool + QString* errorMessage` 人类可读 typed refusal；QJson 工件；jsoncpp 只在算子层。

## 算子层（sicnu_operators, src/operators/CMakeLists.txt `SICNU_OPERATORS_SOURCES`）
- 框架：`framework/rs_operator.h`（name/run 纯虚；determinismGrade/memoryPolicy/streamingHaloPixels/schema/metadata/executionEstimate）；`rs_operator_context.h`（reportProgress 节流 2%、throwIfCancelled→ErrorCode::Cancelled、logInfo/Warning/Error、workDir/tempPath；无 artifact-store 访问，工件以路径传递）。
- 注册双点（缺一不可，#707）：`rs/rs_operators_init.cpp` L121–203 REGISTER_RS_OPERATOR 宏 + `initBuiltinRsOperators()` 内显式 `add("rs:xxx", ...)` 工厂（~L285–420）。
- schema 助手：`framework/rs_schema.h`（makeRasterParam/makeOutputParam/makeNumberParam/makeIntegerParam/makeEnumParam/makeRootSchema/makeRequired）；参数助手 `framework/rs_json_params.h`（requireString/fileExists/getString/getInt/getDouble/getBool/getEnum/parseBands/getStringArray）。
- 错误：`RSOperatorError(ErrorCode::…)`（rs_operator_error.h L24–54 分类）。
- 流式：`GdalMultibandBlockStream::forEach` 单线程顺序，BIP 借出缓冲 `pixels[((y*tileW+x)*bandCount)+b]`；输出 `GdalStreamingOutput`（GTiff、复制 geoTransform/projection、NaN=NoData 不伪造 −9999、失败/取消必须 `abandon()` 删除半成品）。
- 参考谱 seam：`rs/rs_spectral_reference_input.h`（inline/ref/libraryPath 三选一 + `RasterWavelengthGrid::read` + Gaussian-SRF/线性重采样到输入网格 + disjoint typed refusal；`referenceInputSchemaProps()` 保持多算子 schema 一致）。

## 工件/链路
- `exp-rs:spectral-table`（spectral_table.h，v1，Provenance{sourceOperator/sourceInput/parameters/createdAtMs/synthetic/derived}、SHA-256 `digestHex`、kMaxCells=4Mi、validate/toJson/save/load/loadValidated）；写范式 `rs_endmember_extraction_operator.cpp:439-465`；消费 `rs_spectral_reference_input.cpp:101`。
- `exp-rs:mnf-transform`（mnf_transform.h，v1，**kMaxBands=1024**）；写 `rs_mnf_operator.cpp:226-267`，读 `rs_mnf_inverse_operator.cpp:317`。
- placeholder 语法：`$stepId.output` / `${stepId.portName}` / `${task.12.output}` / `${ENV_VAR}`（placeholder_grammar.h）——**不是 `{{}}`**。
- 缓存/指纹：workflow 层（artifact_store.h + execution_fingerprint.cpp + artifact_digest.h `sha256Hex`）；算子工件自带 digest。

## capability/agent 面（drift 门）
- `data/agent/capabilities/spectral.json` + `spectral_transform.json`（rs:rx_anomaly、rs:mnf 在后者）；intent 词汇闭集在 `tests/test_capability_drift.cpp` `kAllIntents`。
- `data/processing/algorithm_meta/rs-*.json` 为**生成物**：代码内 metadata() → `sicnu_geo_rs_cli --export-catalog data/processing/algorithm_meta` 重生成；门：`test_algorithm_meta_drift.cpp` byte-exact + `REQUIRE(expectedCatalog.size() == 32)` 尺寸 pin。
- `data/processing/algorithm_meta/capability/*.json` + capability_relations.json：per-operator manifest，门 test_capability_knowledge / test_drift_projection_10。

## GUI
- dock 挂载范式：`src/app/main_window_docks.cpp:248-275`（QgsDockWidget + objectName + addDockWidget(Right) + tabifyDockWidget(m_spectralDock, new) + hide() + Window menu toggle；成员加 main_window.h）。widget .cpp 注册进 `src/app/CMakeLists.txt` sicnu_geo_rs 清单（~L227，AUTOMOC on）。
- 光谱数据喂入 seam：`SpectralProfileWidget::setSpectrum(values, wavelengths, labels, layerName)`（预计算数组；面板自己 loadValidated 工件）。
- 禁改（D18/#1008）：`src/app/workbench/mission_context.*`、`main_window_workbench.cpp`、`workbench/{agent_context_tool,command_defs,inspector_host,temporal_workbench_panel}.*`、`pipeline/ir2_pipeline_designer_dock.h`、`workbench/classification_studio_widget.*`、`widgets/spectral_profile_widget.*`、`widgets/band_composite_palette.*`。

## 测试
- 光谱中量级范式（tests/CMakeLists.txt ~L5325-5402）：`add_executable` + link `Catch2WithMain Qt6::Core Gui Widgets qgis_core qgis_gui sicnu_processing sicnu_operators GDAL::GDAL` + include src/build + `sicnu_link_jsoncpp` + `sicnu_discover_tests`。
- Qt-free 内核范式：test_spectral_table（L1198-1208：Catch2 + Qt6::Core + sicnu_processing）。
- ctest 名带 `TEST_PREFIX`（`test_xxx::`），gate 用 `ctest -R "test_xxx::" -j1`。
- oracle 风格：套件内独立闭式/参考实现 + 合成立方体；tags `[<family>][kernel|operators|artifact]`。

## 文档/ADR
- ADR 下一个空闲号 **0163**（0158 将被 #1008 占用，避开）。参照 ADR 0148（spectral intelligence platform）。
- CHANGELOG：在现有 Unreleased 之上加 `## [Unreleased] - Spectral Intelligence 11 (zcode/spectral-intelligence-11)` 段。
- 无 docs/spectral/；算子行文档范式 docs/processing/temporal.md。

## 工具链
C++20、Qt 6.8、GDAL、jsoncpp（算子）/QJson（算法工件）、Catch2。构建 preset `build-dev`（CMakePresets.json）。
