# MILESTONES — cloud-data-fabric-datacube-10

| # | 里程碑 | 验收 | 状态 |
| --- | --- | --- | --- |
| M0 | 基线 + planning 落档 + 首提交 | git check-ignore 无输出；GOAL/BASELINE/... 全部被跟踪 | ✅ |
| M1 | fabric/ 契约头全部编译通过 | configure 成功 + 头自包含 TU | ✅ |
| M2 | object_store + catalog_service 测试绿 | 两套件绿（75+56 断言） | ✅ |
| M3 | virtual_cube + chunk_plan 测试绿 | test_io_fabric_cube 绿（70 断言） | ✅ |
| M4 | planner + prefetch/mirror + 算子/CLI | test_io_fabric_plan（57）+ operators（40）绿；CLI 冒烟通过 | ✅ |
| M5 | 规模证据 | test_io_fabric_scale 绿（32 断言，含 RSS 实测） | ✅ |
| M6 | 边缘矩阵收尾 + F-OPS-4 裁决 | corrupt-mirror/typed/offline 入 scale 套件；F-OPS-4 窄修复+回归 | ✅ |
| M7 | review P0/P1 清零 | REVIEW_LOG.md 全 finding 有 disposition（36+4 项全处置，P1×5 全 fixed） | ✅ |
| M8 | PR 创建（不 merge） | PR #974 OPEN | ✅ |
