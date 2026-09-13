# Findings — src/operators

## F-OPS-1 · Labels 输出的 class_mapping 产品类值未按输出栅格编码校验，静默损坏类 id

- **Severity**: P2
- **Lens**: 5（契约/元数据漂移）+ 1（输出域）
- **Location**: `src/operators/runtime/tile_inference_engine.cpp:852-874`（writeType/writeNoData 由重映射**前**的类数决定）、`:1595-1613` 与 `:687-708`（productClass 直接写入 float 平面）、`src/operators/framework/model_catalog.cpp:829-845`（class_mapping 只校验非负与单射，不校验上界）
- **Code**:
  ```cpp
  // tile_inference_engine.cpp:854-860 —— writeType 只看 model.output.classes.size()，
  // 完全没有参考 postprocess.classMapping 的目标域：
        case RasterOutputMode::Labels:
        {
          const int classCount = static_cast<int>( m_model.output.classes.size() );
          writeBands = 1;
          writeType = classCount <= 255 ? /*GDT_Byte*/ 1 : /*GDT_UInt16*/ 2;
          writeNoData = classCount <= 255 ? 255.0f : 65535.0f;
          break;
        }
  ```
  ```cpp
  // tile_inference_engine.cpp:1602-1606 —— 重映射值未经域检查直接写像素：
                  const int productClass =
                    m_model.postprocess.classMapping.empty()
                      ? best
                      : m_model.postprocess.classMapping[static_cast<std::size_t>( best )];
                  outRow[col] = static_cast<float>( productClass );
  ```
  ```cpp
  // model_catalog.cpp:832-843 —— 校验只有 >= 0 与单射：
      for ( std::size_t i = 0; i < mapping.size(); ++i )
      {
        if ( mapping[i] < 0 )
          markInvalid( "postprocess.class_mapping values must be >= 0 (element "
                       + std::to_string( i ) + " is " + std::to_string( mapping[i] ) + ")" );
  ```
- **Root cause**: 输出栅格编码（GDT_Byte/GDT_UInt16 与 NoData 哨兵 255/65535）由**模型类数**决定，而像素实际写入的是**产品类**（class_mapping 目标值）。清单校验只约束"非负且单射"，未约束 productClass < NoData 哨兵且 < 2^(bits)。
- **Impact**: 数据静默损坏。`class_mapping` 值 ≥ 255（或 > 255）时：像素经 `GDALRasterIO` float→Byte 钳制后变为 255——恰是 writeNoData，整类像素读回为 NoData；值 > 255 时全部塌缩成 255/被钳制，类信息永久丢失，且 palette/统计（`SICNU_CLASS_PALETTE`、`class_pixel_counts`）仍声称该类存在。任何错误提示都没有。
- **Trigger condition**: manifest 声明 `output.format="labels"` 且 `postprocess.class_mapping` 含 ≥255 的目标类（例如 8-bit 遗留产品域 1..255、或 0 基产品 id ≥ 255）。清单校验放行（非负、单射），运行时静默产出损坏的标签栅格。
- **Evidence**: `verified-by-test-draft`（转换语义已核：`gdal_multiband_block_stream.cpp:192-202` 以 GDT_Float32 缓冲写入 Byte 波段，GDAL 隐式钳制）
- **Reproduction**: `review/tests/F-OPS-1.cpp`（Catch2 草稿）：注册 manifest `{classes:[a,b,c], postprocess.class_mapping:[0,1,300]}`，对 2×2 全零输入跑 `TileInferenceEngine::run(outputMode=Labels)`，断言输出 Byte 栅格的类 2 像素值 == 2。现实现会得到 255（=NoData），断言失败。
- **Recommended fix**: `parseManifest` 在 class_mapping 校验中增加上界（≤ 254，或与 writeType 联动：`max(classMapping) >= 255` 时强制 UInt16 且 NoData=65535）；`TileInferenceEngine` 在 Labels 分支按 `1+max(classMapping)` 选择 writeType/writeNoData（stats.classPixelCounts 已经按这个域分配大小，说明作者知道产品域 ≠ 模型类数，唯独漏了栅格编码）。
- **Dedupe**: new——250 个既有 issue 与两份历史审计均未涉及 class_mapping 域校验（#878 是渲染端容器规则问题；#646 是该字段引入前的 declared-but-unenforced 清扫）。

