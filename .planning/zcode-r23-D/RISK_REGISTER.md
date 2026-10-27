# RISK_REGISTER — R23-D

| 风险 | 等级 | 缓解 |
|---|---|---|
| foundry 校准规则在本机（Windows/GDAL 3.12.4）量测与 overlay 钉值偏差超容差 → case 1 红 | 中 | overlay 容差本身设计吸收跨平台（mean±0.01、sigma 窗口宽、foundry 字节确定性契约）；若仍红，现场量测后按同一推导方法重钉并记录（不允许为过测试放宽容差语义） |
| WP-D2 修复改变 loadAny 错误消息文本，破坏依赖消息串的测试 | 低 | 仅新增 catch 分支；原 parse-false 路径文本不变；test_lab_runtime 全量回归 |
| WP-D4 拒绝 ±inf 边界破坏既有合法 labspec（若有生产文档用了 1e999） | 低 | data/labs 全目录扫描 `1e999`/大数值字面量（验证 0 命中后才合并） |
| tests/CMakeLists.txt append-only 与他轨并发冲突 | 中 | 只在文件末尾追加独立块；push 前 fetch + 三方合并保留并集；#1715 类冲突标记扫描 0 |
| 基线构建慢（-j2、内存 78%）阻塞验证 | 中 | 分目标增量构建；已完成的 baseline 复用；不升 -j4（协议 RSS>70% 降档条款） |
| 与 A/B/C/E 轨同时 push 的 merge-loss | 中 | push 前 re-fetch + 真三方 diff；本轨生产文件全部专属，共享面仅 tests/CMakeLists.txt 尾部 |
| jsoncpp 版本行为差异（throw vs return false） | 已消 | vcpkg 源码 1.9.6 现场证实 throwRuntimeError（json_reader.cpp:1040）+ reader.h 文档；测试在真链接库上跑 |
