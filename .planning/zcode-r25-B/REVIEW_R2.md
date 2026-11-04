# R25-B REVIEW_R2（fresh 上下文独立评审，2026-10-28）

评审者：未参与实现、也未参与 R1 的 fresh 子代理；输入 = 全量 diff + 生产代码对照 + 仓库自有注册门。
结论：**APPROVE_WITH_FIXES**（R1 的 P0/P1 确认修复；新发现 1×P2 由 R1 修复自身引入 + 6×P2 覆盖缺口 + P3 族）。

## 处置（全部 P2 已修 + P3 除 R2-11 记录项外已修/已处理）

| id | 级别 | 问题 | 处置 |
|---|---|---|---|
| R2-1 | P2 | per-feed dtype 用了顶层 `model.input`（catalog 镜像 inputs[0]）→ 混合 dtype 多模态被误拒/后 feed 漏检 | **已修**：feed→contract 绑定后按绑定 contract 的 dtype 逐带校验（与引擎 per-feed 检查同语义）；新增两个测试（mixed-dtype Ready + 第二 feed 误配 Refused） |
| R2-2 | P2 | 无 temporal 帧元数/missing_timestep 门 | **已修**：`plan.feed_frame_arity`（多帧/缺帧 refuse/超 kMaxTemporalFrames）；测试双形态（refuse→Refused；zero→Ready+9 通道） |
| R2-3 | P2 | dynamic-T 用 contract.temporalLength 而非 provided count | **已修**：`dynamicT = collapse=="sequence" && temporalDynamic` 时 declaredFrames=paths.size()；`frames` 文档注释修正为"绑定期声明的帧轴" |
| R2-4 | P2 | 检测 lane `checkContract`（detectionDeclared/validate/resize=to_input+input 尺寸/pad-clamp）未镜像 | **已修**：`plan.detection_contract_missing` / `plan.detection_contract_invalid`；测试（无 output.detection→拒绝） |
| R2-5 | P2 | scene lane 契约未镜像（classes/单头/distance/场景像素与浮点界） | **已修**：`plan.scene_contract_invalid` + `plan.scene_bounds`（kMaxScenePixels/kMaxSceneFloats 同常量） |
| R2-6 | P2 | 单输入 lane 四处拒绝未镜像（inputs[0].preprocessDeclared、pad/clamp、morphology+Probability、calibration+feather） | **已修**：`plan.preprocess_override_unsupported`、`plan.multimodal_only_knob`、`plan.postprocess_contract_conflict` |
| R2-7 | P2 | 窗口预算（checkedWindowSide/Floats）未镜像 | **已修**：`plan.window_budget`，同算术（side=tile+2·halo+2·pad vs kMaxWindowPx；floats vs kMaxWindowFloats） |
| R2-8 | P3 | ensemble tilesPlanned 是单成员骨架，run 按成员求和 | **已修**：网格块按 ensembleMembers 乘算 tiles/batches；doc 文案改为"combined execution skeleton"与实际一致 |
| R2-9 | P3 | feed 绑定静默位置回退、重名未检 | **已修**：镜像引擎两条拒绝（重名/未匹配名→`plan.contract_unwired`） |
| R2-10 | P3 | roles/mean-std 仅查 feed0 与全局 | **已修**：per-feed（绑定 contract 的 roles 与 effective preprocess，M3 override 优先） |
| R2-11 | P3 | `fedChannels` 仅网格块赋值 | **已修**：无条件赋值 + JSON 恒真 |
| R2-12 | P3 | gate10 catch 把所有 RSOperatorError 映射到 head_contract_violation | **已修**：拆 `plan.output_mode_unsupported`（manifest output.format 词汇）与 head-contract 两码 |
| R2-13 | P3 | 文档漂移+过度声称 | **已修**：补 `import.directory_product_root`；plan.md 改为"the gates listed here"式诚实表述；补多输入保真段 |
| R2-14 | P3 | R1 import 修复无测试钉 | **已修**：qint64 精确性用例、SAFE 目录产品用例（不拒+directory_product_root+remediation）、completeness 真实契约断言 |
| R2-15 | P3 | mode-conflict 用例用显式 Labels，绕过 manifest 派生分支 | **已修**：该 SECTION 用默认 Probability 请求，钉死 `rasterOutputMode(model)` 派生 + plan.outputMode==Labels |
| R2-16 | P3 | netCDF 用例 else 分支把不执行变成 PASS | **已修**：`subdatasetCount<=0` 时整体 SKIP |
| R2-17 | P3 | 泄露 oracle 近空洞 | **已修**：正 oracle（typed error class 或 caller path）+ 负断言全保留 |
| R2-18 | P3 | adaptersAccepting 全量 12 适配器 accepts 的成本表述 | **已修**：第二个 specific 匹配即短路（一个匹配不可能是歧义）；注释说明成本边界 |
| R2-19 | P3 | Windows 盘符 VSI 拼写 | **已修**：`^[A-Za-z]:/vsi` 正则 + `:/vsi` 段匹配 |
| R2-20 | P3 | `<cstdint>` 传递包含 | **已修**：显式 include |
| R2-21 | P3 | 兄弟轨同 EOF 追加的文本冲突风险 | 记录：按 union 协议合并后跑注册门（已跑，exit 0） |

## 评审确认项（原文摘录）

- R1 七项 claim 全部 CONFIRMED（含 OpenCV 门、通道核算、EO lane 保真、引擎门覆盖、import 修复、ensemble/scene、v1 逐字节保持）。
- 调用链顺序、engine 静态规划器复用（无重新推导漂移）、合并范围（全部在 B 写区）、跨平台面（无废弃 Qt API、无裸路径拼接）、资源（无网络/锁/裸 FILE、GDAL RAII、8MiB 身份预算）全部干净。
- 测试判别力真实；provider 单例卫生良好；无 CWD 污染；determinism 判别器是真 oracle。

## 修复过程中的测试修正（如实记录）

- 两处 temporal 测试最初违反 master 现行契约（单输入模型+feeds 被 rejectUnwiredContracts 拒；temporalLength=3+单 path 在 missing_timestep=refuse 下引擎也拒）→ 按 run seam 合法形态修正（双输入、missing_timestep=zero）。这是测试对契约的修正，不是生产代码退让。
- SAFE fixture 初版缺 manifest.safe/measurements → completeness=Invalid → 被正确拒绝；补齐三个核心 constituent 后 PartialReadable（sparse fixture 无 band 信息），主断言（不拒+directions）通过。断言从 "complete" 放宽到 "complete|partial_readable" 以匹配真实契约语义（PartialReadable 不是拒绝）。
- Catch2 不允许 `CHECK(a || b)` 分解 → 拆局部 bool。

## 最终验证

`ctest -R "r25_b"` **21/21 通过 × 2 轮**（1 netCDF 用例按驱动能力 SKIP）。构建 exit 0（共享宿主 -j2、flock 单锁）。邻域回归与注册门见 TEST_RESULTS.md。
