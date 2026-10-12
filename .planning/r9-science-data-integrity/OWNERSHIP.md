# OWNERSHIP — r9-science-data-integrity（方向 A）

## 本分支修改的文件与理由

（随轮次更新；最终以 PR 文件清单为准）

## 跨方向边界遵守

- A 拥有：src/analysis/**、src/geospatial/**、src/data/**、src/dataset/**、src/experiment/**、src/experiment_studio 非 UI 核心、src/verification/**、src/scientific_state/**、src/suitability/**、src/core/{temporal_cube,radiometric_state,qgsdatasourceresolver}*、src/processing/algorithms/** 纯算法核、对应 tests/test_{analysis,dataset,geospatial,experiment}*。
- 不拥有：src/operators/runtime|io/**、src/processing/framework/**、src/workflow/**（B）；src/app/**、src/gui/**、src/teaching*/**、src/help/**、公共 tests infra（C）。
- 跨界最小变更（均在 PR 描述明示，供后续串行集成）：
  1. `src/operators/framework/model_catalog.cpp` — kStates 增加 sigma0/beta0 两 token（B 侧共享文件，纯追加，无契约语义变化）
  2. `src/operators/framework/rs_json_params.cpp` — fileExists() 路由 QgsDataSourceResolver::localPath()（review 建议路径）
- 相邻问题记录不修：#1469 agent/mcp_server recording 顺序（B）；asset_state_provider 无锁读写（science_context，C 协调）；SHA-256 fork 合并（结构性债）；spatial_split/temporal_cube 分层债（#1467）。
