# R24-A Oracles（客观验收）

- **O-A0** ALGORITHM_INVENTORY.md 覆盖本轨全部算法家族（注册 id + 入口 file:line + 现有测试引用）；FIXED_IN_MASTER 判定重复实施 = 0（ISSUE_TRIAGE 表即证据）。
- **O-A1** #1687：RED v1 在未修代码上运行时失败（日志含失败断言与退出码）；修复后同命令 GREEN 两轮（退出码 0）；负控（-9999 未声明网、NaN 传播、参数守卫、a==-b 分母零）全过；NoData/NaN 语义在测试内显式声明。
- **O-A2** ≥3 个不同家族真产线路径被测试触达：math_utils 指数族（NDVI/dNBR 共享核）✓、时序族（temporal index series 链，视审计结论）、地形/DEM 或光谱族（E2E）。README 宣称不算数，以测试二进制实跑为准。
- **O-A3** metamorphic/tile 一致性 ≥3 组合适用处执行：哨兵感知重载在 (声明哨兵=−99 / 未声明 −9999 / NaN) 组合下语义稳定；E2E 两轮独立运行产物可重开且逐字节/容差一致；失败路径无半成品文件。
- **O-A4** 性能：每项优化 5 次 before/after 中位吞吐 + 峰值 RSS；无统计支撑不声称加速；稳定 >5% 退化回退。无可证热点 → PERF_BASELINE=N/A + 结构性说明（沿 r22-science 先例）。
- **O-A5** tests/CMakeLists.txt：R24-A 块 append-only、无冲突标记、注册并集完整（脚本 --require-refs 退出码 0）；R1/R2 两轮 fresh reviewer 闭环；最终非 draft PR 真实 URL 回填 FINAL_REPORT.md。
