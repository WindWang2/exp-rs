# REVIEW_R1 — r21-A

评审代理：fresh general-purpose reviewer（独立上下文，未参与实现；独立采样；主动寻找反例）。
输入：`git diff 911e0b17...HEAD`（19 commits）+ .planning/r21-A 全部文档。
评审者复跑 16 个测试族，断言数与 TEST_RESULTS 完全一致。

## 结论

- P0/P1：无。
- P2：1 项（R1-1，必修）。
- P3：6 项（R1-2..R1-7；其中 4 项本轮修复，2 项记录不修）。
- merge-loss：#1671 七个契约文件全部未被触碰；无回滚。
- 所有权：仅 .gitignore 的 .planning/r21-A 白名单段落在三目录+tests 之外（允许的 append-only）。

## R1-1（P2，必修）move-assignment 残留目标端缓存

- 位置：src/analysis/segmentation/rs_segment_map.cpp:72-89（移动赋值）
- 问题：只重置源端缓存，未重置目标端 mSizeCache/mSizeCacheBuilt/mCoordsCache/mCoordsIndexBuilt。
  兄弟拷贝赋值（:42-56）有重置 → 意图明确。场景：a.pixelCount(1) 建缓存后 a=std::move(b)（b 标签不同）
  → a.mSizeCacheBuilt 仍 true → ensureSizeCache 早退 → 返回过期计数/坐标（静默错值）。
- 可达性：当前生产唯一移动赋值（rs_otb_segmenter.cpp:125）目标为冷图；测试只覆盖移动构造。
- 修复：在 if 块内补 4 行重置（镜像拷贝赋值）。

## R1-2（P3，修）DEM 未知单位不记录日志

- demUnitScaleToMetres 对未知单位返回 1.0 + appliedUnit="meters (assumed…)"，但三个算子以
  `demUnitScale != 1.0` 为日志门 → 未知单位静默按米处理，与头文件"callers should log it"矛盾。
- 修复：日志门加 `|| appliedDemUnit.contains("assumed")`。

## R1-3（P3，修）airmass 拒绝未按方法限定

- operator:1082 无条件拒绝 airmass<=0/非有限，但 airmass 只供 DOS2 使用（内核门已按 method 限定）；
  DOS1/DnToRadiance 调用方传 airmass=0 原本可用，现在会被拒。
- 修复：仅在 method 需要 airmass（dos2）时校验。

## R1-4（P3，不修，记录）isIntegral() 拒绝 JSON 3.0

- 严格性可辩护（3.0 与 3 语义等价但 jsoncpp 类型不同）；oracle（2.5/"3"）仍通过。保持严格，
  在 PR 正文风险段说明。

## R1-5（P3，修）移动赋值锁序（dst→src）

- 两线程互移理论上可死锁；实际不可达。与拷贝赋值（只锁 dst）对齐：改为只锁 dst，
  源端缓存在无锁条件下重置存在理论竞态但源端已是被移出对象（按 C++ 惯例其后续使用未定义），
  故采用"只锁 dst + 源端重置"一致性方案并注释说明。

## R1-6（P3，修）测试注释算术错误

- test_spectral_unmixing.cpp 注释称 LS a=(-0.1,0.8)，实际解 (0.5,0.1)。断言正确，仅注释。

## R1-7（P3，不修，记录）DEM-unit 算子接线无测试

- helper 已被 test_sar_kernels 覆盖；算子 for 循环接线简单且被 sar 家族回归间接覆盖。记录。
