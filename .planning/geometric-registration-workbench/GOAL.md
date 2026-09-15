# GOAL — D14 Geometric Registration Workbench

无人值守长程交付：在隔离 worktree 中按 Matt Pocock TDD 纵向切片落地 9 个工作包
（GCP 管理/空间分布分析、闭式几何变换 SVD 库、TPS 非刚性校正、SIFT/ORB+RANSAC
自动配准、多方法重采样流水线、GS/Brovey/IHS/HPF 全色锐化、双视窗联动工作台 UI、
遥感 Agent 几何工具、lab06/lab07 端到端判分套件），全部本地离线 Catch2 绿灯，
双轴审查 P0=P1=0，最终归档 EVIDENCE 并提交 PR。

验收硬线：
1. 9 个新测试目标 100% 绿（`QT_QPA_PLATFORM=offscreen`，ctest -j1）；
2. 断言真值全部来自独立解析解/标准（凸包鞋带、30° 旋转解析系数、核单位分解、
   Wald ERGAS/CC 阈值），零同义反复；
3. 黑盒测试：仅 Public Seam，无私有探测/无内部 Mock；
4. Qt UI：QPointer + mApplyingSync 防重入 + 16ms 节流，无头可实例化；
5. 资源红线：ninja -j2（内存压力降 j1）、ctest -j1、零远端 CI。
