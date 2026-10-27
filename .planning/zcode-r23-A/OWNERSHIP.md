# r23-A OWNERSHIP

## 可写(白名单)
- src/processing/algorithms/**(科学算法内核)
- src/analysis/**(分类/分割等分析库)
- src/operators/rs/**(rs: 算子,排除纯模型推理包装)
- src/science_context/**(仅科学规则,不动资产持久化)
- 有关科学测试/基准:tests/ 内新增/追加既有测试文件;tests/CMakeLists.txt 仅 append-only
- .planning/zcode-r23-A/**

## 只读
- src/operators/runtime/**(禁改 tile_inference_engine.cpp 等)
- src/data/**, src/app/**

## 禁区(不动)
- 模型运行时/TileInferenceEngine、STAC/远程缓存/数据管理、Pi Harness、Qt GUI/MapSpec
- src/processing/framework|gdal|providers 等白名单外文件(除非仅加测试且证据充分时另行评估,默认不动)

## 冲突协调
- tests/CMakeLists.txt:append-only;push 前 `git show origin/master:tests/CMakeLists.txt` 与工作树逐字节对照兄弟轨注册块
- 公共根文档(README/CLAUDE.md 等):不改;报告一律进 .planning/zcode-r23-A/
