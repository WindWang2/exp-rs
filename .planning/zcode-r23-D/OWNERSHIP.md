# OWNERSHIP — R23-D 写区声明与机器可验证白名单

## 允许写（本轨白名单，正则）
^src/lab/
^src/lab_pack/
^src/grader/
^src/recipes/
^src/faultlab/
^src/verification/
^src/verify_adapters/
^data/labs/          （最小差分）
^data/faultlab/      （最小差分）
^tests/              （仅新增本轨专属测试文件 + tests/CMakeLists.txt append-only 注册）
^\.planning/zcode-r23-D/
^docs/               （仅与本轨改动直接相关的文档）

## 本 PR 计划触碰（预注册，push 前机器复查实际 diff 与此一致）
- data/labs/grading/ndvi_landsat.rules.json（新增，foundry 场景校准规则）
- src/lab/session_store.cpp（readJsonFile try/catch）
- src/recipes/recipe_registry.cpp / provider_interfaces.cpp / lab_source.cpp / lab_document.cpp（bounded read）
- src/lab/spec_runtime.cpp（expected_numeric isfinite 门）
- tests/test_r23_d_*.cpp（新增，≥1 文件）+ tests/CMakeLists.txt（append-only 注册块）
- data/labs/ 相应最小文档（若必要）
- .planning/zcode-r23-D/**

## 严格禁写（其他轨 + 共享高风险）
src/teaching/**, src/teaching_admin/**, src/app/**, src/verify/**, src/help/**, src/ui/**（C）；
src/data/**, src/dataset/**, src/experiment/**, src/experiment_studio/**, src/geospatial/**（B）；
src/analysis/**, src/processing/algorithms/**（A）；
src/native/**, src/plugins/**, src/python/**, cmake/**, packaging/**, scripts/**, .github/**, tools/**（E/灰区）；
根 CMakeLists.txt（不动）；tests/CMakeLists.txt 仅 append-only。

## 白名单机器校验（push 前执行）
git diff --name-only origin/master...HEAD | 逐行匹配允许前缀；tests/CMakeLists.txt 若改动则 diff 必须纯新增行（不含删除/修改现有行）；共享文件零改动。脚本见 tools/check_whitelist.sh。
