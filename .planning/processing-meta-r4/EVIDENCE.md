# EVIDENCE — hardening/r4-processing-meta

所有命令在 worktree `/home/kevin/project/exp-rs-processing-meta-r4` 执行。
（本文件随验证进度回填；每个条目必须带命令 + 退出码 + 双跑记录。）

## E1. 快照 gate（WP-D）

- 生成器双跑字节稳定：`build-r4meta/tests/contract_inventory --source-root . --out /tmp/g1.json && --out /tmp/g2.json && cmp g1 g2`（图）；`--census-out` 同法。
- 字节新鲜度：`--check data/contracts/contract_graph.snap.json`、`--census-check data/contracts/determinism_census.snap.json`——各连续两轮（退出码 0）。
- 本地测试断言：`ctest -R test_snapshot_gate_r4`（双跑稳定 / 字节新鲜度 / 扰动归因三断言）。
- 再生成提交：sidecar/meta 变更后 `--out`/`--census-out` 再生成两快照并独立提交。
- 结果：待回填。

## E2. capability 页面（WP-E）

- `capability_knowledge_tool gen-pages . --check` → `gen-pages: zero diff`（补齐后先 gen-pages 写出、提交，再 --check 验证）。
- 结果：待回填。

## E3. 完备性门禁（WP-G）

- `ctest -R test_capability_completeness`：四键 census（红→绿箭头）+ D1 原契约，双跑。
- 结果：待回填。

## E4. 基线红绿分布（Phase 0 / 6）

- `ctest -R "capabilit|contract|meta|registry|snapshot" -j1`（构建完成后、meta 编辑前）：记录全量结果作为基线；本轨道新增测试的预期状态单列（completeness census 补齐前红为 TDD 设计态）。
- 结果：待回填。

## E5. 终局双跑（Oracle 1）

- 全新构建目录 + `ctest -R "capabilit|contract|meta|registry|snapshot" -j1` 连续两轮，日志存 `.planning/processing-meta-r4/logs/`。
- 结果：待回填。

## E6. help 完备性（WP-F）

- 静态对照表：`HELP_CONSISTENCY.md`（76/76，18 条 command.rs.* 语义对照零矛盾）。
- `ctest -R test_help_integrity`（合成校验零问题）。
- 结果：待回填。
