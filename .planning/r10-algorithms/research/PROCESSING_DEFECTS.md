# PROCESSING_DEFECTS — R10-A S1d 只读缺陷狩猎（WP A5/A6/A7 前置）

- 基线：master `8a0864276`（worktree `exp-rs-r10-algorithms`，分支 hardening/r10-algorithm-processing-quality）。
- 方法：范围文件清单约 870 个；深读 Processing 执行链（registry→validate→preflight→execute→commit）、
  GDAL/OTB/IO 算子运行语义抽样（reproject/clip/warp/pansharpen/mosaic/qa_mask/change_detection/
  orthorectification/band_select/spectral_index/stack_register/solar_geometry/recode）、
  temporal 调用面（collection_input/preflight/smooth/composite/gap_fill）、#1447 全域模式扫描
  （script 初筛 474 个 asX() 调用点 → 逐点人工核对，jsoncpp 语义对照 vcpkg buildtrees 中
  1.9.6 实源码 `json_value.cpp` 验证，不用记忆）。
- 硬排除遵守：model_catalog*/rs_json_params.cpp/temporal_workspace.cpp/query_planner.cpp/
  radiometric_state*/model_runtime.*/task_center.*/sdk/python 均未审查未报告。
- 测试覆盖核对：见各条「测试」。未发现既有覆盖复现下述任一缺陷。

## jsoncpp 1.9.6 实测语义（本报告判型基准，读源码验证）

- `asString()`：string/null 正常；**bool/int/uint/real 静默字符串化**（"true"/"5"）；**object/array →
  Json::LogicError("Type is not convertible to string")**。
