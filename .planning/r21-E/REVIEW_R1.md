# R21-E REVIEW_R1 — 独立评审记录（fresh subagent #1，2026-10-24）

评审人：R1a（W1）+ R1b（W2），独立上下文、只读、56+27 次工具调用。分拆双审因单票据运行时限。

## R1a（W1 verifier 接线）结论：REQUEST-CHANGES
| 发现 | 严重度 | 状态 |
|---|---|---|
| P0 "composer 未编译/attach 缺失" | P0 | **假象**：评审发生在 W1 代码 stash 期间（BASE 盘问）。非缺陷。 |
| P1 测试未传 rulesDir（SICNU_SOURCE_DIR 是 agent 私有定义，测试环境解析不到 → "Unknown lab" usage 误判） | P1 | ✅ 已修（tests/test_r21_e_verifier_evidence.cpp 传 `options.rulesDir = CMAKE_SOURCE_DIR "/data/labs/grading"`，仓库惯例） |
| P2 `artifactKind "file"` 不在 kArtifactKinds | P2 | ✅ 实现已处理（verifierKindHint 对 file 返回空 → 不建 type 检查） |
| P2 pins.crs 需 "EPSG:<code>" 形式 | P2 | ✅ 实现已做映射（"EPSG:" + code） |
| P3 kernel-walk artifact 拒绝路径漏挂 | P3 | ✅ 已补（第 6 挂载点，`!kernelUsage` 条件） |
| P3 envelope 带 error 键不在 engine reportFields | P3 | ✅ 已统一形状（见 R2 P2-4） |

## R1b（W2 faultlab）结论：Approve with changes
| 发现 | 严重度 | 状态 |
|---|---|---|
| P1 runner `declared` 谓词与 loader 不对称（非对象 verifier 被当未声明 → 可 passed） | P1 | ✅ 已修（`!isNull() && !(isObject && empty)`；非对象→verifier_spec_invalid fail-closed） |
| P2 vacuous 声明：rules_ref 无 must_fail → 空洞通过（#1464 形状下沉） | P2 | ✅ 已修（parseFaultVerifierSpec：rules_ref 要求非空 must_fail；首版误写在 isMember 块内，自证时发现并移到块外） |
| P2 studio_live 传递性获得 GUI/Network，其自身 guard 只查直接链接 | P2 | ✅ 已诚实化（CMakeLists 记录权衡+决策理由；CT 记录） |
| P2 FaultVerifierSpec::declared() 死成员且与 runner 谓词矛盾 | P2 | ✅ 已删 |
| P3 早退路径隐藏声明 | P3 | ✅ 已修（report init 即设 verifierDeclared；测试覆盖） |
| P3 fixture 几何注释错误（4x3 → 16x16） | P3 | ✅ 已修 |
| P3 band scale/offset 物化时丢弃 | P3 | ✅ 已修（逆变换+元数据+退化值 fail-closed） |
| 验证为稳健：RasterWriter 用法/CRS 异常捕获/规则解析/正反测试将通过/向后兼容（未声明场景报告 byte 不变）/无链接环/faultlab 叶库保持 jsoncpp-only | — | 记录备查 |

## R1 后自证发现（非评审项）
- 构建系统陈旧对象掩盖编译错误（helpers 落入 `lab_grading` 命名空间、`LabGradeResult` 嵌套作用域未限定、verifier/adapter 目标缺 PIC 链接入 SHARED）——全部修复并复测。
- batch_stress 首版记账口径错误（unavailable 结果未设 verdict）——按 runner 真实契约修正。
