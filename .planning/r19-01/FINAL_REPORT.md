# R19-01 FINAL_REPORT — 空间数据 × 模型运行底座

**PR**: https://github.com/WindWang2/exp-rs/pull/1667（OPEN，32 文件 +1014/−16，未自动合并）
**分支**: `agent/r19-01-data-model-runtime-20261020T102418Z` @ `ff982387b5`，基线 `e12a9afbc`（= PR 时点 origin/master，无 rebase 需求）

## 交付总览
| 类别 | 数量 | 明细 |
|---|---|---|
| 源码修复 | 4 项 | zarr.json 两段扩展名不可达（#1637-2）· setRange 运行时强制（#1578，生产三漏斗 + execute 五入口）· CN/通用/传感器元数据 4MiB 有界读 ×3（#1637-3 及孪生）· 隔离 worker 路由设门 |
| 测试修复 | 1 项 | chunk governor 两处期望漂移（Advisory 枚举 + F-A-13 记账），sweep 发现的预存红 |
| 新测试 | 5 组 | 全部真 RED→GREEN（断言明细见 ORACLES.md）|
| 独立评审 | 2 轮 | R1 + R2 均 REQUEST-CHANGES → 全部 P0-P2 闭环（REVIEW_R1/R2.md）|
| 移交他轨 | 9 项 | CROSS_TRACK_FINDINGS.md（C1-C7 + X1 残余入口清单）|

## Oracle 完成态（O-A1..O-A6）
- **O-A1 ✅** 双种子客观纠偏：#1548/#1456 在当前 master 基线即绿（退出码留档）；#1662 的"仍红"判定不复现
- **O-A2 ✅** SSRF/offline/identity 负向矩阵（remote_validator 232 + range_cache 314 + remote_range 25 + uri 211 + stac 27 断言）**两遍全绿**
- **O-A3 ✅** 引理证据复用：model 家族（runtime_8/9/failure_matrix/tasks 等）在终码 sweep 全绿；golden/OOM/半发布矩阵未回退
- **O-A4 ✅** chunk/session 确定性：chunk_graph（34 用例含 #1553 钉）+ chunked_fabric_e2e 606,370 断言全绿（预存漂移修复后）
- **O-A5 ✅** dataset/experiment 全族 sweep 绿；txn/atomic/version_migration 既有覆盖引用
- **O-A6 ✅** 两轮独立 review 闭环 + master 未动无需 rebase（post-push diff --check clean）+ **PR #1667 创建成功**

## 测试与构建证据
- 终码全域 sweep：**84 PASS / 0 FAIL / 0 TIMEOUT** + 1 NOT_EXECUTED（test_model_runtime_bench：源码明示 env-gated 设计性跳过，不谎报）
- 全程全局 -j2 锁（与 r19-02/r19-03 实测互斥）；每二进制 600s 上限
- 宿主排障配方固化：vendor 钉库（GSL/netCDF/SQLite3/EXPAT/ZLIB/ZSTD/PCRE2_8/QCA）· rpath-link 顺序（PWLIB→usr/lib→pulseaudio→vendor）· 无 ctest 宿主的直跑驱动 · PROJ_DATA 注入

## 遗留（STILL_OPEN / 已声明）
- X1 残余未设门入口（pybind / batch dialog / plugin adapter）——他轨写集，CROSS_TRACK 已列（各一行可闭合）
- TSan 未运行（未标 PASS）；Windows lane 未跑（A11 声明：本轨改动平台无关，但未验证）
- push 首次 SSL 中断后重试成功（记录于 ledger）

## Issue 关联建议（供维护者）
本 PR 盘点证实已修但未关的 issue：#1545 #1546 #1547? #1553 #1554 #1579 #1580 #1581 #1582 #1583 #1584 #1585 #1586 #1587 #1595 #1597 #1603 #1604 #1605（+#1467 P1 双项）可随本 PR 或独立关闭；本 PR 直接修复：**#1637-2、#1578、#1637-3（含孪生）**
