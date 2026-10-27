# RISK_REGISTER — r23-C

| ID | 风险/遗留 | 影响 | 优先级 | 处置 |
|---|---|---|---|---|
| R-1 | R2-F3 乱序场景依赖多线程 operator notify 重排，单线程顺序流不触发 | TaskCenter 日志在极端交错下丢行/重行（用户可见：任务日志缺行） | P3（R22 评审定级） | WP-1 修复+确定性契约测试 |
| R-2 | #1695-17c teaching_admin 批量发现仍在 GUI 线程同步执行 + dtor join | UI 卡顿（大数据量）；本轨不修 | P3 | 转记（PR body 报告）；建议后续轨 |
| R-3 | #1632-3 exp_gui::SpectralProfileWidget 重复类仅测试使用 | 死代码/读者困惑 | P3 | 不动（删除属清理决策）；转记 |
| R-4 | #1655 CP11 渲染门：5 渲染用例在一切自动车道永久 SKIP | 渲染路径零自动覆盖 | P3 | 不动（启用需健康主机验证 legend paint 挂死再现性）；转记 |
| R-5 | #1646-2 local_worker_pool m_running 无锁读（形式 UB） | TSan 可检；既定契约下良性 | P3 | WP-4 候选；若评审建议纳入则 atomic 化 |
| R-6 | tests/CMakeLists.txt 并行 append 冲突 | 注册丢失/冲突标记（#1714 前科） | P1(流程) | push 前逐字节对照 origin/master 版本；仅 append |
| R-7 | 本机 GCC 16.2.1 vs 项目惯用 gcc-15（账本记录 R22 用 gcc-15） | 编译器差异引入假红/假绿 | P2 | 记录实际工具链；若遇 ICE 按 R3 轨教训禁用 PCH 重配 |
| R-8 | TaskCenter 为进程单例，测试间状态泄漏 | 闪断 | P2 | 沿用 shutdownForTests + clearExecutors 惯例；RUN_SERIAL 不必（直跑单进程） |
| R-9 | R2-F2：atomic_fs::publishStagedMembers 对缺失 staged 成员静默跳过、空列表视为成功（src/geospatial/util/atomic_fs.cpp:842-850）——无并发删除时不可达 | 理论上 true-但未发布 | P3 | **非本轨白名单**（src/geospatial）：转交拥有轨；本轨调用点两 staged 文件由同进程刚写入，删除窗口不存在 |
| R-10 | R2-F5：新 %n 源串 "entry(ies)" 未入 sicnu_zh_CN.ts（仓库无 .ts 新鲜度 CI 门）；"(ies)" 后缀对译者不友好 | 翻译缺失（中文无复数形态，功能无损） | P3 | 接受：与 QGIS core "feature(s)" 先例一致；.ts 由 lupdate 管线统一刷新，手工添加易与他轨冲突 |
| R-11 | R2-F6：相对 outPrefix 时 journal/.bak 路径保持相对（staging 侧已 canonicalize） | 极角；app 调用方恒绝对路径 | P3 | 已在头文件契约注明「outPrefix 必须带目录成分」 |