## F-OPS-2 · TensorBlob::fromMat 非连续回退拷贝对 ND Mat 是空转——静默喂入未初始化字节

- **Severity**: P3
- **Lens**: 2（内存安全）
- **Location**: `src/operators/runtime/tensor_blob.cpp:163-173`
- **Code**:
  ```cpp
      else
      {
        // Multi-dim Mats can be non-continuous when ROI'd; copy row ranges.
        std::size_t offset = 0;
        for ( int r = 0; r < mat.rows; ++r )
        {
          std::memcpy( blob.bytes.data() + offset, mat.ptr<const std::uint8_t>( r ),
                       static_cast<std::size_t>( mat.step ) );
          offset += static_cast<std::size_t>( mat.step );
        }
      }
  ```
- **Root cause**: 对 `dims > 2` 的 `cv::Mat`，`mat.rows` 恒为 `-1`（cv::Mat 契约：rows/cols 仅在 2D 有意义），循环体零次执行；而 `blob.bytes.resize(total)` 已按 `mat.total()*elemSize` 分配。结果：非连续 ND Mat（例如对 4-D blob 的 dim-0/1 Range ROI，插件 provider 的真实用法）走 `fromMat` 得到一个 `isValid()` 为真（字节数匹配）但内容**未初始化**的 TensorBlob。
- **Impact**: 静默垃圾数据——校验全过（isValid 只查字节数），推理直接消费未初始化内存；无任何错误信号。
- **Trigger condition**: 当前第一方调用方全部传连续矩阵（clone/fresh blob），故**现网不可达**；但 `fromMat` 是 provider/plugin 的公共拼装面（http/python/第三方 runtime 都把 `cv::Mat` 交给它），任何 ROI 化的多维张量都会触发。
- **Evidence**: `static-only`
- **Reproduction**: `review/tests/F-OPS-2.cpp` 草稿：构造 4-D 连续 Mat，取 `mat(Range(1,2), Range::all(), Range::all(), Range::all())` 得到非连续 4-D ROI，`TensorBlob::fromMat(roi)` 后断言 blob 内容与源平面逐字节相等——现实现字节全为未初始化。
- **Recommended fix**: 循环上限用 `mat.total() / (mat.step[mat.dims-2] / mat.elemSize())`（或按 `mat.dims` 逐维步进），dims>2 且非连续时直接 `cv::Mat contiguous = mat.clone()` 复用连续分支。
- **Dedupe**: new。

## F-OPS-3 · rs:qa_mask 对不可读 QA 样本 fail-open（NaN/NoData→0=clear），质量门失效方向错误

- **Severity**: P2
- **Lens**: 1（科学计算与算法契约）+ 5（输出契约）
- **Location**: `src/operators/rs/rs_qa_mask_operator.cpp:296-311`（convertSample）、`:106-113`（buildMaskRule 的 SCL 类集从不选 class 0/NoData）
- **Code**:
  ```cpp
    auto convertSample = [&](double v) -> uint16_t {
        if (hasNodata && std::isfinite(nodataVal) && std::abs(v - nodataVal) < 1e-9) {
            ++irregular;
            return 0; // declared NoData -> not a QA word; leave unmasked
        }
        if (!std::isfinite(v)) {
            ++irregular;
            return 0; // NaN/Inf -> keep the historical "clear" outcome, no UB
        }
        if (v < 0.0) {
            ++irregular;
            return 0; // negative sentinel -> clear
        }
  ```
  ```cpp
        if (maskSelection == "all")
            select({QaMask::SclSaturated, QaMask::SclDarkFeatures,
                    QaMask::SclCloudShadow, QaMask::SclCloudMediumProbability,
                    QaMask::SclCloudHighProbability, QaMask::SclThinCirrus,
                    QaMask::SclSnow});
  ```
