# R15-C Oracles — 每工作包验收定义

格式：`Oracle → RED 证据 → Fix → GREEN 证据（×2）→ 残留风险`。
原则：只对最新 origin/master 真实复现且不与 open PR 重叠的问题修复；无 RED 证据不提交"推测性修复"。

| WP | Oracle（验收陈述） | 计划 RED 证据 | Fix 策略 | 状态 |
|---|---|---|---|---|
| C01 | 模型目录→manifest→provider→session→tile→publish→verify 路径图完整，每模型类型单一路径，无互相矛盾的发布路径 | 路径图审计（文档）+ 多入口同模型同结果 golden | 若发现第二套解析路径→收敛 | **CLEAN**（C14 审计：7 算子全收敛 runModelInference→acquireWithFallback，无第二套 session 创建路径） |
| C02 | 模糊 square tensor layout 在注册期明确拒绝或显式定义且推理一致（R14-B 遗留缺口） | `validateManifestJson` 对方头 square 歧义 manifest 返回空 issue list（RED）→ 修复后返回 typed 拒绝 | model_catalog.cpp 注册期交叉约束（detection 契约 × 固定输入几何） | **FIXED**（test_r15_C_manifest_contracts；RED→GREEN 待构建取证） |
| C03 | 模型内容改变→缓存失效；版本/权重不一致 fail closed | 审计 + 既有 test_model_runtime digest/session 用例扩展 | 视审计结论 | **CLEAN**（session cache key 含 contentDigest+framework；digest 缓存 (path,size,mtime)；aux files resolve 时强制） |
| C04 | CPU 不支持 vs 临时 GPU 资源不足区分明确，不悄悄错选设备 | 审计 device_planner + acquire 错误分类 | 视审计结论 | **FIXED**(F5/F6 env 覆盖) + **CLEAN**(分级：readiness→InvalidInputData vs acquire→DeviceUnavailable；F11 代码选择已修) |
| C05 | 同一有效 tensor 合同在可用后端一致；无依赖时只声明 compile-only | 协议对照表（文档）+ provider 单测 | 视审计结论 | **FIXED**（B2 原因注入、B3 能力预检、B4 分类归一、B7/B8/B9 provider 一致性） |
| C06 | 溢出/不支持 layout 获 typed error，无越界访问 | 既有 test_r14_B_runtime_providers TensorBlob 用例复跑 + 补边界 | 视审计结论 | **FIXED**(dedup 键加宽) + **CLEAN**(R14-B 项复核在位) |
| C07 | 无 tile seam、无邻块重复/遗漏，产品与块尺寸在容差内等价 | 既有 test_r14_B_tile_engine 复跑 + 不同 tile/overlap/halo 矩阵扩展 | 视审计结论 | **FIXED**（单输入 min_valid_coverage 门）+ **CLEAN**(R14-B halo/seam 复核) |
| C08 | 输出排序稳定、每实例落一个 owner tile、坏 bbox 不进结果 | 既有 test_r14_B_detection_decode 复跑 + golden 扩展 | 视审计结论 | **FIXED**(dedup 键) + 覆盖补齐（test_r15_C_detection_decode；WBF 零测试仍为记录缺口） |
| C09 | 输出类型不裁剪 class ID；sidecar 统计与实际 raster 像元一致 | 审计 + labels/tally 用例扩展 | 视审计结论 | **FIXED**（vote agreement NoData 哨兵）+ **CLEAN**(tally/argmax/calibration/remap 逐项复核) |
| C10 | terminal status 只发布一次；取消/超时/worker 异常无孤儿资产 | 故障注入测试（http provider 卡死/取消中 publish） | 视审计结论 | **CLEAN**（逐路径复核；B1 HTTP dispatcher 限制记录为未决） |
| C11 | 能安全减 tile 或拒绝；释放临时显存；不复用错误 session | OOM 阶梯实测/审计 | 视审计结论 | **FIXED**（F1-chunk 梯级、F2/F3/F4 ledger、F5/F6 env、F11 代码、F12 文档） |
| C12 | 慢消费者下峰值 RSS 受预算约束；取消及时终止 | chunk pipeline 审计 + bounded queue 用例 | 视审计结论 | **FIXED**（F1-chunk、F7 join 进度、F9 一次性守卫、F8 文档）+ **CLEAN**(queue/cancel/checkpoint/scratch 逐项复核) |
| C13 | 下游不读半成品；模型证据可离线再验证 | 既有 publish 故障注入套件复跑 + crash 阶梯审计 | 视审计结论 | **FIXED**（F1/F2 后缀完备收养）+ **CLEAN**(provenance_verify/其余发布路径) |
| C14 | 同一模型/数据/设置经 CLI/SDK/Agent/UI 指向同一 runtime 权威，结果可复现 | 入口对照审计（文档）+ 若发现第二套默认值→golden | 视审计结论 | **FIXED**(B5 覆盖范围) + **CLEAN**(权威路径/参数漂移机械强制)；B6 commonEstimate 属他轨所有权，已记录 |
| C15 | 报告 p50/p95、吞吐、资源开销；有证据才优化 | test_model_runtime_bench 实测（warmup/缓存/线程固定） | 先测量后动手 | 待构建（见 PERFORMANCE.md） |
| C16 | 无可用后端时给明确可恢复路径；不假装通过不具备的硬件测试 | 诊断路径审计 + CPU-only 实测 | 视审计结论 | **FIXED**（B2 可恢复文案、B10 测试诚实性）；B11 env-doctor 属 src/cli 他轨，已记录 |
| C17 | 修复项 RED→GREEN×2；reviewer 复检无开放 P0/P1 | 两轮独立 review（reviewer 不写本轨生产代码） | 修复全部 P0/P1 + 合理 P2 | 待 RED/GREEN 取证后启动 |

## 失败/阻塞记录规则

- 连续三轮无进展→换假设；八轮无进展→回查前提/缩小工作包；保留诚实阻塞记录于 LEDGER.md。
- 硬件缺失（CUDA/TRT/OpenVINO）→ 标注 BLOCKED_WITH_EVIDENCE + NOT VERIFIED，不伪造 GREEN。
