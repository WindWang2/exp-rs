# CROSS_TRACK_FINDINGS — R25-E

## CT-E1（→ C 轨，src/agent/output_verifier.cpp，本轨只读）
验证证据链的 pixel-size 缺口（W3 侦察确认，非缺陷而是记录在案的能力边界）：
- `verify_adapters/lab_verify_composer.h` 自述 "the grid re-check pins width/height/bandCount/crs only — the rules' pixel_size_* constraints stay the grader's own assertion (the authority's GridInfo carries no geotransform)"。
- `src/agent/output_verifier.cpp:936 verifierGridPins()` 从 crs_grid 断言提取 pins 时只读 width/height/band_count/epsg，不读 pixel_size_x/y/tolerance。
- 若 C 轨愿意接：`GridInfo`（src/verify/verify_context.h，E 可写）加 pixelSizeX/Y + gdal_grid_probe 读 geotransform + evalArtifactGrid 断言 + output_verifier 6 行提取。E 侧文件全部就绪意愿明确；仅 src/agent 提取归 C。本轨不越权实现（避免双写 src/agent）。
- 现状安全性：pixel_size 由 grader 自身 crs_grid 断言检查（output_verifier），无漏检；只是验证权威的独立证据包不含它。

## CT-E2（信息，无动作）
- `grade_all.cmd` 默认 `--lab ndvi_basics` 是参数化工具的默认值（教师可传 lab_id），非缺陷。
- `scripts/offline_smoke.sh` 用 bundle 自己的 overlay 规则目录（`SICNU_LAB_RULES_DIR=$bundle_dir/data/labs/grading`），其 `ndvi_basics` 即 foundry 校准权威——正确，勿改。

## CT-E3（→ 后续教学轨）
- r22-teaching 遗留：rubric/lab 文档作者应开始标注 `version` 字符串（gradingVersionLabels 优先取 authored version，否则显示 fp:<hash12>）。data/labs/grading/*.rules.json 均无 version 成员——建议后续为权威规则文件补 version 标注（规则文件属 E 所有权，但改动会变 checkpoint digest 使在途班级重跑，本轨选择不动在途权威）。
