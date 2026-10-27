# r23-A TASKS(终态)

状态机: PENDING → RED → FIX → GREEN1 → GREEN2 → REVIEWED

| ID | WP | 任务 | 来源 | 终态 | 状态 |
|---|---|---|---|---|---|
| T1 | A02 | bothNormalizable 去魔数(#1687-1)+ validCount 文档(#1687-2) | [I] | RED(3 断言)→GREEN×2→R1/R2 复核 | REVIEWED |
| T2 | A07 | viewshed 亚米环断链(F1,P1) | [A3][M] | RED(75/441)→GREEN×2→R2 18 组对抗格网复核 | REVIEWED |
| T3 | A07 | 曲率混合导数模板(F2,P2) | [A3][M] | RED(2.0≠1.0)→GREEN×2→R1 独立反例 z=x²y | REVIEWED |
| T4 | A05 | ROI 波长单位(H1,P2) | [A2][M] | RED(0.49≠490)→GREEN×2 | REVIEWED |
| T5 | A02 | 流式 stretch ±Inf(F3,P3) | [A3][M] | RED→GREEN×2 | REVIEWED |
| T6 | A07 | 极点太阳方位(F4,P3) | [A3][M] | RED(171.19/8.81 噪声)→GREEN×2→R2 带缘 0.28° 实测 | REVIEWED |
| T7 | A03 | harmonicFit clamp(T1a,P3) | [A1][M] | RED(SIGABRT)→GREEN×2 | REVIEWED |
| T8 | A05 | MNF 空选择(H3,P3) | [A2][M] | RED(0.1174≠0)→GREEN×2 | REVIEWED |
| T9 | A05 | VCA 尺度无关门(H4,P3) | [A2][M] | RED→GREEN×2→R2 退化用例复核 | REVIEWED |
| T10 | A05 | spectral_table 宽度拒绝(H5,P3) | [A2][M] | RED→GREEN×2+R1-P3-3 qWarning | REVIEWED |
| T11 | A05 | spectral_anomaly 契约文档(H2,P3) | [A2][M] | 纯文档,R1 对照实现核验 | REVIEWED |
| T-dNBR | A02 | dNBR legacy 哨兵缝(r21-A M3 冲突解决) | [M][R1] | 邻域回归发现→修复→R1-P2-1 端到端钉+变异验证 | REVIEWED |
| T12 | A01 | 审计 CLEAN 面登记 | [A1][A2][A3][M] | FINAL_REPORT 附审计范围 | REVIEWED |
| T13 | A10 | 测试两轮 GREEN+13 邻域族回归 | — | 10,972+888,684+130,277+… 全绿 | REVIEWED |
| T14 | 交付 | R1(APPROVE,1P2+6P3 闭环)/R2(APPROVE,1P2 披露+P3 闭环)/push/PR/FINAL_REPORT | — | R1/R2 完成;push/PR 见 FINAL_REPORT | REVIEWED |

## 不修(登记)
- [A1] seasonal breaks CI 统计量 vs 点估计(R12 登记于 RISK_REGISTER)
- [A1] harmonicFit robust rmse/r2 加权语义未钉(弱,header 未承诺)
- [A2] VCA GS 门绝对阈值(R1-P3-2/RISK:较 master 纯放宽)
- [A2] Dictionary::etUnit 死标签(非缺陷)
- R2-P2-1 未声明哨兵披露项(R13):PR body 明示