- `asInt()/asInt64()`：数值正常（越界 uint → LogicError）；**string → LogicError("Value is not
  convertible to Int.")**；in-range real **静默截断**；bool → 0/1。
- `asDouble()`：数值正常；**string/object/array → LogicError**。
- `asBool()`：bool/数值正常；**string/object/array → LogicError**。

---

## A5 — GDAL/QGIS 运算语义

### PR-1 (P1) io:warp `bounds` 契约自相矛盾：schema 宣称字符串、run() 只认数组，字符串形态被静默忽略
- 位置：`src/operators/io/io_operators.cpp:224`（schema：`makeStringParam("bounds", "Target extent
  [minX,minY,maxX,maxY] in target CRS")`）vs `src/operators/io/io_operators.cpp:258-262`（run() 仅
  `isArray() && size()==4` 分支填充 `options.targetBounds`，无字符串解析，无未识别告警）。
- 触发：按 schema 文档传 `{"bounds": "116.2,39.4,116.8,39.9"}`。`targetBounds` 保持空 →
  warpRaster 按源影像全范围重投影——**用户请求的裁剪范围被静默丢弃，产出错误范围的结果**，
  无 error 无 warning。对照 io:clip（:395-408）字符串形态虽也不支持但 fail-closed（size!=4 抛
  typed 错），warp 是 fail-open。
- 为什么错：一算子 schema 与执行语义漂移；agent/GUI 按 schema 生成的参数被静默吞掉，产出
  「看似成功」的错误产物（A5「静默回落」类）。
- 最小复现：tiny GeoTIFF + `io:warp {"input":..,"output":..,"targetCrs":"EPSG:3857","bounds":"1,2,3,4"}`
  → 输出 extent ≠ 请求；再跑 `bounds:[1,2,3,4]`（数组）对照。
- 测试：`tests/test_io_operators.cpp:336-430` 仅覆盖数组形态；字符串形态无任何用例。

### PR-2 (P1, 系统性) 算子直写目标路径 + 失败路径 `QFile::remove`：失败运行会摧毁输出路径上的既有产物（#617 类未收口）
- 位置（代表）：
  - `src/operators/gdal/gdal_operator_utils.cpp:166-206`：`runGdalWarpOnDataset` 把 GDALWarp 直写
    `outputPath`，失败/取消分支 `QFile::remove(outputPath)`（:175,:184,:203）；
  - `src/operators/gdal/gdal_pansharpen_operator.cpp:94-98`：`OutputCleanup` 失败即 remove；
  - `src/operators/rs/rs_mosaic_operator.cpp:350-356`：`OutputFileCleaner` 同型。
- 触发：输出路径已存在合法历史产物 → 新运行 truncate 直写 → 中途失败（GDAL 错误/取消/崩溃）→
  catch 分支 remove → **旧产物一并被删**。`StagedRasterOutput`（`src/processing/gdal/
  staged_raster_output.h`，注释明言 #617 契约「失败既不留半成品也不毁旧产物」）已存在，但
  operators/ 下仅 4 个文件采用（io_fabric_operators、gdal_polygonize、model_publish、
  detection_tile_engine；`PartialOutputGuard` 另 4 个：mnf/mnf_inverse/spectral_unmixing/
  spectral_band_select）；processing/algorithms 侧 8 个文件采用。**绝大多数 rs:/gdal: 算子仍在
  直写+失败删除**。
- 为什么错：GUI/CLI 直连路径绕过 OutputCommitter（staged_raster_output.h 注释自认），#617 修复
  未推广；「取消无半成品」做到了，但「不毁旧产物」没有。
- 最小复现：`output=path/to/old.tif`（已存在合法产物）→ 传损坏 input 触发 warp 失败 → old.tif 消失。
- 测试：无「输出路径已存在 + 失败」的回归用例（#617/#703.4 只覆盖 committer/staged 路径）。

### PR-3 (P2) `compareGrids` 单侧缺 geotransform 静默放行（与单侧缺 CRS 的 fail-closed 不一致）
- 位置：`src/data/raster_grid_compat.cpp:180-181`（`if (!a.hasGeoTransform || !b.hasGeoTransform)
  return report;`）对照 :136-147（单侧缺 CRS → blocking `grid.missing_crs`）。
- 触发：栅格 A 正常地理参考；栅格 B 有 CRS 但无 geotransform（如裸 PNG+prj，GT 为默认单位阵）→
  compareGrids 返回**零 issue** → rs_change_detection(:264-278)、dNBR(:738-748) 等同格算子只剩
  维度检查兜底，按像素位置比较无坐标语义的数据。
- 为什么错：同一服务对「CRS 不对称」fail-closed、对「geotransform 不对称」fail-open；带 CRS 的
  无变换栅格与正常栅格做像素运算是典型静默错配（A5「格网对齐」维度）。
- 最小复现：构造 100×100 EPSG:4326 正常 GT 栅格 + 100×100 同 CRS 无 GT 栅格 →
  `compareGrids` 无 issue；change detection 直接跑通。
- 测试：`tests/` 中 raster_grid_compat 相关用例未覆盖「一侧 hasGeoTransform=false」。

### PR-4 (P2) 算法 preflight 的 same-grid/CRS 检查与运行时语义漂移：WKT 裸串不等即 crs_mismatch，且不查分辨率/原点/范围
- 位置：`src/processing/framework/algorithm_preflight.cpp:334-351`（同格检查仅 width/height；
  CRS 用 `first["crs"].asString() != other["crs"].asString()` 裸 WKT 串比较），对照运行时
  `raster_grid_compat.cpp:149-176` 用 `OSRIsSame` + 像素大小/原点/范围 blocking 检查。
- 触发：同一 EPSG:4326 的两份栅格 WKT 方言不同（ESRI WKT vs OGC WKT、不同 GDAL 版本产出）→
  preflight `crs_mismatch` → `compat["ok"]=false` → `result["valid"]=false`（:560-564）——
  MCP `preflight` meta 工具（mcp_server.cpp:1839）告知 agent「不可运行」，而运行时其实接受。
  反向：分辨率/亚像素错位 preflight 完全不可见（仅 width/height），agent 拿到 valid=true 提交后
  被运行时 typed 拒绝，白跑一轮。
- 为什么错：preflight 与运行时两套同格语义（甚至没用现成的 compareGrids 服务），
  agent 决策依据与实际闸门不一致。
- 最小复现：两份同 CRS 不同 WKT 方言小栅格 → `preflightAlgorithm` → blockers 含 crs_mismatch；
  同参数直接执行成功。
- 测试：tests/test_preflight*.cpp 属 analysis preflight（另一套），算法 preflight 的 CRS 方言用例无。

### A5 正向确认（无缺陷，防重复劳动）
- `gdal_reproject`：dstCrs 空+无 reference → typed 拒；reference 无 CRS/degenerate 像素 → typed 拒；
  categorical 数据强制 nearest（gdal_operator_utils.cpp:95-111）带告警。
- `rs_mosaic`：isSameCrs、旋转拒、像素容差、Y 方向、亚像素 snap 告警（#700）、200MP 上限、
  逐输入 NoData 掩膜均到位。
- `rs_qa_mask`：#699 原生 dtype 读取、F-OPS-3 NoData/NaN fail-closed 掩膜、流式块。
- `gdal_orthorectification`：无 RPC/GCP typed 拒、DEM 存在性检查。
- `rs_change_detection`：radiometric state + compareGrids blocking + 维度兜底。

---

## A6 — 时序算子调用面（黑盒）

结论：**该域质量高，未发现 P0/P1**。正向确认：`temporal_preflight.cpp` 空集合 blocking、缺时间
blocking（requireTimes）、duplicate policy reject/keep_all、多模态/极化一致/尺度 all-or-none 均
有硬闸；fold 算子最少场景数 typed 拒（smooth ≥3、gap_fill ≥2）、composite 空分组 typed 拒、
median 内存预算闸（`medianBudgetInfeasible`）、tile_size clamp 16..4096；取消经
`prepareTemporalRun`→stream reader 贯通。

### PR-5 (P3) `chunkedMode`/`executionMode` 传 bool 被静默字符串化后静默忽略（模式开关 fail-open）
- 位置：`src/operators/rs/rs_change_detection_operator.cpp:336`、`src/operators/rs/
  rs_change_primitives.cpp:134`、`src/operators/rs/rs_spectral_index_operator.cpp:518,524`。
- 触发：非校验入口（CLI/JobEngine，见 PR-6）传 `{"chunkedMode": true}` → jsoncpp asString 把
  bool 静默变 "true" → 与 "pipeline"/"resumable" 都不等 → **静默回落默认执行模式**，无告警。
  传 object 则 Json::LogicError 穿透为泛化错误。
- 为什么错：模式选择是可复现性相关参数（resumable vs pipeline），静默回落违背
  「宁拒勿猜」。经 dispatcher 枚举校验的入口不会触发，属入口不对称放大项。
- 最小复现：CLI `run rs:change_detection '{"chunkedMode":true,...}'`，观察日志无任何模式提示、
  走默认路径。
- 测试：无 bool 形态 chunkedMode 用例。

---

## A7 — Processing 封装执行链

### PR-6 (P2) 一算子多入口参数校验不对称：agent/MCP 面 validate，CLI 与 JobEngine 面裸跑
- 位置：`src/processing/framework/tool_call_dispatcher.cpp:349,419`（validateParameters 后才
  submit）；`src/app/teaching/lab_operator_launch.cpp:77`（GUI 面校验）；对照
  `src/cli/cli_commands.cpp:505`（adapter->execute 直调，无校验）与
  `src/app/shell/processing_job_adapter.cpp:64`（JobEngine 执行器直调，无校验）。
- 触发：同一份畸形参数（如 `"postNir": "5"`）→ MCP 面：结构化
  `parameter postNir must be an integer`（甚至通过，见 PR-8）；CLI 面：
  `operator threw: Value is not convertible to Int.`（cli_commands.cpp:523-527 的 std::exception
  兜底打印 jsoncpp 内部消息）；JobEngine 面：ErrorCode::Unknown 泛化失败。
- 为什么错：「一算子多入口失败语义一致性」不成立；错误类型化在两个入口丢失；用户看到
  jsoncpp 内部文案。
- 最小复现：同一 JSON 分别走 MCP execute 与 CLI run 对比错误输出。
- 测试：无跨入口同参数一致性用例。

### PR-7 (P2) schema_validator 对 String/Any 端口全类型放行（fail-open 根因）
- 位置：`src/processing/framework/schema_validator.cpp:250-254`（`case DataType::String/Any/
  default: valid = true`——bool/object/array/数字全部通过）。
- 触发：String 端口传 `{"name": {"path":"x"}}` → 校验通过 → 算子侧 `requireString` 抛 typed（好）
  或裸 `asString()` 抛 LogicError（PR-8/PR-11 各点）。Raster 端口的 `{path}` 对象形态
  （matchesFileShape :70-80 合法）与算子侧只认 string 的要求也同类。
- 为什么错：校验层宣称「type/enum/range 单一实现」但对最常见类型不设防，把类型错误推迟到
  算子内部以最差形态爆出。
- 最小复现：dispatcher validateCall 传 `{"rename":{"a":1}}`（rename 为 string 端口）→ valid=true。
- 测试：test 有 schema_validator 用例但未覆盖 String 端口 hostile 类型。

### A7 正向确认
- `output_committer.cpp` 成熟：#617 publish-then-swap、#1174 sidecar 先主文件后、R6 fsync、
  fault-point、注册失败回滚、provenance 附着失败降级 warning 不吞——无新缺陷。
- 四个执行入口（JobEngine job_engine.cpp:1106-1114、worker sicnu_worker_main.cpp:446、CLI
  cli_commands.cpp:523、workflow workflow_runtime.cpp:318,336）均有 std::exception 兜底——
  Json::LogicError 不会击穿进程，只降解错误语义。
- Unicode 路径：GDAL 调用统一 UTF-8（含 output_committer.cpp:71）、QProcess 走 QString，未见
  ANSI 误用。

---

## #1447 模式域内清查（无守卫 asX() 于外部可控 JSON）

初筛 474 调用点 → 103 无近旁守卫候选 → 人工核对后剔除自产 JSON（worker IPC 帧、preflight 探针
info、agent_workflow_executor stepResults）与假阳性（gdal_clip extent 有 isNumeric 预检）。确认下列
域内实例（语义见文首基准）：

### PR-8 (P2) 校验器接受「数字字符串」而算子裸调 asInt()/asDouble()：LogicError 穿透 + 整值浮点静默截断
- 根因：`schema_validator.cpp:117-120,169,179` 对 Integer/Numeric 端口接受数字字符串
  （parseIntegerString/parseNumericString）；算子侧却直接 `.asInt()/.asDouble()`：
  - `src/operators/io/io_operators.cpp:261`（io:warp bounds 项）、`:403`（io:clip bounds 项）、
    `:573`（buildOverviews levels 项）、`:735`（vector clipBounds 项）；
  - `src/operators/rs/rs_spectral_index_operator.cpp:559-560`（postNir/postSwir2——同文件 dNBR
    分支 :757 用的是 `getInt()` 助手，自相不一致）；
  - `src/operators/rs/rs_spectral_band_select_operator.cpp:190-191`（wavelengthMin/Max）。
- 触发：`{"bounds": ["116","39","117","40"]}` 或 `{"postNir": "5"}` → 校验通过（字符串数字合法）
  → 算子抛 `Json::LogicError("Value is not convertible to Double/Int")` → 各入口兜底成泛化错误
  （见 PR-6）。另：`{"postNir": 5.5}` → asInt 静默截断成 5（in-range real 截断），无告警。
- 为什么错：这是 #1447 的域内变体——类型混乱以异常穿透参数契约边界；修复方向是统一
  （校验器收紧 or 算子走 rs_json_params 容错读取，二选一，勿造方言）。
- 最小复现：MCP execute io:clip，bounds 全字符串数组。
- 测试：tests/test_io_operators.cpp bounds 用例全为 double 数组；无字符串项用例。

### PR-9 (P2) 外部 STAC 文件解析 `.get(k,"").asString()` 无类型守卫（敌意/损坏 STAC → LogicError）
- 位置：`src/operators/rs/rs_inference_operator.cpp:91,101,108,112,126,136,143`（datetime/href/
  rel/type/资产键）；同型 `src/operators/rs/rs_sar_network_inversion_operator.cpp:263`
  （inputs 数组项）。`.get(k, "")` 只在键缺失时给默认，键存在但值为 object/array 时返回原值 →
  asString 抛 LogicError。
- 触发：STAC item 写 `"properties": {"datetime": 20241001}` 或 `"href": []` → 该算子其余分支都
  抛 typed InvalidInputData（:88-97,114-124 等好范例），唯独这几处变成 jsoncpp 泛化异常。
- 为什么错：外部文件输入正是 #1447 原始威胁模型（hostile JSON）；与本文件其余 fail-closed
  风格脱节。
- 最小复现：构造 datetime 为数字的最小 STAC collection + item，跑推理算子 stac_collection 参数。
- 测试：tests 中 STAC 用例（test_io_stac_client 等）属 io 层，不覆盖该算子。

### PR-10 (P3) FeatureCubeContract::fromJson 版本号 asInt：字符串→LogicError、1.9→截断成 1 静默接受
- 位置：`src/processing/features/feature_cube.cpp:150`（`v["version"].asInt() != 1`）。
- 触发：契约 JSON `"version": "1"` → LogicError；`"version": 1.9` → 截断为 1 → 当作 v1 接受，
  后续字段按 v1 语义解析未知结构。
- 最小复现：fromJson(R"({"version":1.9,...})") 返回 true。
- 测试：无。

### PR-11 (P3) rs:stack_register scenes 数组项 asString 无守卫
- 位置：`src/operators/rs/rs_stack_register_operator.cpp:141-143`。
- 触发：`{"scenes": [{"id":"a"}]}` → LogicError；`{"scenes":[5]}` → 静默变 "5" 当场景 id 用。
- 测试：无。

### PR-12 (P3) rs:recode 映射值 asInt 包在 catch(...) 里：类型混乱条目被静默丢弃
- 位置：`src/operators/rs/rs_recode_operator.cpp:107-118`。`mapValue[key].asInt()` 对字符串值
  抛 LogicError → catch(...) → 该重映射条目**静默消失**，类被映射到默认而非用户指定值——
  分类语义静默改变（比报错更糟）。catch 同时吞掉 stoi 的真键错误，无法区分。
- 最小复现：`{"mapping": "{\"1\":\"2\"}"}`（值是字符串）→ 1→2 映射丢失，无告警。
- 测试：无。

### #1447 模式明确排除项（核对过，勿重复报）
- worker IPC 帧解析（local_worker_pool.cpp:453-507、local_worker_host.cpp:147-176）：帧由
  sicnu_worker_main 自产且 malformed 帧走 Crashed 通道——非外部输入。
- algorithm_preflight.cpp 各 asInt/asBool/asString：全部作用于本文件 probeRasterDataset 自产
  JSON（宽度/exists 均为强类型写入），无暴露。
- schema_validator.cpp:17 `isNumeric → asString()` 与 json_params_converter.h:44,103
  `uint64>2^63 → asString()`：jsoncpp 1.9.6 对数值 asString 是**合法字符串化**（已对源码核
  实），不是缺陷——修复 #619/#1447 的同事踩对了语义。

---

## 汇总

| 编号 | 域 | 严重度 | 一句话 |
|------|----|--------|--------|
| PR-1 | A5 | P1 | io:warp bounds schema 说字符串、代码只认数组，字符串形态静默忽略产错范围结果 |
| PR-2 | A5/A7 | P1 | 直写+失败 QFile::remove 系统性未收口：失败运行摧毁输出路径既有产物（#617 类） |
| PR-3 | A5 | P2 | compareGrids 单侧缺 geotransform 静默放行（单侧缺 CRS 却 blocking） |
| PR-4 | A5/A7 | P2 | 算法 preflight 同格检查 WKT 裸串比较+仅宽高，与运行时 OSRIsSame/全格网语义漂移 |
| PR-5 | A6 | P3 | chunkedMode 传 bool 被静默字符串化后静默忽略（模式开关 fail-open） |
| PR-6 | A7 | P2 | 参数校验只在 agent/MCP/GUI-lab 面，CLI 与 JobEngine 裸跑，同参数异象错误 |
| PR-7 | A7 | P2 | schema_validator 对 String/Any 端口全类型放行（fail-open 根因） |
| PR-8 | #1447 | P2 | 校验器收数字字符串、算子裸 asInt/asDouble → LogicError 穿透 + 浮点静默截断（io bounds/levels、spectral_index、band_select） |
| PR-9 | #1447 | P2 | 外部 STAC 解析 get(k,"").asString() 无守卫，敌意类型 → LogicError 非 typed 错 |
| PR-10 | #1447 | P3 | feature_cube version asInt：字符串炸、1.9 截断成 1 静默接受 |
| PR-11 | #1447 | P3 | stack_register scenes 数组项 asString 无守卫（object 炸/数字静默变 id） |
| PR-12 | #1447 | P3 | rs:recode 映射值类型错误被 catch(...) 静默吞掉，重映射条目无声丢失 |

P0=0，P1=2，P2=6，P3=4。所有发现均为主代理可独立复核的 file:line 级；jsoncpp 行为判断全部对照
本机 vcpkg buildtrees 1.9.6 源码验证，非记忆引用。A6 时序面与 OutputCommitter 经深读确认健壮，
建议 R10-A 修复波次不要在这两处重复投入。
