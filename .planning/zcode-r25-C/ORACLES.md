# ORACLES — R25-C（对照任务书 §7）

| 任务书 Oracle | 本轨实现 | 状态 |
|---|---|---|
| 1. ≥4 条真实 E2E 成功/拒绝/取消/恢复链 | E2E 脚本 5 链：成功发现（含 W2 字段）、TaskCenter 执行、类型化拒绝、取消、崩溃→checkpoint 诊断→resume 完成；真实 run_id/typed diagnostics | 待二进制构建后执行 |
| 2. ≥3 项功能深化且 schema 一致 | ① bridge 类型化错误+代际漂移（W1）② capability prerequisites/limitations（W2）③ workflow 恢复诊断+resumable（W3）；schema 变更走 snapshot 再生 | W1 ✅；W2/W3 待验证 |
| 3. 恶意输入矩阵 fail-closed | E2E 拒绝链（../escape run_id）+ r22 回归钉（URI 逃逸/env 注入/IPC/ghost）维持 | 待跑 |
| 4. 非法输入不触发执行、幂等、资源回收 | 拒绝在 dispatch 层（验证先于执行）；resume 幂等语义复用生产状态墙；E2E 崩溃后资源回收（进程终结 ladder） | 待验证 |
| 5. 有界内存/帧/竞态 oracle | bridge 侧 4MiB 出站/32MiB 入站上限保持（既有钉）；r18c 读写竞态修复（bounded poll） | W1 ✅ |
| 6. pi/test + C 轨 test_r25_c_* + 既有回归 ×2 | pi suite ×2 已绿；C++ 侧待构建 ×2 | 进行中 |
| 7. C/D 轨隔离 | 未触碰 cartography/mapspec/layout/symbology；ChartRegistry 未动 | ✅ |
