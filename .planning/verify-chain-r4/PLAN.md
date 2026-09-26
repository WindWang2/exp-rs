# PLAN — Track 16 验证链收口执行计划

基线见 BASELINE.md。每轮：读账本 → 单一改动 → 亲自验证（关键双跑）→ 记账 → 判定。
构建纪律：`ninja -j2`（RSS>70% 降 -j1）、`ctest -j1`、改动 TU 删 .o 真重建（假绿陷阱）。

## 执行序（TDD，一接缝一红测一提交）

### Stage 0（构建落地后立即）
- S0.1 基线 ctest（宽口径 `verif|grader|preflight|suitab|science_context|evidence` -j1）→ 实测红集 → 域内红测 = 修复候选，域外红测记录。
- S0.2 提交 planning 工件基线（BASELINE/PLAN/DECISIONS 骨架/账本首行）。

### Stage 1 = WP-B 前半（digest locale 稳定性）
- R1/R2/R3 三个红格（preflight request_digest / finding canonical / report digest）：
  - 红：双 locale harness（C vs LC_ALL=de_DE.UTF-8 模拟；若宿主无 de_DE 则用 setlocale 注入可用 locale，测试自建 locale 依赖探测）驱动含小数 operatorParams/evidence → 断言字节一致（预期红）。
  - 绿：产出点入口单点钉 ClassicNumericLocale（复用 verify_locale.h 模式；preflight 引入等价 per-thread pin —— 复制该模式到 preflight/ 或从 verify 引用头？preflight 是 Qt-free 叶子，verify 也是 Qt-free；跨叶子引用需审查 CMake 依赖方向，倾向 preflight 内自带最小 pin 类型，与 #1318 "sibling leaf libraries made" 先例一致）。
  - 矩阵：≥18 行 = 产出点(P1-P12) × {C, 非C} × 含小数输入；数据驱动 + 独立复跑脚本 `tools/digest_locale_matrix.py`（或 CTest 脚本）。
- grader to_chars 侧：矩阵行验证（绿，证明设计成立）。

### Stage 2 = WP-D 失效联动（≥20 用例）
- 文件：`tests/test_preflight_authority_invalidation.cpp`（新，避开 #1337 的 test_preflight.cpp）。
- 矩阵：4 粒度 × 规则族消费（facts/capability）+ budget 边界；断言拒绝/放行二元 + 不回退陈旧事实 + cacheHit/revision 证据。
- 桥接：StateAssetFactsProvider(broker.assets resolver 形状) × CapabilityMirrorProjection / MemoryCapabilityProvider。
- 先例审查：test_science_context_live_authorities.cpp、test_preflight_engine.cpp。

### Stage 3 = WP-A adapter 覆盖矩阵（5 adapter × 10 检查族）
- 文件：`tests/test_verify_adapters_matrix.cpp`（新）。
- 全交叉表机械枚举：kCheckKinds × 5 adapter；每格"通过性"用真 fixture（sidecar exp-rs-prov/1 真件 / GDAL 内存栅格 / checkpoint 真件 / 真文件）；负例三态（Missing/Unreadable/ForeignEnvelope）逐 adapter。
- 引擎↔adapter 集成格：production adapter 组装 VerificationContext 跑 evaluateCheck 的端到端格。
- ADAPTER_MATRIX.md 归档；缺口一格一提交。

### Stage 4 = WP-C 两级 outcome 边界（≥6）+ WP-E grader 对抗（≥6）+ WP-F suitability 三态（≥6）
- WP-C：扩 test_verifier_engine.cpp / test_verifier_adversarial.cpp（先读现状避免重复）。
- WP-E：扩 test_grader_engine.cpp；超时/证据缺失/部分提交 → typed 拒绝。
- WP-F：扩 test_suitability_adversarial.cpp / test_suitability_core.cpp；部分可用/冲突/过期事实。

### Stage 5 = WP-G 收口
- API_AUDIT.md 72 行；DECISIONS.md ≥5 条；ctest 宽口径全量双跑（新构建目录复核用 `build2`）；EVIDENCE.md。

### Stage 6 = Review + PR
- 1 个只读 subagent 对抗审查全 diff → P0/P1 修复 → REVIEW_LOG.md → 提交推送开 PR（不轮询 CI）。

## 风险与预案
- qgis_core 全量编译 -j2 长时间：定向 target 构建 + 后台等待期间做读码/审计表。
- 无 de_DE locale 宿主：测试内用 `setlocale(LC_NUMERIC, "de_DE.UTF-8")` 探测，不可用则跳过并记录（但宿主实测通常有 C.utf8/de；备选 fr_FR 或自建 locale archive 路径不可行则用环境层 LC_ALL 注入 + 双进程跑法）。
- 既有红测：以 S0.1 实测为准，域内收口、域外登记。