- **Root cause**: QA 词不可读（NaN/负哨兵/声明 NoData）时返回 uint16 值 0：Landsat QA_PIXEL 词 0 = 全位清零 = "clear"；SCL 值 0 = NO_DATA 类，而 sclClasses 对任何 maskSelection（含 "all"）都不选中 class 0。两条路都把"QA 不可知"判为"无云"。
- **Impact**: 科学结论污染——质量掩膜是下游云掩蔽的门；QA 波段损坏/带声明 NoData 的区域（S2 堆栈中 SCL 波段声明 nodata=0 很常见）被掩膜产品判为 clear，云/雪像素流入合成与指数计算。仅有一条 warning 日志，管线消费者不会失败。
- **Trigger condition**: 输入 QA 波段带声明 NoData（或含 NaN/负值），且这些像素处光学数据可能有云。常见于 landsat_import/sentinel2_import 产出的堆栈。
- **Evidence**: `verified-by-test-draft`
- **Reproduction**: `review/tests/F-OPS-3.cpp`：Float32 QA 波段含 NaN 的 2×2 输入跑 `rs:qa_mask`（cloud_and_shadow），断言输出掩膜对 NaN 像素为 1（masked）；现实现写 0。SCL 路径：SCL=0 的像素即使 mask=all 也断言被掩蔽——现实现为 0。
- **Recommended fix**: fail-closed：不可读 QA 词应产生 masked=1（或一个独立的 255=unknown 输出类）；SCL 类集至少在 "all" 中纳入 SclNoData。`#699` 注释表明这是"保留历史结果"的有意选择——修复时需同步更新其测试。
- **Dedupe**: 与 #719（temporal point-extract fail-open）、#612（threshold_raster NoData→clear）同族但文件与行为均未覆盖；#699 修复 UB 时明确保留了该语义（"keep the historical clear outcome"），非重复提交。

## F-OPS-4 · io:reproject 的 srcCrsOverride 是死参数——无 CRS 输入经"获准兜底"重投影后静默错配地理参考

- **Severity**: P1
- **Lens**: 5（契约/元数据漂移）+ 1（地理参考正确性）
- **Location**: `src/operators/io/io_operators.cpp:301-323`（读而不传）、`src/geospatial/convert/raster_convert.h:50-62`（WarpOptions 无源 CRS 字段）、`src/geospatial/convert/raster_convert.cpp:202-204`（warp 参数只建 -t_srs）
- **Code**:
  ```cpp
  // io_operators.cpp:306-313 —— 校验要求 CRS-less 输入必须声明 srcCrsOverride：
    const sicnu::geo::RasterMetadata meta = sicnu::geo::inspectRaster( input );
    if ( !meta.crs.valid && params::getString( params, "srcCrsOverride" ).empty() )
    {
      Json::Value details;
      details["path"] = input;
      details["policy"] = "declare srcCrsOverride to take responsibility for the source CRS";
      throw RSOperatorError( ErrorCode::InvalidParameter,
                             "input raster carries no CRS; refusing to guess", details );
    }
  ```
  ```cpp
  // io_operators.cpp:314-317 —— options 组装完全没有 srcCrsOverride：
    sicnu::geo::WarpOptions options;
    options.targetCrs = params::requireString( params, "targetCrs" );
    options.resampling = params::getString( params, "resampling", "near" );
    options.creationOptions = { "COMPRESS=LZW", "TILED=YES" };
  ```
  ```cpp
  // raster_convert.cpp:202-204 —— warp 参数里没有 -s_srs（WarpOptions 也没有对应字段）：
    std::vector<std::string> args;
    args.emplace_back( "-t_srs" );
    args.emplace_back( options.targetCrs );
  ```
