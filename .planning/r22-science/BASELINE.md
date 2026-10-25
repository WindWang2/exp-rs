# R22-01 science 轨道基线（2026-10-25T13:33Z）

## 远端与本地一致性
- `gh api repos/WindWang2/exp-rs/commits/master` → `911e0b17a3533ca0d79c1b68b000d6052c4c3c61`
- 本地 `origin/master` → `911e0b17a3533ca0d79c1b68b000d6052c4c3c61`（一致）
- `git fetch origin master` 直连 https 超时（300s），改用 gh api 验证远端 SHA（任务书 §1 允许）。fetch --prune 亦超时；后续 push 前重试。
- `gh repo view` → defaultBranch=master, viewerPermission=ADMIN。

## 工作区
- 主仓：`/home/kevin/project/exp-rs`（master @ 28e1891 干净，未改动）
- 本轨 worktree：`/home/kevin/project/exp-rs-r22-science-20261025T133250Z`
- 分支：`agent/r22-science-20261025T133250Z`（基于 origin/master @ 911e0b17）
- 其他 R22 轨（data @ 911e0b17、inference @ 911e0b17）已存在，本轨不触碰。

## Open PR（截至 2026-10-25T13:30Z，共 5 个）
| PR | 轨 | head | 状态 |
|---|---|---|---|
| #1673 | R21-B data/model-runtime | 941af0d1 | OPEN |
| #1674 | R21-E teaching-verification | 0d919f50 | OPEN |
| #1675 | R21-C pi-mcp-harness | d088f0a4 | OPEN |
| #1676 | R21-A scientific-algorithms | ec7d85ef | OPEN（与本轨高关联） |
| #1677 | R21-D qt-qgis-cartography | 37c5f539 | OPEN |

## #1676（R21-A）排重结论（源码级，非 issue 口头）
- `math_utils.cpp`：#1676 抽出 `bothNormalizable` 帮助函数（与 master 的内联判断等价），
  但 `computeStatsFromAccumulators` 仍为 `stats.mean = static_cast<float>(...)` 后
  `variance = acc.sumSq/count - stats.mean*stats.mean` → **#1684 未被 #1676 修复**。
  两改动不同函数，合并冲突风险低。
- `rs_kmeans_operator.cpp`：#1676 只改 scaler 拟合时序（~L330），不触及 L221 reservoir
  的 `std::mt19937+std::uniform_int_distribution` → **#1685 kmeans 点未被修复**。
  修改区域不同，冲突可控。
- 其余 #1685 调用点文件（split/training/multires/endmember/feature_matcher）不在 #1676
  改动列表中 → 无冲突。
- #1707（distance_transform.cpp）、#1686（resampler.cpp）不在 #1676 改动列表中 → 无冲突。

## 宿主构建事实（承 R21-A run-tests.sh）
- 系统 Qt 6.12 链接；vendored GDAL 依赖 paleo prefix 的 ~20 个传递库；
  `/tmp/r21a-libs`（非 Qt 依赖 SONAME shim）与 `/tmp/r21a-deb/libwrapper2stub.so`
  （flang wrapper2_* 符号桩）仍存在，直接复用。
- `PROJ_LIB=/home/kevin/project/pw-seismic-runtime-closure-20260930-215353/vendor/prefix/usr/share/proj`
- 共享构建锁 `/tmp/exp-rs-r21-build.lock`（七轨统一）；`CMAKE_BUILD_PARALLEL_LEVEL=4` 硬上限。
- 内存 47Gi 可用 / 磁盘 343G 可用（2026-10-25T13:33Z）。

## 构建配置命令
见 `LOOP_LEDGER.md` 轮次记录；build 目录 `build-r22-science`（本轨独占）。
