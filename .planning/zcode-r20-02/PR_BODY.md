# R20-02 — 地理空间数据管理、模型推理运行时与持久化可靠性（第二轮）

**BASE**: 28e1891390bb812dc7ef2fe045ac90492c9525f1 (= origin/master @ 2026-10-23)
**HEAD**: <回填>
**PR 类型**: 修复 + 回归测试（无新依赖、无新平台、零共享文件重构）

## 工作范围（B05/B09/B11 + 合并存续验证）

### 修复 6 项（全部 master RED → HEAD GREEN×2，证据见 .planning/zcode-r20-02/TEST_RESULTS.md）
| ID | 缺陷 | 文件 |
|---|---|---|
| F1 | 非整除栅格上边缘 tile 混批使整次推理失败（10 个随库 manifest 受影响）| src/operators/runtime/tile_inference_engine.cpp |
| F2 | NoData 像素 uncertainty 输出 0（最大确信）而非 NaN | 同上 |
| F3 | preprocess.interpolation/resize 非词表 token 静默退化（#646 类 fail-closed）| src/operators/framework/model_catalog.cpp |
| F4 | 声明 output.uncertainty 但无 ≥2 通道头时静默丢带 | tile_inference_engine.cpp |
| F5 | 快照治理库拷贝失败仍报 ok=true（恢复侧静默丢治理状态）| src/data/governance/workspace_lifecycle.cpp |
| F6 | 对象池 put 撞损坏对象永久拒绝该 digest（不自愈）+ rename 前无 fsync | src/data/artifact_object_pool.cpp |

### 合并存续验证（R19-01 四项修复 + swarm 已修 issue 抽查）
- zarr.json 两段扩展名 / setRange 运行时强制 / 4MiB 有界读 / worker 路由设门：源码存续确认 + 相关测试族复跑全绿。
- 本轨域 open issue（P1×12、P2×15、P3×10）逐一静态核验全部已修（ISSUE_TRIAGE.md），建议随本 PR 关闭清单见 FINAL_REPORT。

## RED/GREEN 证据
<回填：命令 + 退出码>

## 回归
<回填：域 sweep 结果>

## 跨轨风险
- F1/F2/F4 改动 tile_inference_engine 推理路径，rs_inference_operator（01 轨域）经同一引擎，回归含 test_rs_operator 全量（831 断言级）。
- F3 词表收紧：随库 27 个 manifest 全部在词表内（test_model_library_manifests 钉住），第三方自定义 manifest 若用了非法 token 将从静默 bilinear 变为显式拒绝——这是有意的 fail-closed 修正。

## Known Fail / Not Executed
- 无已知预存红回退。
- NOT_EXECUTED：GPU lane（无 GPU）、Windows/macOS lane、TSan。
- RISK_REGISTER.md 列 9 项已审计未修项（含 4 项降级理由与跨轨移交）。

## 独立评审
- R1: <回填>
- R2: <回填>

## 无需等待 CI；不自动合并。
