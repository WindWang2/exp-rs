# R21-D REVIEW_R2（fresh reviewer #2，独立上下文/采样；R1 修复后复审）

- 基线 `911e0b17a3`，被审 HEAD `75a9bdf247`（R1 修复提交 `db93b2655e` 之后）。
- 评审员实证手段：全 diff 逐 hunk + QGIS 源码语义核对 + 实跑测试 + **自行重构 RED 探针**（改生产代码→重编→跑→还原→复跑 GREEN），全部临时改动已还原。

## 总体结论（时点）：APPROVE-WITH-FIXES → 4×P2 修复于 `8c3ff74bd6` → 复跑 GREEN

R2 独立 RED 重放确认 R1 的 P1-1（extent/CRS 单位契约）与 P2-2/P2-3 真实修复且测试可失败；W1b lease 括号五分支逐一走查自洽；无 P0/P1；无历史 patch 回滚（R20-04 三腿、R18-D NaN 安全 DPI、R19-03 会话门全部复跑绿）。

### P2 发现与处置（全部修复于 8c3ff74bd6）

| 级别 | 发现 | 证据 | 处置 |
|---|---|---|---|
| P2-1 | produce 导出取消探针仍每次轮询 report()，detail 串变化击败进度限流 → **每次成功的多页导出都推 "export: export cancelled" 进度事件**；R1 闭环注释/提交信息虚假声称已修；`exportCancelled` 死变量 | produce.cpp:417-432 | 取消判定在导出前快照一次，探针读纯标志 |
| P2-2 | R1-P2-1 "全部 isBool/isString 守卫"不实：4 处 `asString()` 遗漏（label `color`、scalebar/picture `linked_map`、map `layers` 数组元素），错类型抛异常半应用 | layout_service.cpp:921/1124/1182/970 | 全部加守卫；非字符串 layers 元素进 ignored |
| P2-3 | frame 声明 CRS 时 grid interval 仍按项目 CRS 单位、被 grid 按项 CRS 读 → 1 度声明为 1 米 ≈ 9 万网格线（851151b449 引入的新风险） | mapspec_compiler.cpp:960 | interval 按 extent 同一变换缩放 |
| P2-4 | mapspec-reference.md 无 frame `crs` 文档（本轨最重新语义只存在于代码注释） | docs/cartography/mapspec-reference.md | 补 map_frames crs 段（单位契约/无效拒绝/inset 与 page.crs 排除） |

### 采纳的 P3（零成本项，同提交修复）

- R2-P3-1： extent 变换失败从"静默保留未变换 extent + setCrs"改为 typed 编译拒绝（消除与 20 行外原则的自相矛盾）。
- R2-P3-3： 陈旧 sibling 清理与 `deliveredFiles` 出参解耦（清理是写入自身属性，所有调用方一致）。
- R2-P3-4： 协调器 acquireEditLease 增加 `showConflictWarning` 形参；重取路径传 false（状态栏，与 shell saveEdits 一致），不再弹模态。
- R2-P3-5： facade toggleEditing 的 commit 失败经 message bar 上报（此前静默 return false）。
- R2-P3-6： USER_GUIDE 补 layout:export 的 page_count 语义说明。
- R2-P3-7： .planning 记账随提交合并（PR_BODY.md 入库）。

### 记录不修（附理由）

- R2-P3-2（取消回滚无法恢复被本次覆盖的上一版同名文件）：注释"leaves no trace"过度声称已收窄为"本 run 已交付页"；恢复上一版需要预读旧文件（磁盘/内存成本）且原子发布契约本就是"新全量或不动"——作为已知限制记录。
- R1-P3-3（manifest creates_artifact 与 remove 并存）：实测上限 16 次 remove；`test_harness_catalog.cpp` 钉住 kCreatesArtifact，改类需同步改测试——记录。
- R1-P3-4（kStaleSiblingSlack=16 孤儿）：上版 >16 页时 `_19..` 之后留孤儿，有界且下次导出自愈——记录。
- R1-P3-5（page_count 跨表面 pdf/svg 不一致）：produce 的 pdf page_count = 布局页数（manifest 语义），layout:export = 交付文件数；两者文档已各自说明——记录。
- R1-P3-7 / R2 page.crs：文档明示 provenance-only（"Not consumed by the compiler"），series_planner 是唯一非项目 CRS 单位 extent 的生产者但 src/ 内无生产调用方；修为改全局 extent 单位契约，成本/收益倒挂——记录 + mapspec_compiler 注释已软化引用并点名例外。
- R2-P3-8（bbox 变换 vs densify 保真度）：QGIS 标准 transformBoundingBox 行为，附注已说明。

### R2 对 R1 发现的逐条验收

| R1 发现 | R2 结论 |
|---|---|
| P1-1 extent 变换 | **FIXED**（R2 自行 RED 重放：把 zoomToExtent(transformed) 置假 → 4 断言红；方向/4326 回落/atlas/inset/locator 均核） |
| P2-1 类型键守卫 | **PARTIAL → 已补全**（R2 找出 4 处遗漏，8c3ff74bd6 修复） |
| P2-2 取消 seam | **FIXED**（R2 RED 重放：删三处 poll → 3 断言红） |
| P2-3 清理时机 | **FIXED**（R2 RED 重放：删清理块 → stale 断言红） |
| P2-4 OWNERSHIP | **FIXED** |
| P3-1 probe detail 事件 | **NOT-FIXED（时点）→ 已修**（R2-P2-1） |
| P3-2 facade 重 acquire 静默 | **NOT-FIXED/记录失准 → 已修**（R2-P3-4，实为弹模态） |
| P3-3/4/5 | ACCEPTED-AS-RECORDED |
| P3-6 W1b 五分支无测试 | R2 逐分支走查自洽；**3 个新测试已补**（commit 失败/acquire 拒绝/无 assetId），8c3ff74bd6 |
| P3-7 page.crs | RECORD（不修，理由如上） |
| P3-8 PR_BODY 引用 | FIXED（REVIEW_R1.md + 本文件） |

### R2 修复后复跑（2026-10-24）

- r14_D 440/440（24 用例，含 3 个新 W1b 分支用例）；CP11 [r21d] 44/4；layout [r21d] 27/2；mapspec 2830/239；layout_tools 1338/16；CP11 默认 29+3 已知环境红（render-off 0 failed）。
