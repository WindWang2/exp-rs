# MILESTONES — cloud-data-fabric-datacube-10

| # | 里程碑 | 验收 | 状态 |
| --- | --- | --- | --- |
| M0 | 基线 + planning 落档 + 首提交 | git check-ignore 无输出；GOAL/BASELINE/... 全部被跟踪 | ✅ 进行中 |
| M1 | fabric/ 契约头全部编译通过 | configure 成功 + 头自包含 TU | ⬜ |
| M2 | object_store + catalog_service 测试绿 | ctest -R test_io_fabric_object_store\|test_io_fabric_catalog → 0 fail | ⬜ |
| M3 | virtual_cube + chunk_plan 测试绿 | ctest -R test_io_fabric_cube\|test_io_fabric_plan（chunk 部分）→ 0 fail | ⬜ |
| M4 | planner + prefetch/mirror + 算子/CLI | test_io_fabric_plan/cache/operators 绿；CLI 冒烟 | ⬜ |
| M5 | 规模证据 | test_io_fabric_scale 绿 + PERFORMANCE.md 落档 | ⬜ |
| M6 | 边缘矩阵收尾 + F-OPS-4 裁决 | 各 fabric 测试含边缘 case；DECISIONS D-1013 回填 | ⬜ |
| M7 | review P0/P1 清零 | REVIEW_LOG.md 全 finding 有 disposition | ⬜ |
| M8 | PR 创建（不 merge） | gh pr view → OPEN | ⬜ |
