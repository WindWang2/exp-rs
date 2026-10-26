# TASKS — R23-E Work Packages（终态）

| WP | 终态 | 证据锚点 |
|---|---|---|
| WP1 plugin host 类型门 | **DONE**（c9c05614） | TEST_RESULTS T6：84+34 断言绿 ×3；突变 RED ×2 |
| WP2 python worker 兼容层 | **DONE**（b39942cd + cf3bfd04 N3） | T2：5 用例 ×3 绿；突变 RED ×3（R1/R2 独立复现）；temp 零泄漏 |
| WP3 merge-integrity 门 | **DONE**（11497c6c + b22f7ddd + cf3bfd04） | T1/T9：10+ 场景；R1 F1-F8、R2 N1-N5 全闭环 |
| WP4 安装/交付冒烟 | **DONE**（脚本级 + 完整安装树） | T3（Windows 真机 5+1 腿 + canary）、T8（12/12：worker/.qm/relocate/RPATH/ldd/bundle guard） |
| WP5 native terminal | **DONE**（b41f9952 + b22f7ddd F4） | T7：3 用例绿（选择/cwd/零参数） |
| WP6 #1699 残余 | **DONE（跨轨记录）** | CROSS_TRACK_FINDINGS #1：9 处逐条方案 |
| WP7 资源/脚本/根目录 | **DONE（验证+盘点）** | TEST_RESULTS T4：JOBS_CAP 绿、-j 零违规、YAML 解析；CROSS_TRACK #4 盘点 |
| WP8 升级/卸载 canary | **DEFERRED** | FINAL_REPORT：relocate/覆盖写已覆盖；完整卸载矩阵超时间预算且与 #1708 冒烟重叠 |


## 不做（并说明）

- 不重新实现任何 FIXED_OPEN 项（ISSUE_TRIAGE.md 列表）。
- 不改 B 轨模型推理 Python provider、不新增 Python-only 平台核心。
- 不动 CI YAML 除非真缺口；永不等待线上 CI。
- 不批量重写其他轨生产测试（E12 规则）。

## 依赖

- WP1/WP2/WP4 依赖基线构建完成（build-E）。
- WP3 可先行（纯脚本 + git 数据）。
- WP5–WP8 与构建并行做静态/脚本侧工作。
