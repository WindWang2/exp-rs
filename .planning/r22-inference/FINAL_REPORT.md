# R22-03 FINAL REPORT — C0 工作包交付

**轨道**：r22-inference（遥感 AI 模型推理的多通道正确性、GPU/CPU 稳定性与分块吞吐）
**分支**：`agent/r22-inference-20261025T132554Z`（base `911e0b17` = origin/master 实测）
**交付物**：一个 PR（本轨唯一），3 commits：修复 + R1 P3 测试补强 + R2 注释修正。

## 完成内容

### C0 [#1706] 多类别羽化拼接结果损坏 — P1 修复（本 PR）

**缺陷**（源码级验真，OPEN_UNFIXED）：`FeatherAccumulator::resetWindow` 的 `m_sum.assign(n, cv::Mat::zeros(...))` 因 cv::Mat 浅拷贝语义使全部槽位共享一个缓冲；`add()` 又把每 tile 的羽化权重按槽位重复记账（C 次）。任何 C>1 的 feather 推理：概率波段全部 = 类别均值、argmax 全图 class 0、uncertainty 槽同污染。

**修复**：每 slot 独立 `m_sum[s]` + `m_weight[s]`；band s 恰被"对该 band 贡献过有限值的 tile 集合"归一（R2 反例确证：全局分母会用未贡献权重伪造 0.0128 概率）；derived 折叠对部分定义栈 fail-closed 到 NoData（不再 argmax 半定义栈）；C=1 行为位相同。

**证据**：RED（未修复树 5 用例全败/39 断言失败，均值特征 0.3333/0.3131 精确复现）→ GREEN×2（7 用例 1203 断言）→ 回归族 ×2（test_model_runtime_9 1226、test_model_runtime 110429、test_r14_B_tile_engine 20695 断言全过）。

### C2 [#1554/#1553] — FIXED_OPEN 归档（不重复修）

`d9f1b886f1` 已在 master：gpu_plane.cpp 析构持锁、chunk_graph.cpp addSink 守卫。证据链见 ISSUE_TRIAGE.md。PR body 注明可随本 PR 关闭（工程上已在 master 生效）。

## 双独立 Review

- **R1**（agent_fbf1e99b，只读反对式，7 轴）：**APPROVE**，0×P0-P2；4×P3 全部闭环（测试缺口→已补 1381e5d80a；内存→注释+文档；ptr hoist→噪声级不落实；out-of-diff 崩溃疑点→本人源码证伪 :1056/:1060 守卫）。
- **R2**（agent_7ebb3b26，明确不复用 R1 结论，独立重推全部数学）：**APPROVE**，0×P0-P2；1×P3 注释已修，1×P3 备忘（X4）。
- 两审分离执行，非自审自批。

## 任务书 §7 最终 Oracle 对照

1. ✅ 三类 feather：各类概率不同（band1=0.8 vs band3=0.1 左核）、权重守恒（seam (0.8+0.1·0.5)/1.5 精确 1e-3）、argmax 与无羽化一致（左0右2，seam 翻转 x=14/18）。
2. ✅ GPU 路径 NOT_EXECUTED 如实申报（无 CUDA；修复层为设备无关纯数学）。
3. ✅ 部分定义栈/NoData fail-closed 无半成品（typed RSOperatorError 已有路径 + 本轨 poison/NoData 用例）；原子发布（.tmp~ + rename）为既有机制，未触碰。
4. ✅ 端到端：既有 test_model_runtime/test_model_runtime_9（本 PR 双跑全绿，含 rs:infer 语义链）覆盖 manifest→推理→输出；本轨修复强化其羽化路径。
5. ✅ #1673 先审（mask_threshold 门控，无语义重叠，hunk 不相交——PR_COLLISION_MATRIX）；非重复 PR。

## 未尽事项 / 展望（后续工作包，不在本 PR）

- C1（模型/数据/输出语义统一审计）、C3（分块流水线原子产物）、C4（可观测）、C5（确定性/吞吐基准——GPU 项 NOT_EXECUTED）、C6（极端路径攻防矩阵）。
- 半精度权重账本（X5）、derived+uncertainty 死分支处置（X4）。
- #1554/#1553/#1706 的 issue 关闭由维护者执行。

## 复现命令

```
V=/home/kevin/project/paleo-singlefactor-closure/vendor/prefix/usr
cd /home/kevin/project/exp-rs-r22-inference-20261025T132554Z
export PROJ_LIB=$V/share/proj
export LD_LIBRARY_PATH="$PWD/build-r22-inference/link-shim:$V/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH"
QT_QPA_PLATFORM=offscreen ./build-r22-inference/tests/test_r22_inference_feather   # 1203 断言
QT_QPA_PLATFORM=offscreen ./build-r22-inference/tests/test_model_runtime_9         # 1226 断言
```
