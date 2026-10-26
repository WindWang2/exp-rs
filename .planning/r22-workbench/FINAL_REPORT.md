# ZCode R22-05 FINAL_REPORT

轨道：Qt/QGIS 桌面工作台交互、图层状态、制图与布局导出的易用性闭环（r22-workbench）。
会话：2026-10-25/26（单会话多轮 goal-loop：盘点 → 分诊 → 双子代理审计 + 人工复核 → W1-W9 实施RED→GREEN×2 → R1/R2 双独立评审闭环 → PR）。

## ① 基线（LIVE）

- `origin/master` = `911e0b17a3533ca0d79c1b68b000d6052c4c3c61`（fetch + GitHub API 双确认，与任务书一致）。
- Open PR = 5（#1673-#1677，R21 轨；#1677 与本轨白名单重叠最大 → 全程只读其文件）。
- Open issues ≈120；第一批指定 issue 全部动态验真（ISSUE_TRIAGE.md：#1557 FIXED_OPEN、#1655 CONTESTED→跨轨、#1677 遗留 2 项 BLOCKED_ON_#1677、#1695 仅第 1 项在本轨 live）。
- 历史技术债 #1632 六项全部源码级证实"master 已修"（**避免 6 个重复修复**）；R17-ui 新批 #1678/#1688/#1689/#1694 全部白名单外 → CROSS_TRACK。

## ② 交付（14 commits，911e0b17a3 → HEAD）

| 包 | commit | 内容 |
|---|---|---|
| W1 | deaa6473e9 | #1695-1：async runner 生命周期（QPointer 父、析构恢复 override cursor、push 标志恢复、null-parent 完成送达）— RED 含父亡 UAF SIGSEGV 实证 |
| W2 | 6441a81f18 | temporal 11.0 三算法页路由（seasonal/model/phenology 落汇总页→参数不可达） |
| W3 | befa585a2b | zoomToNativeResolution 1:1 窗口重投影进画布 CRS（5 个数量级错位） |
| W4 | 4e955d063f | 组 Remove 走 ActiveViewHost::removeLayers → DisplayManager（租约/记录不再滞留） |
| W5 | 5676a29875 | log 面板键入标签过滤生效（currentData 空导致静默忽略） |
| W6 | 077d676775 | WorkspaceBrowser 重绑释放旧 ImportCenter |
| W7/W8 | efc640815e + sing | DPI: ASCII 冒号；layout 设计器 4 处 item(s)/component(s) 单复数；移除确认单复数 |
| W9 | 2dfe738f97 | 共享 sicnu::app::jobCancelInFlight；工具箱 close/reject 在 cancel 已武装且 worker 未启动时放行 |
| extras | 8c93758841 | map tool 计数由清单推导（19→真实 20）；TaskCenter 外部取消停止批处理链 |
| R1 闭环 | d8d99ee28d | close 门加 workerStarted；executor 空反馈守卫；模态后节点守卫；组仅空时移除；nav 计数列表化 |
| R2 闭环 | a1f1fd8da6 | workerStarted→atomic；reject/Esc 共享 canCloseSafely 门；removeLayers→void；predicate 注释 |
| docs | ef7857607e/27f25d114e + 终批 | 规划/评审/测试证据 |

## ③ 证据

- RED→GREEN×2：5 新目标；RED 含 2×SIGSEGV（父亡 UAF、组默认动作）；输出持久化 RED_GREEN.md；R2 评审员独立 revert 复现 RED。
- 邻域回归 12 目标全绿（含 m3_stress 5241 断言）；关键崩溃类双跑。
- R1（fresh）：APPROVE-WITH-FIXES（1×P1+1×P2+4×P3）→ 全闭环。
- R2（另一 fresh，独立构造反例+独立 RED 复现）：APPROVE-WITH-FIXES（2×P2+3×P3/4）→ 全闭环。
- 白名单：diff 100% 在 src/app/**（无 teaching_admin）、tests/test_r22_workbench_*、tests/CMakeLists.txt(append)、.planning/r22-workbench/**（两轮评审均独立核验）。

## ④ Oracle 对照（任务书 §7）

| Oracle | 状态 | 证据 |
|---|---|---|
| 1 可点击操作与真实可用性对应 | 🟡 部分 | W2（参数页不可达=隐性不可用）、W9（关闭被永久困住）、extras（批处理取消语义）修复；全量逐按钮矩阵未做（既有族+缺陷驱动） |
| 2 跨 view layer/CRS/属性一致 | ✅（本轨切片） | W3 CRS 一致性、W4 记录/租约一致；E1 双窗口测试 BLOCKED_ON_#1677（如实） |
| 3 MapSpec/Atlas 交付契约 | ➖ | #1677 未合并符号占用；本轨不重做（防覆盖别轨成果）；记 CROSS_TRACK |
| 4 既有 GUI 工作流 offscreen 自动验证 | ✅ | 12 既有族 + 5 新目标全绿（含 m3_stress/m2_batch_b 全链） |
| 5 #1677 现有修复核对 + CP11 诚实标签 | ✅ | ISSUE_TRIAGE/TEST_RESULTS：CP11 环境差异如实记录，未设门强跑 |

## ⑤ 遗留（如实）

- 双窗口编辑租约集成测试、Atlas 覆盖恢复：BLOCKED_ON_#1677 合并。
- 对话框 close 生命周期行为级 E2E（真实算法中途 Esc）：NOT_EXECUTED，源码契约+双评审推演替代。
- zh_CN .ts 六处源串变更产生孤儿条目（resources/translations 不在本轨白名单）→ CROSS_TRACK 附串清单。
- python 脚本编辑器输出无上限/无取消、temporal 同步 GDAL 全量重扫、魔棒每点击 GDALOpen、CrossSectionWidget 死代码：发现并记录（CROSS_TRACK），未实施。
- tests/CMakeLists.txt EOF append 与 r22-inference 轨文本冲突：合并时双方保留（已在 PR body 说明）。

## ⑥ 环境（供复现）

- `/tmp/r22wb-configure.sh`（本轨会话内）记录全部 CMake 入参；构建锁 `/tmp/exp-rs-r21-build.lock` 串行；-j4 上限全程遵守。
- 运行环境 shim 仅存在于宿主（wrapper2 stub/Qt shim），不入产品提交。
