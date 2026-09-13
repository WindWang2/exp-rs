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
