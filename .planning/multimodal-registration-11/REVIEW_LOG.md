# REVIEW LOG — multimodal-registration-11

## Subagent #2 对抗 review（Phase 7）

Reviewer：Explore subagent #2（只读）。范围：`git diff origin/master...HEAD` 全量（63 files, +6450/-53）。
结论：P0=1、P1=2、P2=2、P3=8（13 findings 全部有 disposition，见下）。
Reviewer 明确验证为 clean 的部分：FFT 约定与符号、亚像素抛物线、MI 归一化、CE90 最近邻秩与 Rayleigh 参照、
stack 正规方程/消元/断连排除、RANSAC 复用（seed 42）、全部 caps、确定性声明、QGIS transformer 未被修改、
两个 1 行 unblocking fixes 的正确性与最小性、构建接线（CMake/registry/knowledge/.gitignore）。

| # | 严重度 | 位置 | 摘要 | disposition |
|---|---|---|---|---|
| 1 | P0 | rs_register_images_operator | warp 把像素空间仿射当世界坐标用 + gt 未随抽点缩放 → 静默错位产物 | 已修：warp 改为 identity gt（纯 buffer 像素空间），输出挂抽点感知的 refGtBuf；readBand1Bounded 返回 raw dims；新增内容级 oracle（真实 10m/px y-flip gt，mad<3）——该测试先暴露并连带修复了 WarpOptions 默认 clampRange[0,1] 的第二处产物缺陷 |
| 2 | P1 | model_selector CV | 空 test fold → NaN cvRmse → 选择被静默污染 | 已修：空 held-out split 跳过；非有限 cvRmse → degenerate_geometry infeasible |
| 3 | P1 | #1005 fix | invalid layer CRS 一刀切 nullopt 阻断"无 CRS 栅格配准"主工作流 | 已修：invalid layer CRS = 像素空间语义，保留 master 直通（附测试）；invalid canvas CRS 才拒绝 |
| 4 | P2 | rs_register_images | quality 锚定 refW/H，但点在 source 网格 | 已修：evaluate/residualField 传 srcW/srcH |
| 5 | P2 | matcher 头文件契约 | low_peak_snr 状态、resource_exhausted 名称、LowConfidence 下 H 语义三处与实现漂移 | 已修：头文件对齐实现（Refused 含 low_peak_snr；cap_exhausted；LowConfidence 携带 H 供复核） |
| 6 | P3 | docs/reason codes | transformer 消费写成本进行时；io_error 无生产者；model_not_justified 归属；全 kappa 拒绝时 reason | 已修：docs 改 follow-up 语气；io_error 标 reserved；types.h 注释归属 RPC bias 层；ModelSelector 新增 ill_conditioned 优先级 |
| 7 | P3 | stack 键名双写 | fromId/from_id 两套拼写；select_model 的 gcps 键未在 schema 描述 | 已修：operator 双拼写兼容；schema 描述按 action 分列键名 |
| 8 | P3 | 错误路径部分产物 | sidecar 失败时 raster 已存在 | disposition（docs）：docs 补错误路径语义说明（raster 先写、sidecar 失败报错不静默）；GDAL temp+rename 超出最小改动，列为 follow-up |
| 9 | P3 | atomic writer 短写 | write()<0 漏短写；kMinCe90Samples 引用名；meanResidual 语义 | 已修：按字节数比较；头注释改 ce90MinSamples（字段名不改，公共 API 稳定优先）；meanResidual 注释改为"相对中位数偏移的空间变化分量" |
| 10 | P3 | matcher 边界 | win==dim 时空中心范围 → low_peak_snr 误导；estimateScratchMiB 漏 mags 面 | 已修：win 钳到 minDim-2；estimate 计入第 4 块缓冲并同步 pinned 测试 |
| 11 | P3 | executionEstimate | 硬编码 1024 基准与 maxDim 4096 上限差 30x | disposition（部分）：est 公式按 1024 基准显式注释（调度估计与默认值一致；按请求缩放属 operator 框架能力，非本层） |
| 12 | P3 | 负例缺口 | cap_exhausted/maxMatches/low_consensus/算子 <3 inlier 无测试 | 已修（3/4）：cap_exhausted、maxMatches 截断、算子 <3 inlier → RSOperatorError 已加；low_consensus 无确定性 fixture（6 度场景在 matcher 强化后正确收敛为 Success——恰是紧共识豁免的预期行为），disposition 记录 |
| 13 | P3 | rpc_bias 注释 | un-center 注释的基序与代码不符 | 已修：注释按 {x,y,1} 基序重写 |

## 主 agent 自审（Phase 6 末，先于 #2 review）

- 发现并修复：金字塔粗层空中心范围（dim==win）、相位白化缺失（SNR 2.7 → 频谱白化后正常）、
  粗层窗口密度不足导致的周期旁瓣锁定（粗层窗减半）、minScore 按度量校准（MI 0.10）、
  coverage 全幅化（Oracle #2）、stack 断连全零行（degenerate 拒绝）→ 限连通子图、
  #1005 语义拆分（layer 无 CRS 直通 vs canvas/transform 失败拒绝）。
- 全部 13 findings 修复后 targeted gate 复测：见 TEST_MATRIX.md（Phase 8 双遍记录）。
