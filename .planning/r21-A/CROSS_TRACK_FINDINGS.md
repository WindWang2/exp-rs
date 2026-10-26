# CROSS_TRACK_FINDINGS — r21-A

## CT-R21-A-1 → 02 轨（src/core，数据/模型/运行时）— sceneInstant 路径日期误解析

- **位置**：`src/core/temporal_cube.cpp` `sceneInstant()`（约 :84-96）→ `parseIsoDate(path)`（约 :99+）。
- **缺陷**：`parseIsoDate` 在**完整路径**上先做 `\d{8}` 全局扫描，返回第一个可解析为 yyyyMMdd 的 8 位数字串。路径前缀中的 8 位连续数字（如 worktree/构建目录名 `exp-rs-r21-A-20261023T184835Z` 中的 `20261023`、或任何带时间戳的目录）会先于文件名被当成采集日期。
- **重现**：把任意依赖文件名日期的 TemporalCube 测试/工作流放进含 8 位数字的目录即触发。实证：本轨 phenology e2e 在 worktree 路径 `/home/kevin/project/exp-rs-r21-A-20261023T184835Z/build-r21-A/...` 下 46 个场景全部解析为 2026-10-23，`TemporalCube::open` 拒绝 "scene span [2487,2487] contains no regular 16-day node"（3 个用例红）；同一代码在无数字路径 `/tmp/wtprobe/b/...` 下 675 断言全绿。
- **建议修复（属 src/core，本轨禁区）**：`sceneInstant` 应先对 `QFileInfo(path).fileName()` 做文件名解析，再回退完整路径；或在 metadata 分支已有 acquisitionTime 时直接返回（该分支已存在，只是本类 GTiff 无元数据）。
- **影响面**：`src/core` 属 B 轨所有权；本轨不修，仅登记。任何把 fixture/产品放在带数字路径下的时序工作流都受影响。

## CT-R21-A-2 → 02 轨 — #1591 的 OBIA 下游契约已在本轨对齐

- 本轨为修 #1591 相关 KMeans 重映射语义（AG5-3）而让 `RsClassRaster::paint` 接受 class id 0
  （与头文件"0 写 NoData"及 OBIA 调用方"缺席段画 0"契约一致）。该文件在 `src/analysis/segmentation`
  （本轨白名单），非跨轨；但若 02 轨另有 paint 调用方假设"ids>=1"，需知悉此契约变更。
