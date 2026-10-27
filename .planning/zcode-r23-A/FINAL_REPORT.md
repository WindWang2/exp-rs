# r23-A FINAL_REPORT — 遥感科学算法、科学算子与数值性能闭环

- BASE: origin/master `ad98a7a7041b9d6952f5d5fa4ac5cc46ac181dea`(2026-10-26T16:41Z fetch 复核)
- HEAD: `50a66a31e01bb263128a806e5fa86bb9ceceacdd`(分支 `agent/r23-a-science-algorithms-20261026T164112Z`,17 commits)
- PR: 见 PR_BODY.md(push 后回填 URL)

## 工作包终态
| WP | 状态 | 摘要 |
|---|---|---|
| A01 算法资产盘点 | DONE | 3 路审计代理+人工复核,覆盖时序/高光谱/地形/指数/辐射/SAR(polsar)/science_context;审计 CLEAN 面:Whittaker/OLS/Theil-Sen/MK-CI/日历/物候(SA 拟合/重采样/MNF/CEM/OSP/TCIMF/FCLS/FISTA/PPI/库匹配/D8/D∞/Strahler/NOAA 太阳几何/C-校正/Ross-Li BRDF/GLCM/CVA/IR-MAD/Horn 坡向/多向 hillshade 等(全文见评审代理报告摘要,源码级逐行核验) |
| A02 NoData/NaN 收敛 | DONE | #1687-1 修复(魔数+等值负对)+dNBR legacy 缝(M3 冲突解决)+流式 stretch ±Inf 对齐;#1687-2 validCount 文档 |
| A03 时序 | DONE(域内) | harmonicFit clamp(OOB SIGABRT);T2a/T3a suspicious 登记不修 |
| A04 SAR | DONE(核验) | polsar(FD/Yamaguchi4/CloudePottier 相干/特征分解)人工逐式核验 CLEAN,与测试钉住约定一致 |
| A05 高光谱 | DONE | ROI 波长单位(1000× 错)+MNF 空选择+VCA 尺度门+table OOB 拒绝+anomaly 契约文档 |
| A07 地形 | DONE | viewshed 亚米断链(P1)+曲率混合导数(P2)+极点方位 |
| A08 性能 | N/A(如实用) | 全部修复零复杂度变化;viewshed 环索引内存有界论证(R1 §2);无 >=15% 目标项,不虚报 |
| A09 算子契约 | PARTIAL | dNBR 分支缝+readTileBand 契约论证;跨运行时接口未动(B 轨) |
| A10 质量门 | DONE | 12 个直接族两轮 GREEN(10,972 断言)+13 邻域族(含 888,684/130,277 断言大族);6 次变异-杀伤验证(R1×4、R2×2、开发×1) |
| A11 文档 | DONE | 5 个头文件契约同步(math_utils/terrain_analysis/terrain_solar/spectral_anomaly + resampler 既有);无新科研立项 |

## 修复清单(10+1 项,全部 RED→FIX→GREEN×2→双评审)
1. math_utils #1687(负反射率契约+魔数移除) 2. viewshed 亚米环断链 3. 曲率 zxy 模板 4. ROI µm 波长 5. 流式 stretch Inf 6. 极点方位 7. harmonicFit clamp 8. MNF RMSE 空选择 9. VCA 尺度门 10. table OOB 拒绝 11. dNBR legacy 缝(M3)

## 测试与证据
- RED:10/10 新用例在 master 源码上恰好失败(temporal SIGABRT;solar 实测推翻审计 NaN 预测为噪声有限值——如实记录)
- GREEN×2:断言数两轮逐一相同;邻域 24 族全绿;R1 变异 4 次+R2 变异 2 次全部先 RED 后恢复 GREEN
- 命令与退出码:TEST_RESULTS.md;构建全程 -j2+全局 flock(三轨串行,r23-B/C 交叉等待如实入账)

## Issue 状态
- 已修(留 open 待维护者关):#1687
- 本轨未重复修复(FIXED_OPEN 核验):#1589/#1549/#1649/#1631×3/#1646-6/#1550/#1538/#1590/#1591
- OUT_OF_SCOPE 登记:#1646-1..5/#1642×4/#1588 等(权属见 OWNERSHIP)

## 双独立评审
- R1:APPROVE(1×P2+6×P3;P2 端到端钉已补+变异验证;P3 4 修 2 登记;勘误:R1 叙述 9/75 归属互换,数值均真)
- R2:APPROVE(1×P2 披露项+P3 闭 3 登记 1;18 对抗格网 17,280 格不变量实测 0 破)

## 风险与遗留
- R13(#1687 架构披露):未声明 -9999 族哨兵在非 dNBR 指数分支如实计算(R2 反例 NDVI(-9999,0.3)→+1.0000600);遗留产品应声明 NoData;PR body 明示
- R12 seasonal breaks CI 统计量错配(suspicious,未修,需产品语义决策)
- VCA GS 门绝对阈值(纯放宽,登记)
- 提交回退耦合:cc34cb4ed6 与 d4f284c6b4 需成对回退(R1-P3-1)
- 未执行:全仓 ctest(按最窄→受影响→邻域策略);Windows/macOS 实机(单主机,跨平台主张仅涉标准浮点/整数行为)

## 恢复
- worktree/branch 保留;阻塞恢复命令:cd ../exp-rs-r23-A-20261026T164112Z && git log --oneline origin/master..HEAD;ctest --test-dir build-r23-A -R "math_utils|terrain|spectral|mnf|temporal_fit|image_enhancement|image_fusion|formula_drift" --parallel 1
