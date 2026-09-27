# PLAN — Track 15 Model Runtime & Python Integration Hardening R4

基线见 BASELINE.md；接缝见 RUNTIME_FILE_MAP.md。所有 WP 遵循 TDD：公共接缝 → 失败测试（RED）→ 最小实现（GREEN）→ `-j2` 双跑 → 原子提交。

## WP 分工与接缝（实测后落位）

| WP | 接缝（实测） | 交付下限 |
|---|---|---|
| WP-A P1-8 | `mc.cpp` validateManifestJson/parseManifest/verifyArtifactLocked 统一校验出口（路径+字段+期望/实际四元组；身份先于内容）；python_worker_provider acquire 期错误的 manifest 语境 | ≥12 拒绝类×四元组 + 顺序回归 ≥3 |
| WP-B 边界 | worker=worker_lease/worker_protocol；gpu=ModelSessionPool；chunk=tile_run_contract/chunk_graph/scratch_registry/resumable_tile_run；exec=ExecutionGovernor | ≥18 用例（每子域≥3）+ 压力套 ≥1 |
| WP-C OOM 阶梯 | src/runtime 池阶梯（真实降档痕迹）+ operators 引擎 batch→tile 阶梯 | ≥3 真实层级（模拟层标注+原因） |
| WP-D crash-orphan | #1314 adoption 族边界（半写孤儿、竞争 adopt、provenance 完整、幂等） | ≥4 用例 |
| WP-E provider 矩阵 | SICNU_ENABLE_TENSORRT/OPENVINO 开关矩阵；默认双 off 守护 | ≥3 格 + 默认构建硬守护 |
| WP-F Python 通道 | python_worker_process_pool/ipc_server/worker_daemon.py 生命周期边界 | ≥4 用例 |
| WP-G 收口 | tests/CMakeLists.txt 注册 + SUBDOMAIN_BOUNDARIES.md 总表 + 双跑全绿 | 18 行可追溯 |

## 轮次推进顺序

1. Phase 0 尾：构建完成 → 基线 ctest 红绿分布 → 账本建档。
2. WP-A：先 RED（构造坏 manifest 记录现状输出缺口）→ 统一出口最小修复 → 12 类补满 → 顺序回归。
3. WP-B：worker（lease 边界）→ gpu（池边界）→ chunk（分区算术/graph 防御）→ exec（governor 边界）→ 压力套。
4. WP-C → WP-D → WP-E → WP-F 依次 TDD。
5. WP-G：总表 + 注册 + 全量双跑。
6. Phase 5 独立 review（1 个只读 subagent）→ P0/P1 修复 → Phase 6 归档 + PR。

## 提交规划（≥16 原子提交）

WP-A ≈ 5（RED 记录/统一出口/四元组补齐/顺序回归/文档）；WP-B ≈ 6（每子域 1 + 压力套 1-2）；WP-C ≈ 2；WP-D ≈ 1-2；WP-E ≈ 1-2；WP-F ≈ 2；WP-G ≈ 1；工件 ≈ 1-2。
