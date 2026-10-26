# R21-D REVIEW_R1（fresh reviewer #1，独立上下文/采样）

- 基线 `911e0b17a3`，被审 HEAD `d3d356f9f2`（R1 时点；R1 修复提交 `db93b2655e`）。
- 评审员实证手段：全 diff 逐 hunk + QGIS 源码语义核对 + 实跑测试（runner）+ 临时探针用例（已还原）。

## 结论（时点）：REJECT → 1×P1 + 3×P2 → 全部修复于 `db93b2655e` → 复跑 GREEN

### 发现与处置

| 级别 | 发现 | 证据 | 处置（db93b2655e） |
|---|---|---|---|
| **P1-1** | frame CRS 修复引入新回归：声明 crs 未变换 extent，项目 CRS 度数被按声明 CRS 单位解释 → 静默渲染错误区域（空图/几内亚湾） | mapspec_compiler.cpp:574-590（原）；zoomToExtent 无变换 + setCrs 不改 extent；探针实证 extent=[116.1,38.95,116.2,39.04] 度 + crs=3857 | extent 变换进声明 CRS；项目 CRS 无效回落 EPSG:4326（与 item crs() 回落一致）；测试补 extent 断言（44/4） |
| P2-1 | P3-2 只硬化 4 个布尔键；exclude_from_exports/frame_enabled/background_enabled 的 asBool、name/text/title/linked_map/style/unit/unit_label/path/north_mode 的 asString 对 array/object 仍抛 → 半应用 | layout_service.cpp:814-844 等 | 全部 isBool()/isString() 守卫 |
| P2-2 | 取消 seam 只覆盖多页 png/svg；pdf（生产主格式）与单页栅格无 seam → Stop 后 PDF 已落盘 | export.cpp:247/266（唯一 cancelled() 在多页循环内） | 单文件路径 exporter 前 + publish 前各 poll 一次 |
| P2-3 | P3-1 陈旧 sibling 清理在导出前 → 导出失败摧毁用户上一版交付 | layout_service.cpp:1445-1458 vs 1481-1486 | 清理移到导出成功后、只删超出新交付页数的 sibling |
| P2-4 | OWNERSHIP.md 未列 src/agent/mapspec/**（P2-1 提交改动了它）→ O-G5 白名单不符 | OWNERSHIP.md vs diff | 补入白名单 + 三腿回归义务 |
| P3-1 | produce 的 reporter-as-probe 每轮询推 detail 事件（未取消也显示 "export cancelled"） | produce.cpp:422-426 | 探针只在取消分支 report |
| P3-2 | facade 重新 acquire 失败静默（shell 同场景有状态栏消息） | qgis_app_facade.cpp:274-276 | 记录（P3，行为与 shell 的"另一视图正在编辑"消息等价缺失；风险低，未改——保持最小 diff；见 R2 复核） |
| P3-3 | layout:export 执行最多 pageCount+15 次 remove 但 manifest 标 creates_artifact | tool_manifest.cpp:46 | 记录（R2 复核） |
| P3-4 | kStaleSiblingSlack=16 有界清理对 >16 页收缩留孤儿 | layout_service.cpp | 记录（R2 复核） |
| P3-5 | page_count 语义由布局页数改为交付文件数，未钉契约 | layout_tools.cpp | 记录（R2 复核） |
| P3-6 | W1b 五类分支（commit 失败/acquire 失败/无 assetId/无 coordinator/startEditing 失败）无测试 | — | 记录（R2 复核） |
| P3-7 | page.crs 仍是声明不编译字段（page 级） | mapspec.cpp:1278/1321 | 记录（R2 复核） |
| P3-8 | PR_BODY 引用不存在的 REVIEW_R1/R2 | — | 本文件补写 |

### 核验为非缺陷（附证据）

W1b lease 括号逐分支自洽（commit 失败保留 buffer+lease 重入编辑；leaveEditable 重 acquire；批量按 id 重解析跳过换层；无 coordinator 回落原生 QGIS；acquire 失败不 startEditing；startEditing 失败即 rollback；stopEditing Save/Discard/Cancel/未修改四分支）；属性表 mLayer 是 QPointer 协调换层后为 null 非悬空；P2-1 的 extent/CRS 顺序本身安全；P3-1 jpg/jpeg 后缀一致；跨轨 API 默认参数向后兼容全部调用点编译通过；无历史 patch 回滚（R20-04 DPI 地板/R18-D NaN 安全/GDAL 取消探针文件未触碰）；性能无 O(n²)；测试非恒真（各断言均可在旧代码失败）。

## R1 修复后复跑（2026-10-24）

- `[r21d]` CP11：44 断言/4 用例全绿（P1-1 extent 断言在旧实现下红：`1289062.5 > 1e7` false 级）。
- mapspec 2830/239、layout_tools 1338/16、CP11 默认 27 passed + 5 skipped（render off）/ 29+3 已知环境红不变。
