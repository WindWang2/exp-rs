# R20-02 — 地理空间数据管理、模型推理运行时与持久化可靠性

**BASE**: 28e1891390bb812dc7ef2fe045ac90492c9525f1 (= origin/master @ 2026-10-23)
**HEAD**: 61b9b9af90（5 commits：RED 种子 → 六项修复 → 账本 → R2 修复）
**PR 类型**: 修复 + 回归测试（无新依赖、无新平台、CMakeLists 零改动）

## 工作范围（B05/B09/B11 深度审计 + 合并存续验证）

本轨域 126 条 open issue 逐一核验（swarm P1×12、P2×15、P3×10 均已在 master 修复，证据见 `.planning/zcode-r20-02/ISSUE_TRIAGE.md`，建议随本 PR 批量关闭），转入 Work Package 深度审计，本人实读源码核实出 6 项真实缺陷（B05 持久化 2 + B09 契约 1 + B11 推理 3），全部修复：

| ID | 缺陷 | 文件 |
|---|---|---|
| F1 | 边缘 tile（非整除栅格）混批使整次推理失败——随库 10 个分割 manifest（batch≥2 无 to_input）在任意非整除尺寸栅格上必败 | src/operators/runtime/tile_inference_engine.cpp |
| F1+R2-1 | probe 路径同类残留：尾部全 NoData 边缘 tile 撞未 flush 的有效前缀批 | 同上 |
| F2 | NoData 像素 uncertainty 输出 0.0（最大确信）而非 NaN，与类别带自相矛盾（两 lane） | 同上 |
| F3 | preprocess.interpolation/resize 非词表 token 静默退化（"bicubic"/"Nearest"→bilinear；#646 fail-closed 类） | src/operators/framework/model_catalog.cpp |
| F4 | 声明 output.uncertainty 但无 ≥2 通道头时静默发布缺带产品（两 lane，现 typed refusal 且零 staging） | tile_inference_engine.cpp |
| F5 | 快照治理库拷贝失败仍 report.ok=true（恢复侧静默丢失全部治理状态） | src/data/governance/workspace_lifecycle.cpp |
| F6 | 对象池 put 撞损坏对象永久拒绝该 digest（不自愈）+ rename 前无 fsync/后无目录 sync | src/data/artifact_object_pool.cpp |

## RED/GREEN 证据（本地实跑，全程全局锁 -j2）
- **RED（master@28e1891 pristine 引擎 + 新测试）**：6/6 全部命中——F1 `mixed tile sizes (36x64 vs 64x64)`；F2 `mismatchedConfidence==2048`（两象限 NoData 全部误报 0 置信）；F3 3 断言 `Ready != InvalidManifest`；F4 无异常抛出；F5 ok==true 且快照目录未清理；F6 `REQUIRE(second)` nullopt。四二进制 101,215 断言中仅本轨 13 条失败 → 无预存红。
- **R2-1 真 RED**（stash 修复保留测试）：`mixed tile sizes (36x64 vs 64x64)` 复现 probe 机制。
- **GREEN×2（终态 61b9b9af90）**：test_model_runtime **110,429 断言/25 用例**、test_model_catalog_v2 436/24、test_workspace_services 212/17、test_fault_injection 162/9 —— 两遍全绿。

## 回归（零回退）
- **38 个域族二进制 sweep 全 PASS**（chunk/dataset/experiment/governance/model/workspace/fault/gpu/pool 全域；TEST_RESULTS.md 列全名单）。
- 受 R2 修复影响的 11 个二进制复验全绿（model_runtime_8/9、model_tasks 12,816、failure_matrix、ensemble、library_manifests、store_fault_injection、rs_operator 832 等）。
- **合并存续验证**：R19-01 四项修复（zarr.json 扩展名/setRange 强制/4MiB 有界读/worker 路由）源码存续 + 对应测试族复跑全绿；#1545/#1553/#1554/#1580-#1586 运行时回归绿。

## 独立评审
- **R1**: REQUEST-CHANGES → 7 项闭环（含 P0 批处理不变量崩溃：评审者实跑 SIGABRT 复现；修复后该场景 110,429 断言全绿）。`.planning/zcode-r20-02/REVIEW_R1.md`
- **R2**: APPROVE（只读终审）→ P2+P3×3 全部本轮闭环（P2 为 stash 法真 RED→GREEN）。`.planning/zcode-r20-02/REVIEW_R2.md`

## 跨轨移交（CROSS_TRACK_FINDINGS.md，6 项）
#1597/#1595 CLI 残留（03/05 轨）、#1647-1 SicnuPythonApi 未注册（03 轨）、#1537 STAC 分页（04 轨）、#1544 建议 01 轨关 issue、本轨域 stale issue 批量关闭清单、锁纪律观测。

## Known Fail / Not Executed
- 无已知预存红回退；构建期 GCC16 ICE×4 与 ld 段错误×1 均为工具链瞬时故障（make 重试/重跑自愈），最终产物全量验证无恙。
- NOT_EXECUTED：GPU lane（无 GPU）、Windows/macOS lane、TSan（RISK_REGISTER.md 平台声明）。
- 跟进记录：A1/A4/A5/A6/B5/R2-4 六项已审计未修项（含理由与建议方向）。

## 明确声明：无需等待 CI/CD；不自动合并。
