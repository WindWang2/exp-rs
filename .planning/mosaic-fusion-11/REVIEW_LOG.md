# REVIEW_LOG — 独立 review findings 与 disposition

## Round 1（主 agent 全 diff review，commit 前）

| Finding | Severity | Disposition |
|---|---|---|
| provenance winnerWeight 在三方混合时 dominant 判定不准 | P2 | **fixed** `e8b54fa7cd`：前主导权重按 (1-s) 衰减，s 超过衰减值即接管 |
| schema `rejectedInputs` 声明为 integer 实为数组 | P3 | **fixed** `e8b54fa7cd` |
| `BalanceGain::composedWith` 死代码 | P3 | **removed** `e8b54fa7cd` |

## Round 2（subagent #2 只读对抗 review，全 diff `origin/master...HEAD`）

审阅范围：完整 diff + 上下文源码（只读，无法执行测试，按代码/算术审阅）。
汇总判定原文："core mosaic algorithm layer is otherwise in good shape"（balancing 数学/gates、
确定性子采样、DP tie-break、binned seam 几何、金字塔恒等重构、feather 权重、seam side
符号代数、provenance 衰减代数、plan 南北向 placement、sampler grid→local 映射、注册三件套
+ CMake 接线均逐一验证通过）。

| ID | Severity | File | Title | Disposition |
|---|---|---|---|---|
| F1 | P0 | image_fusion.cpp | hpf 分支读未填充的 panBuf（恒 0），输出=MS−boxmean | **fixed**（review 前 main agent 独立发现并修复：中心样本取自 halo；本次随批提交）。known-answer 测试验证 |
| F2 | P1 | test_image_fusion.cpp | HPF oracle 算术错误（声称 mean=125 非 100） | **rejected**——reviewer 求和错误：0+100+100+200=400，/4=100；期望 {200,300,300,400} 正确且测试对修复后实现实际通过（83/83） |
| F3 | P1 | fusion_quality_report.* | BandAccum::maxY 从未更新，SSIM 的 L 恒为 1 | **fixed**：addWindow 累积运行最大 \|y\| |
| F4 | P1 | rs_quality_mosaic_operator.cpp | 用户 priority 被静默忽略（空 priorities 传入 compositeOrder） | **fixed**：priorities 透传；符号约定端到端理顺（"高者优先"与 plan 一致，tie-break 改为 pa>pb，文档+测试同步）；新增 provenance 优先级 E2E 回归 |
| F5 | P2 | fusion_quality_report.cpp | 零方差常量带 CC=0 误触发守卫 | **fixed**（review 时已在工作区：残差≈0 → CC=1；已在 test_image_fusion 常量场景覆盖），本次随批提交 |
| F6 | P2 | fusion_quality_report.cpp / operator | remove-before-rename 破坏原子性声明 | **fixed**：POSIX 直接 rename（原子替换）；remove-then-rename 仅作 Windows 回退；两处发布路径 + 注释同步 |
| F7 | P2 | mosaic_plan.cpp | Y 向镜像场景 grid-ineligible 但无诊断（balancing 关闭时被静默丢弃） | **fixed**：新增 `yDirectionMismatch` 诊断 + actionable warning；operator 的 fail-closed 过滤随之覆盖 |
| F8 | P2 | mosaic_balancing.cpp | 每带 5 次全 overlap 重读的 I/O 放大 | **mitigated + documented**：首次 refit 后 inliers==全部 即提前收敛（干净 overlap 3 遍）；最坏情形写入头文件文档 |
| F9 | P3 | mosaic_balancing.h/cpp | 头注释与实现不符（"composed with parent"/bias gate 单位/"Theil-Sen" 命名） | **fixed**：三处注释改写（累计校正语义、\|meanY\|+stdY 单位、LMS 命名） |
| F10 | P3 | mosaic_quality.cpp | `t < 0` 死分支 | **fixed**：删除 |
| F11 | P3 | rs_image_fusion_operator.cpp | `qualityPassed` schema 声明 string 实写 bool | **fixed**：boolean 类型 |
| F12 | P3 | mosaic_plan.h | OverlapPair 注释声称 a<b 数值序不变量不成立 | **fixed**：注释改为"composite order（非数值序）" |
| F13 | P3 | rs_quality_mosaic_operator.cpp | estimateExecution 标 basis=dynamic 但返回静态值 | **fixed**：按输入数缩放 estimatedRamBytes |
| F14 | P3 | operator sampler | band-1 NoData 被套用到所有波段 | **fixed**：逐波段 sentinel 捕获，sampler 按带索引 |

## 终态

- P0 = 0，P1 = 0（F1/F3/F4 已修复并有回归覆盖；F2 有据驳回）。
- P2 全部 fixed/mitigated+documented；P3 全部 fixed。
- 修复后全量 suite：**84/84 passed**（追加 registry 接线回归 test_quality_mosaic_registration）（clean rebuild；ctest 用例级选择，-j1，offscreen）。
- 既有套件 test_pansharpening / test_capability_* 因 master #1000 的 pre-existing
  sicnu_agent 编译失败无法在本 worktree 构建（对照证明见 EVIDENCE.md P0 out-of-scope 节）。
