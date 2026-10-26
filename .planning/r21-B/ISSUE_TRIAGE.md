# R21-B ISSUE_TRIAGE — 126 open issues 动态分诊（2026-10-24）

规则：open ≠ 未修。结论 = issue 正文 vs master@911e0b17 现码实读。

## FIXED-OPEN（源码级已核实修复，待本轨运行时复验后建议关闭）
- #1541 #1542 #1543 #1545 #1546 #1547 #1548 #1553 #1554 #1555 #1556（R20-02 核验，911e0b17 复认在位）
- #1575 #1577 #1578 #1579 #1580 #1581 #1582 #1583 #1584 #1585 #1586 #1587 #1603 #1604 #1605（同上）
- #1608（worker_daemon.py:619-651 _json_safe + 预编码 typed error）
- #1609（worker_daemon.py:469-483 unload 清理 callbacks/algo_executors）
- #1635 #1637(1-4) #1642 #1647(2)（R20-02 核验）

## OPEN-UNFIXED（本轨域，M2 目标）
| Issue/ID | 内容 | 现码 |
|---|---|---|
| A1(R20 风险) | canonical 空回退双 AssetId | ogr_vector_source_provider.cpp:38 等 |
| A4(R20 风险) | 探测失败当 fresh DB、版本戳覆写 | artifact_store.cpp:227 |
| A5(R20 风险) | bundle 导入孤儿 metric 行 | reproduction_bundle_import.cpp:378+ |
| A6(R20 风险) | createDataset TOCTOU 误码 | dataset_store.cpp:361+ |
| B5(R20 风险) | mask_threshold 阈值化所有 head | tile_inference_engine.cpp:1819+ |
| #1647-1 | SicnuPythonApi 未注册+假广告 | qgis_python.cpp:148-207 |

## 非本轨（不动，跨轨记录）
- #1535-#1540(app/agent_ops/processing)、#1551/#1552(sdk/workflow)、#1557/#1558(native/测试)
- #1559-#1574、#1588-#1594、#1596、#1598-#1615、#1616+（gui/app/help/faultlab/cli/verify 等）
- #1595/#1597 残余（src/cli）、#1537（src/app）、#1544（operators/rs 归 A）
- 大量 [P3] 技术债汇总 issue（#1625-#1658）：各域归各轨；本轨域 #1635/#1637/#1640/#1647 已含在上表处置
