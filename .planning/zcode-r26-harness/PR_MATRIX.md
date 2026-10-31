# PR MATRIX (2026-10-30T15:11Z)
| PR | 轨 | 与本轨关系 |
|---|---|---|
| #1724 | R25-D Qt/QGIS 制图 | 无文件交集 |
| #1725 | R25-E 教学 | 无文件交集 |
| #1726 | R25-B 数据/推理/复现 | 只读上游（import_preflight/inference_plan/replay_deviation）；本轨不消费其新 API，无 cross-PR contract 需登记 |
| #1727 | R25-C Pi/MCP | 直接防撞：其改 mcp_server/workflow_run_coordinator/pi 桥；本轨改动全在 harness/planner 内部与 tests 新文件；tests/CMakeLists.txt 双方仅尾部追加不冲突；surface_diff_snapshot.json 本轨不改 |
| #1728 | R25-A 科学算法 | 其可能新增算子→若其新增 rs: id 进 slotTable 而 familyMap 未更新，本轨 F 包漂移守卫测试会在联合 master 上抓到（好事，属可调和冲突，登记跟踪） |

## 同轮 R26 兄弟轨（2026-10-30T15:20Z 确认）
- r26-workbench（exp-rs-r26-workbench-20261030T151124Z）：src/app/** GUI；tests 尾块 R26-workbench。与本轨无文件交集。
- r26-science（exp-rs-r26-science-20261030T151203Z）：src/analysis、processing/algorithms、2 算子文件；tests 尾块 R26-science。与本轨无文件交集。
- 共享文件仅 tests/CMakeLists.txt：三轨各自尾部 append-only 块，可并行合并。
