# TEST MATRIX — D14 Geometric Registration Workbench

9 个新测试目标（D14 runbook ctest 正则全匹配）：

| # | 测试目标 | 包 | 断言核心（真值来源） | 状态 |
|---|---|---|---|---|
| 1 | `test_gcp_manager` | A | 凸包=10000/覆盖率=1/共线=0（鞋带）；CE=4.0、√10（闭式）；Delaunay 纵横比 (√2+1)/2；残差 0.3/0.4→RMSE=0.5；禁用点剔除；CSV/JSON 往返 | 待绿 |
| 2 | `test_geometric_transform` | B | 平移/刚性/相似/仿射解析系数（θ=30°,s=1.5,t=(25,-10)）；P2/P3 手选系数精确还原；DLT 已知 H；共线病态 success=false；批处理一致 | 待绿 |
| 3 | `test_tps_interpolator` | C | U(r)=r²lnr 闭式值；5 节点精确插值 1e-10；仿射再现+零弯曲能；λ 正则能量递减；批处理/去重/拒绝非法 | 待绿 |
| 4 | `test_feature_matcher` | D | 40 内点+20 粗差→粗差 100% 剔除、保留≥38、H≈H_true；<4 点恒等+全 false；比例检验歧义过滤；合成纹理平移 (7,-5) 恢复 | 待绿 |
| 5 | `test_resampler` | E | Keys 核闭式值+单位分解 1e-12；Lanczos 闭式；解析平面 (1.4,2.6)=20.6（双线性/三次/Lanczos）；NoData 50% 权重规则；warp 恒等/平移/过冲截断 | 待绿 |
| 6 | `test_pansharpening` | F | Brovey/GS/IHS/HPF 常量场景解析恒等；GS 物理门 ERGAS≤2.5、CC≥0.94；均值漂移<1%、无负值；evaluateQuality 恒等性 | 待绿 |
| 7 | `test_georef_dual_window_workbench` | G | 无头实例化；防重入探针同步期间=true；50 次高频拖拽镜像一致不递归；GCP 表格行数/RMSE 联动；模型切换 RMSE 退化；warp 信号 | 待绿 |
| 8 | `test_geometric_agent_tools` | H | schema draft-07+action 枚举；信封契约；8 点 1 粗差→恰标 GCP_04（mean=2.75、RMSE=√29 解析）；决策树全覆盖；inspect 结构化失败/成功 | 待绿 |
| 9 | `test_d14_geometric_registration_e2e` | I | lab06 全链路 100/100（拟合 vs 解析逐像素 <1e-2px）；lab07 GS+Wald 100/100；劣化方案 RMSE=2.5（解析 δ）精准扣 35 | 待绿 |

运行命令（离线、无头、ctest -j1）：
```
QT_QPA_PLATFORM=offscreen ctest --test-dir build -R \
"test_gcp_manager|test_geometric_transform|test_tps_interpolator|test_feature_matcher|test_resampler|test_pansharpening|test_georef_dual_window|test_geometric_agent_tools|test_d14_geometric_registration_e2e" \
--output-on-failure -j1
```
（注：`test_georef_dual_window` 同时命中仓库既有同名 QGIS georeferencer 测试，
master 绿基线，一并纳入回归证据。）
