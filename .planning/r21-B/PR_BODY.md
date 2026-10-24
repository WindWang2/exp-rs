# R21-B: geospatial data / model runtime / reproducibility — risk-register closure + e2e chain

**Base**: 911e0b17a3533ca0d79c1b68b000d6052c4c3c61 (origin/master at track start)
**Head**: de2dbd121b
**Track**: R21-B（地理空间数据 / 模型推理运行时 / 实验可复现）；worktree `exp-rs-r21-B-20261023T185101Z`

## 读过的 PR / issues
- PR #1668–#1672（R20 五轨均已合并；#1669 同域成果在 911e0b17 复验无 merge-loss，源码级+测试两遍）
- R20-02 RISK_REGISTER 遗留 A1/A4/A5/A6/B5/R2-4 逐项现场再核实（BASELINE.md）
- 126 open issues 全量分诊（ISSUE_TRIAGE.md）：本轨域 swarm 层无新增未修项；#1608/#1609 复验已修
- #1647-1（SicnuPythonApi 死代码 + 假广告）本轨修复

## 交付（每项 RED→GREEN×2，证据见 TEST_RESULTS.md / RED_GREEN.md）
| ID | 修复 | 类型 |
|---|---|---|
| A1 | 缺失文件身份锚定规范化父目录——symlink/相对拼法不再为同一物理文件产生第二 AssetId | fix(data) |
| A4 | schema 版本戳 write-once + ArtifactStore probe 失败 fail-closed + open 如实报告库内版本 | fix(data) |
| A5 | bundle 导入 run 安装失败补偿删除孤儿 metric 行（新增 deleteMetricRecord） | fix(experiment) |
| A6 | createDataset/createDraftVersion 并发 PK 冲突映射 dataset.conflict（80 轮双连接对抗） | fix(dataset) |
| B5 | mask_threshold 按 head role 按名门控（embedding/uncertainty/detection/未声明头不再被二值化） | fix(runtime) |
| #1647-1 | sicnu_project_path 真实注册（SicnuPythonApi 落地，删除假广告） | fix(python) |
| R2-4 | feather-blend NoData 恢复 + 多输入 every-feed-invalid 直测 | test(runtime) |
| M5 | 导入→数据集→推理→run/lineage→bundle 重放端到端 fixture + 零半成品失败腿 | test(r21-B) |

## 回归与基准
- R20 #1669 F1–F6 测试族两遍绿：model_runtime 110,440 断言/26 cases、model_catalog_v2 436/24、workspace_services 212/17、fault_injection 162/9
- 19 域族 sweep PASS=19/19（chunk_graph、gpu_plane、governance_store、workspace_catalog 804 断言、model_runtime_8 2404 断言等）
- 性能同条件实测（PERF_BASELINE.md）：并发 oracle 0.28–0.30s×3；model_runtime 全族 1.9–2.1s×2；修复不涉热循环

## 评审闭环
- R1（fresh 只读 reviewer）：10 项发现全闭环——P0：RED 阶段 src 回退被索引副作用提交（显式恢复提交 100b1975b6）；P1：A5 oracle 重写为 trigger 注入（pristine 实证 RED）；P2：role 按名查、并发 stamp INSERT OR IGNORE
- R2（fresh 只读 reviewer）：5 项 P0/P1/P2 闭环——P0：测试文件编译修正补提交；P1：多输入 lane headNames[h] 越界读改 resolvedHeadNames[h]；P2：死变量删除、WorkspaceCatalog stamp 竞态同步修、A5 不可达死代码删除。P3 记录（既有 derived-lane 契约缺口、A5 崩溃窗口收窄未闭合、A1 后旧拼法查询行为变化）

## 未执行项（诚实声明）
- GPU lane / TSan / Windows / macOS：无环境，NOT_EXECUTED
- A5 崩溃窗口（metrics 与 run 安装之间进程崩溃）：收窄未闭合（需跨两表单事务才可闭合）
- B5 derived-output lane（output.format=labels/mask）仍无 role 门控：既有契约缺口，未扩大本轮范围
- 环境断裂：vendored gdal 符号供给方（libarmadillo.so.15）从 prefix 消失 + Qt6Core 需 PCRE2_10.47；本轨用 r19-02 link-shim + --allow-shlib-undefined + 系统共享 zlib/zstd/expat/sqlite3 配方解（跨轨同样受影响）

## 文件白名单
`git diff 911e0b17...HEAD --name-only`（28 文件）100% 位于 src/{data,dataset,experiment,operators/runtime,python}、tests/、.planning/r21-B/（markdown）、.gitignore（append-only）、tests/CMakeLists.txt（append-only）

## Issue 关联建议（已修未关，可核验）
#1541 #1542 #1543 #1545 #1546 #1547 #1548 #1553 #1554 #1555 #1556 #1575 #1577 #1578 #1579 #1580 #1581 #1582 #1583 #1584 #1585 #1586 #1587 #1603 #1604 #1605 #1608 #1609 #1635 #1637 #1642 #1647