- **Root cause**: schema 把 `srcCrsOverride` 文档化为"the only sanctioned fallback"（io_operators.cpp:278-279），run() 校验它，但 `WarpOptions` 根本没有源 CRS 字段可承接。CRS-less 输入放行后，`GDALWarp` 在源无 SRS 且无 `-s_srs` 时按"源 CRS == 目标 CRS"处理：像素不做任何变换，输出栅格却被打上 targetCrs 标签。
- **Impact**: 数据静默损坏——像素坐标空间的数据被标注成（例如）EPSG:4326，下游按错误地理参考做叠加/量测/切片，无任何报错。声明参数全程无效，正是 #646 "declared knob does nothing" 类。
- **Trigger condition**: 对无 CRS 的栅格（常见：产线外原始影像、部分 ENVI/二进制转换产物）调用 `io:reproject` 并按提示声明 `srcCrsOverride`。
- **Evidence**: `verified-by-test-draft`
- **Reproduction**: `review/tests/F-OPS-4.cpp`：GDAL 建一个 4×4、无 SRS 的 GTiff，调 `IoReprojectOperator` 带 `srcCrsOverride="EPSG:4326"`、`targetCrs="EPSG:32633"`；断言输出中心像元的地理坐标等于源像素中心经 4326→32633 变换后的坐标。现实现输出 geotransform 与源像素网格一致（未变换）。
- **Recommended fix**: `WarpOptions` 增加 `sourceCrsOverride`，warpRaster 在其非空时 emplace `-s_srs`；IoReprojectOperator 把参数传入。
- **Dedupe**: new——#903 覆盖 range_cache/python-worker/时间戳，未涉及 io:reproject；#880 是 io:inspect 的 schema 漂移；#637 是 VRT 提供者。

## F-OPS-5 · 检测 NMS/去重是 O(n²) 且位于取消检查点之外——max_detections 上限内可长时间不可取消阻塞 worker

- **Severity**: P2
- **Lens**: 3（取消契约/吞吐）+ 2（无界计算）
- **Location**: `src/operators/runtime/detection_postprocess.cpp:150-163`（nonMaxSuppression 双层循环）、`:166-187`（dedupDetections）；调用点 `src/operators/runtime/detection_tile_engine.cpp:424`（dedup 在 `context.throwIfCancelled()` 之前执行）
- **Code**:
  ```cpp
  // detection_postprocess.cpp:150-159 —— 双层全扫描，无取消注入点：
    for ( std::size_t i = 0; i < ordered.size(); ++i )
    {
      if ( suppressed[i] )
        continue;
      kept.push_back( ordered[i] );
      for ( std::size_t j = i + 1; j < ordered.size(); ++j )
      {
        if ( suppressed[j] )
          continue;
        if ( iou( ordered[i], ordered[j] ) > iouThreshold )
          suppressed[j] = true;
      }
    }
  ```
  ```cpp
  // detection_tile_engine.cpp:417-427 —— 去重先于取消检查：
    stats.rawDetections = static_cast<int>( detections.size() );
    if ( stats.rawDetections > det.maxDetections )
      throw RSOperatorError(...);
    dedupDetections( detections, det.nmsIou );
    stats.detectionsKept = static_cast<int>( detections.size() );

    context.throwIfCancelled();
  ```
- **Root cause**: `output.detection.max_detections` 默认 100000，成为 NMS 输入的合法上界而非拒绝线；全栅格 NMS 是 O(n²)（最坏 ~5×10⁹ 次 IoU），且 `dedupDetections` 与 `nonMaxSuppression` 都不接收 `RSOperatorContext`，整个去重段不可取消。
- **Impact**: 高候选密度输入（大栅格低阈值检测头常见数万候选）时 worker 线程被去重段阻塞数十秒到分钟级；用户取消要等整段结束才生效（writeTile 后续检查才抛）。属性能瓶颈 + 取消契约缺口。
- **Trigger condition**: 检测模型 + conf 阈值低/目标密集场景，rawDetections 逼近 maxDetections（如 5 万框 → ~1.25×10⁹ IoU 比较）。
- **Evidence**: `verified-by-test-draft`
- **Reproduction**: `review/tests/F-OPS-5.cpp`：直接调用 `dedupDetections`（纯函数，无需模型）构造 100,000 个互不重叠的框，断言在 1s 内完成或提供可取消重载；现实现 O(n²) 无法达标。
- **Recommended fix**: ① NMS 前按 tile 网格分桶（空间哈希）把比较限制到邻近桶；② 给 dedup/NMS 传入 `std::function<bool()>` 取消谓词，内层每 N 次迭代检查；③ 或将 maxDetections 的默认值从"拒绝线"语义改为"NMS 输入预算"并按批分块。
- **Dedupe**: new——#620/#701 的 unbounded 家族针对 MCP vector_inspect/harness；检测 NMS 的 O(n²)+不可取消未被既有 issue 覆盖。
