# PATH_OWNERSHIP — R29-T02 实际改动文件（本轨所有权内）

| 文件 | 改动 | 依据 |
|---|---|---|
| src/runtime/chunk/resumable_tile_run.{h,cpp} | R8 publishedOutputValid 回调 + marker 降级 | 主写区 src/runtime/chunk/** |
| src/operators/framework/chunked_run.{h,cpp} | verifyPublishedOutput 选项 + 默认验证 | 主写区 operators/framework/chunked_run* |
| src/operators/framework/rs_operator.h | requiresHostQtThread() 契约（加法） | 执行运行时契约归 T02 |
| src/operators/rs/rs_spectral_index_operator.cpp | 身份戳 + 输入指纹 + verify 回调 | 任务文件指定整文件归 T02（只改身份/缓存/发布/恢复，不改公式） |
| src/data/virtual_raster_preflight.cpp | targetDiffers 分支 | 主写区 src/data/** |
| src/data/providers/virtual_raster_source_provider.{h,cpp} | WP06 warp 路径 + pinned grid + structureFromDataset 提取 | 主写区 src/data/** |
| src/agent/cartography/cartography_operators.cpp | requiresHostQtThread×3 + requireQgisHost 线程检查 | 任务文件指定本批归 T02 |
| src/jobs/job_engine.cpp | host lane dispatch | 主写区 src/jobs/** |
| src/processing/framework/task_center.cpp | waitForTask/Pipeline 事件切片 | 主写区 task_center* |
| tests/test_r29_t02_published_claim.cpp, test_r29_t02_virtual_warp.cpp, test_r29_t02_chains.cpp | 新增回归 | 新增 test_r29_t02_* |
| tests/CMakeLists.txt | 追加式注册块（R29-T0X 块内） | 共享热点，最小接线 |

不改动：rs_change_streaming.cpp（保留 R28 校验语义）；rs_operator_context.cpp 默认 workDir（identity 已含 outputPath+内容指纹，跨工作区假绿由构造消除；共享 /tmp scratch 的 R7 锁已防并发互毁）；教学评分、科学公式、T01/T03/T04 模块。
