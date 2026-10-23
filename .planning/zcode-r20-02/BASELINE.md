# R20-02 BASELINE — 地理空间数据管理、模型推理运行时与持久化可靠性

## LIVE recon（2026-10-23 00:0x CST，全部命令实测）
- `git fetch --prune origin` → `origin/master = 28e1891390bb812dc7ef2fe045ac90492c9525f1`（与 2026-10-22 快照一致，无新提交；HEAD == origin/master，0 ahead / 0 behind）
- `gh pr list --state open` → **空**（最近合并：#1663/#1664/#1665/#1666/#1667，全部 2026-10-21）
- `gh issue list --state open --limit 500` → 126 条（P1×24、P2×~60、P3 技术债汇总×~30 等，全部 2026-10-18 swarm 提交，基线 2d2881631f）
- `git branch -a -vv` / `git worktree list` → 远程仅 master；本机历史 worktree 均为旧轮次（flash/rs14/hardening/r18/r19），无其他 r20 轨 worktree；并行轨 r20-01 于 00:06 起持全局锁构建（实测 flock 排队生效）
- 必读文档已读：CLAUDE.md、.planning/r19-01/{FINAL_REPORT,TEST_RESULTS,LOOP_LEDGER,FIX_DESIGNS}.md、r19-02/r19-03 FINAL_REPORT（git show 确认在 master）

## Worktree / 分支
- 分支：`agent/r20-02-data-model-runtime-20261022T160551Z`（基线 = origin/master @ 28e1891390）
- 路径：`/home/kevin/projects/rs-studio/exp-rs-r20-02-data-model-runtime-20261022T160551Z`
- 构建目录：`build-r20-02`（本 worktree 唯一）；全局锁 `$GIT_COMMON_DIR/exp-rs-zcode-global-build.lock`，-j2 默认

## 构建/宿主配方（实测）
- cmake 4.4.4 / Unix Makefiles / Debug / ENABLE_TESTS=ON / ENABLE_LOCAL_BUILD_SHORTCUTS=ON；系统 GDAL/PROJ/GEOS（/usr/lib/cmake/*，主仓 build-dev 同配方实证可用）
- QT_QPA_PLATFORM=offscreen 跑测试；ctest 在 /usr/sbin/ctest 可用
- 注意：shell 每次调用后 cwd 重置 → 全部用绝对路径

## 合并存续验证（静态部分；运行时回归在 TEST_RESULTS.md）
R19-01 四项修复在 master@28e1891 全部存续（grep/sed 实读）：
1. zarr.json 两段扩展名 → format_profiles.cpp:600-611 bestLen 最长后缀匹配 ✅
2. setRange 运行时强制 → operators/framework/rs_param_validation.{h,cpp} + rs_operator.h:224 ✅
3. 4MiB 有界元数据读 → geospatial/products/cn_product_metadata.cpp:130-154 + product_adapters/sensor_profile 孪生 ✅
4. 隔离 worker 路由设门 → 493ab3e5fc（sicnu_worker 先 validateParams）✅

## swarm issue 静态分诊（详见 ISSUE_TRIAGE.md）
- 本轨域 P1 全部已修：#1541/#1542/#1543/#1544*/#1545/#1546/#1547/#1548/#1553/#1554/#1555/#1556（*#1544 在 operators/rs=01 轨域，master 已修）
- 本轨域 P2 抽查全已修：#1575/#1577/#1578/#1579/#1580-#1587/#1595(部分)/#1603/#1604/#1605
- P3 汇总抽查已修：#1635-1/#1637-1/-2/-3/-4/#1642-1/-2/-3/-4/#1647-2
- 唯一残留：#1597 PARTIAL（pipeline/workflow/data/catalog-export 的 CLI 未知 flag 静默忽略）→ `src/cli` 非本轨权域 → CROSS_TRACK_FINDINGS
