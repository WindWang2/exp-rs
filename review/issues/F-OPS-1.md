# [Inference/Operators] rs:infer Labels 输出的 class_mapping 重映射值未按输出栅格编码校验——≥255 的产品类被 GDAL 钳制为 NoData 哨兵，整类像素静默丢失

P2
Affected Location: src/operators/runtime/tile_inference_engine.cpp:852-874（writeType/writeNoData 由重映射前的类数决定）与 :1595-1613 / :687-708（productClass 直接写入）；src/operators/framework/model_catalog.cpp:829-845（校验只有非负+单射）
Root Cause & Impact: 输出栅格编码（GDT_Byte/GDT_UInt16、NoData=255/65535）由模型类数选择，而像素写入的是 class_mapping 的目标值；清单校验不约束目标域上界。manifest 声明 class_mapping 值 ≥255 时，float→Byte 写入钳制为 255==writeNoData，该类像素读回即 NoData，palette 与 class_pixel_counts 仍声称该类存在——静默数据损坏，无任何报错。subagent V 独立复核确认：无任何一处存在 255/65535 上界检查。
Reproduction: review/tests/F-OPS-1.cpp（Catch2 草稿）——validateManifestJson 对 class_mapping [0,1,300] 放行（现实现 issues 为空）；运行路径断言类 2 像素值==2（现实现得 255）。
Recommended Fix: parseManifest 对 class_mapping 增加上界校验（≤254，或与 writeType 联动选择 UInt16/NoData=65535）；engine Labels 分支按 1+max(classMapping) 选择编码（stats.classPixelCounts 已经按此域分配，唯独栅格编码漏了）。
