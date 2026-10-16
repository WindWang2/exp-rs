# R15-A LEDGER — Oracle-Ledger 循环记账

> 循环：分析 → RED → 最小修复 → GREEN×2 → 记账 → 下一任务。连续三轮无进展须改变假设；八轮无进展回查前提。每阶段可恢复 checkpoint。

| # | 任务 | Oracle | 状态 | 证据 |
|---|---|---|---|---|
| L0 | M0 事实冻结（BASELINE/OWNERSHIP/矩阵/ORACLES） | — | DONE | origin/master=46791bc6；0 open PR；#1505/#1509/#1512 已合并；25 issues 分类完成 |
| L1 | 构建树 build-r15-a 配置 + 9 个优先目标编译通过 | — | IN_PROGRESS | configure exit 0（VCPKG_MANIFEST_MODE=OFF + VCPKG_INSTALLED_DIR + OpenCV_DIR 修正）；2122 edges 总量；编译进行中 |
| L2 | O3：#1465 3-arg std::equal 定位与分类 | O3 | DONE（静态清扫） | 全仓 8 处：asset_state_json 4 处已有长度门卫（FIXED_ON_MASTER）；temporal_change.cpp:218 尺寸相等前置；study_spatial.cpp:169 定长 6 栈数组；qgsabstractgeometry（QGIS 只读）。结论：本分支无存活缺陷 |
| L3 | O5：rs_spectral_index #1509 diff 审计 + 回归基线 | O5 | DONE（静态） | 共享 JSON 辅助（rs_json_params.cpp getInt/getString/getDouble/getBool）全部 isNumeric/isString/isBool 门卫 + #647 int64 范围检查；rs_spectral_index_operator 全部经辅助路由，scale 参数 isNumeric+finite>0 双检；锁已随 #1509 合并解除，无残留缺陷 |
| L4 | O1：融合 resample-then-mask 边缘掺混 RED→GREEN×2 | O1 | IN_PROGRESS | RED 测试已提交（7aae63038 流式路径 + b4a7d148b 质量报告路径）；修复已备于 evidence/image_fusion.o1-fixed.cpp.bak（覆盖 862 调用点）；1845 扩展补丁已设计待 RED 后应用 |
| L5 | O2：sam/spectral_similarity NoData 方言双锁或统一 | O2 | TEST_READY | 双锁测试已写入 tests/test_spectral_classification.cpp（未提交）；sam metadata 文档已改（未提交）；待构建验证 |
| L6 | O4：#1454 科学算子回调侧审计 | O4 | PENDING | — |
| L7+ | A01–A16 工作包审计新发现（按复现准入排队） | O6+ | IN_PROGRESS | 见 FINDINGS.md；已完成 A02/A03/A13 静态审计 |

## 阻塞与假设变更记录

- 假设变更：vcpkg manifest 模式在本环境不可用（reg.exe 被拦 → SDK 探测失败 → 编译器哈希不匹配），切 classic 模式 + 复制主仓 vcpkg_installed 树；configure 两次误删复制树后已恢复。
- 假设变更：不跨树复制 build-dev 的 6430 个预编译 .obj（保 M4 隔离构建树证据链纯净）。

## Checkpoint 索引

- CP0（本轮）：M0 完成，分支 `hardening/r15-A-science-processing` @ 46791bc6 + M0 文档提交。
- CP1：configure exit 0；O1 RED 测试提交（流式 + 质量报告两路径）；O2 双锁测试就绪；A02/A03/A13 静态审计完成（详见 FINDINGS.md）。
- 下一 session 恢复点：读本文件 + FINDINGS.md + BASELINE.md，从 L4 RED 运行继续（等 test_image_fusion 构建完成）。
