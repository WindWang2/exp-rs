# R24-B TASKS — 工作包与执行序

状态：`TODO / DOING / DONE / BLOCKED / NOT_EXECUTED`

| WP | 内容 | 状态 |
|---|---|---|
| WP-B0 | `DATA_INFERENCE_MAP.md` 真实链路图（侦察代理的目录/调用图已供料）；#1710/#1709/#1673 修复在位核验 | DONE（核验）→ 图待写（随首个实现 commit 落） |
| WP-BX | 构建基线：configure+目标测试 target 编译，回归邻域 ctest 跑通 | DOING |
| WP-DX | 第二波对抗审查：tile_inference_engine / detection_* / range_cache / dataset_store+audits / experiment_store+bundle+capsule / eo_preflight+model_execution_service+device_planner+model_publish —— 找 LIVE 缺陷 | DOING |
| WP-B1 | geospatial：GAP-G1 literal-'%' 正例回归 + 审查所得缺陷修复 | TODO |
| WP-B2 | remote COG/STAC：审查所得缺陷 + 本地假 server 负控（不连外网） | TODO |
| WP-B3/B4 | data/dataset：审查所得缺陷 + 10^4 级合成行分页/审计确定性 | TODO |
| WP-B5 | eo preflight/provider：审查所得缺陷 + typed fail-closed 负控 | TODO |
| WP-B6 | tile inference：metamorphic（tile size / 顺序 / provider）≥3 类 + 尾块/羽化/取消/OOM 阶梯 | TODO |
| WP-B7/B8 | **GAP-G2 主攻**：lineage run_edges 导出/导入重挂 + 原子发布故障注入（复用现有 atomic_fs/journal 能力） | TODO |
| WP-B9 | E2E×3 + 性能 5 次实测（RelWithDebInfo） | TODO |
| WP-B10 | 双独立 review R1/R2 + merge-integrity 门 + PR | TODO |

每回合纪律：单一改动 → RED → GREEN×2 → 记账。连续 3 失败换假设；8 失败重定义。
