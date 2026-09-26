# REVIEW_LOG — core-foundations-r4(独立对抗性 review,Phase 5)

Review:只读子代理(与实现分离),对象 `origin/master..HEAD` 全量 diff + planning 工件;方法含**变异验证**(删除被 pin 的代码行验证 pin 是否变红)。原始结论:**SHIP-WITH-FIXES**(P0=1,P1=3,P2=4,P3=4)。全部发现逐条处置如下;修复提交见各条。

## P0 — 必修(真实缺陷)

| ID | 发现(证据) | 处置 | 状态 |
|---|---|---|---|
| P0-1 | `export_manifest.cpp:201` 新增的 `fsyncFile` 裸调用位于 **bool+error 出参** 契约的 `writeExportManifest` 内;`fsyncFile` 抛 GeoError 而调用方 `produce.cpp:336/404` 无 try/catch → fsync EIO/EDQUOT 时异常逃逸,**绕过已交付页面的回滚路径**(produce.cpp:330-345) | 包 try/catch → `fail(...)`,与该函数全部既有失败出口同型;autoRemove 兜底清理 staged | 已修(commit:fix(review P0)) |

## P1 — 必修(静默错误/测试效力洞)

| ID | 发现 | 处置 | 状态 |
|---|---|---|---|
| P1-1 | `rs_post_process.cpp:614-623` 本轨新增的 fsync 失败 catch 只 setErr+return,**滞留 staged 栅格**(相邻两条失败路径都清理) | 该 catch 内补 `drv->Delete( tmpPath … )`,与 :640 发布失败路径同一纪律 | 已修(同 P0 提交) |
| P1-2 | journal 清理 pin 对**第二分支**盲(rfind 解析到第一分支的 cleanup,变异删除第二 cleanup 仍 PASS) | pin 改为"cleanup 必须落在**上一分支的 return false 之后**、本 marker 之前"区间断言;两分支各自被锚定 | 已修(commit:fix(review pins)) |
| P1-3 | 单文件 publisher pin 只保护每文件**第一处** publish 锚(`source.find` 首现);export.cpp 第二处(atlas 页循环)变异删除仍 PASS | pin 表增加 sites 计数:逐锚点遍历,要求每对相邻锚点之间存在独立 fsync 命中 | 已修(同 pins 提交) |

## P2 — 采纳

| ID | 发现 | 处置 |
|---|---|---|
| P2-1 | EVIDENCE §5 计数与矩阵自相矛盾("26 一致/6 偏差/5 defer" ≠ 35,实际偏差(已修)=11) | 更正为矩阵口径:11 偏差(已修)/ 一致面 22 / defer-#1338 处置 6(4 业务 + 合同定义面 2);EVIDENCE 修正提交 |
| P2-2 | 严格白名单读法:改动了两个**既有**测试文件(platform_portability +140、source_contract +81,均纯追加 0 删) | 归入"声明扩展"记账(prompt 允许 tests/ 白名单 + 扩展逐处记账):DECISIONS D-7 声明,ledger 记录 |
| P2-3 | 混合分隔符用例为 POSIX 语义(反斜杠文件名)但落在 Windows 无守卫区 | 用例加 `#if !defined( _WIN32 )` 守卫 |
| P2-4 | 注入类计数口径:严格去重 13 主体类/21 用例/~25 类×设备组合;publishStagedGroup 注入依赖既有 lane(defer 说明) | EVIDENCE/PR 按双口径如实陈述:≥20 按"注入 TEST_CASE 类(21)+合同 lane 失败用例(6)"计;严格主体计数 13 一并披露,不做单一夸大口径 |

## P3 — 全部采纳

- 账本 1.2 行 "3 文件 defer" → 更正 **4**(rows 11/19/21/27)。
- 矩阵 row 9 使用面 API 修正:仅 `writeFileAtomic(1)` 为直调,其余为合同面转引(加"转引"限定)。
- BASELINE §7 账本表述更正:该文件为 **master 跟踪的跨轨道追加式账本**,本轨按仓库惯例追加提交(非 gitignored);`.planning` 证据以 `git add -f` 入库不变。
- 删除 `test_core_failure_injection_r4.cpp` 未用 include `raster_reader.h`……经复核 RasterWindow 确由其引入 → 改为保留并在 P3 记录说明(见 fix 提交注)。

## Review 确认的强项(原文摘录)

1. 矩阵是**读过代码写的,不是抄的**(19 行抽查行号全对,含 row 2 主路径修复的细节)。
2. 白名单合规 + 门禁放置正确(11 个 src 文件全属 35 文件集合;组发布 flush 全部存在性守卫;单文件门禁都在第一个 rename 机制之前)。
3. 并发 lane 无竞争且有效力(按值捕获、原子量、字节精确合法集;POSIX rename 语义下 monitor 真不可能见到半文档)。
4. "4 预存红 = #1338 Cluster A" 闭包论证**airtight**(io 测试闭包与本轨改动零交集,注册行不改变闭包)。

## 复核(remediation 后)

- P0-1/P1-1 修复后重建 + 合同/注入 lane 重跑绿(见 EVIDENCE §7 双跑区)。
- P1-2/P1-3 修复的效力:按 reviewer 的变异方法反向自证——删除 atlas 处 fsync 与第二 cleanup,两 pin 均转红(变异记录入提交注)。
