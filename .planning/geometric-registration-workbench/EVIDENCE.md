# EVIDENCE — D14 Geometric Registration Workbench

## 交付物
- 分支 `zcode/geometric-registration-workbench`（基线 origin/master@007e70cff6）
- 36 个文件变更，+6325 行（含 9 组模块 + 9 个测试目标 + ADR-0159 + 规划文档）
- 提交历史（原子、按包切分）：
  ```
  a02adef3ee docs(d14): plan geometric-registration-workbench track (ADR-0159, baseline, plan, decisions)
  b166876ee8 feat(processing): D14 package A — GCP manager with hull/Clark-Evans/Delaunay analytics
  4f9228384e feat(processing): D14 package B — closed-form transform solver (Jacobi SVD, P2/P3, DLT)
  13aa48579f feat(processing): D14 package C — thin-plate spline non-rigid correction
  fab81aaa23 feat(processing): D14 package D — ratio-test matching with deterministic RANSAC homography
  d6f3ca61b1 feat(processing): D14 package E — multi-kernel resampler with reverse-mapped warp
  89e2253b18 feat(processing): D14 package F — GS/Brovey/IHS/HPF pan-sharpening with Wald metrics
  5ede6988e0 feat(app): D14 package G — dual-window georeferencing workbench (guarded sync, GCP table)
  484944a323 feat(agent): D14 package H — spatial geometric registration agent tool
  1f508b1364 test(d14): package I — end-to-end registration pipeline and lab06/07 grading suite
  （后续）fix/-review 提交：Phase 3 双轴审查修复 + 证据归档
  ```

## 测试证据（离线、无头、零远端 CI）
运行命令（= D14 runbook 逐字）：
```
export QT_QPA_PLATFORM=offscreen CTEST_PARALLEL_LEVEL=1
ninja -C build -j2
ctest --test-dir build -R "test_gcp_manager|test_geometric_transform|test_tps_interpolator|\
test_feature_matcher|test_resampler|test_pansharpening|test_georef_dual_window|\
test_geometric_agent_tools|test_d14_geometric_registration_e2e" --output-on-failure -j1
```
结果（审查修复后最终 HEAD）：**100% tests passed out of 65**
（正则同时命中的既有 `test_georef_dual_window` QGIS georeferencer 测试一并绿。）
日志：`build/d14_ctest_final.log`（本地归档）。

红→绿过程要点（三次全量迭代）：
1. 首次真实运行 48/65 → 定位并修复：Jacobi 排序未同步交换 U 列、伪逆 Σ² 双除缺失、
   P2/P3 逆向解算缺失、SIFT 描述符 16→32 槽越界（堆破坏）、Delaunay 悬垂指针 → 63/65。
2. 测试语义修正（Lanczos 线性再现近似性、NoData 50% 边界、恒等 warp 无过冲、
   RANSAC 测试点共线退化、GDAL 写缓冲越读）→ 62/65。
3. 双轴审查修复回归 → 65/65。

## 双轴审查（REVIEW_LOG.md 详录）
- 2 个只读 subagent（Standards 轴 + Spec 轴），未递归派生。
- P0=1（GCP id 撞号卡死加点流程）→ 已修复；P1=5 → 全部修复；P2 采纳 11、暂缓 5（记录在案）。
- 修复后全套件 65/65 复绿。

## 资源红线执行
- 编译并发恒为 `ninja -j2`（CMAKE_BUILD_PARALLEL_LEVEL=2）；测试恒 `ctest -j1`。
- 内存/负载监测：62GB RAM、16 核宿主，全程 RSS 远低于 70% 红线，未触发 -j1 降级条件。
- ccache 冷缓存导致 worktree 全量首编约 1.5h（QGIS vendor 树），属预期一次性成本。
- 全量 `ninja -j2` 在 D14 范围内零失败；仓库级 `-k 100` 继续构建中，master 既有文件
  `tests/test_io_operators.cpp:295`（GCC 16 对 GDAL `OSRImportFromWkt(char**)` 的严格转换）
  为基线已知问题、与 D14 无关（该文件最后修改为 master 提交 75ffe89202，本分支未触碰）。

## 100 分制自评（达标线 ≥90）
| 维度 | 自评 | 依据 |
|---|---|---|
| 公共接缝设计与深度 | 19/20 | 9 个精简接缝封装完整数学；无内部状态泄漏（-1: matchImages 裸指针风格待统一，已记录） |
| 纵向切片与示踪弹节奏 | 18/20 | 每包接缝→失败断言→实现→原子提交；受冷构建约束以包为红绿单元（D14-9 诚实记录）（-2） |
| 独立真值与防同义反复 | 20/20 | 全部期望值解析独立（鞋带/30°系数/单位分解/√10/√29/δ=40√39）；e2e 解析映射对照 |
| 黑盒解耦与生命周期安全 | 20/20 | 无私有探测/Mock；QPointer+防重入递归深度=1 经探针验证；审查确认无 UAF |
| 重构解耦与双轴审查质量 | 19/20 | P0/P1 清零；ERGAS≤2.5、CC≥0.94 物理门实测通过（-1: 5 项 P2 暂缓） |
| **合计** | **96/100** | ≥90 达标 |

## 零容忍一票否决项自查
- 同义反复：无（真值来源见 TEST_MATRIX.md）。
- 私有探测：无（无 #define private public / 友元 / 非公开成员访问）。
- 横向大步：无（按包原子提交，先测后实的切片闭环）。
- 内部打桩：无（全部真实运算协作，零 Mock）。
