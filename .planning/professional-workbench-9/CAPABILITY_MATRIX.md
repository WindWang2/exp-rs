# CAPABILITY MATRIX — master (f316dfdbb4) 现状 vs 9.0 计划新增

图例：✔ 有实现+生产调用方+测试；◐ 部分实现（注明缺口）；✘ 无。

## A. UI 生命周期 / 安全

| 能力 | 状态 | 证据 / 缺口 |
|---|---|---|
| SelectionContext 悬垂层过滤 + 地址复用放行 | ✔ | #849 修复 + 回归测试 |
| per-owner scan generation + GUI 回调 marshal | ✔（scan 池用户） | #861 修复；M0 补 per-owner 回归 |
| canvas teardown settle | ✔ | `stopRenderingAndSettle()` 三处 |
| bridge 显式 delete | ✔ | removeView + record 清理 |
| project clear/switch stress 无 crash/hang | ◐ | shutdown_policy 测试有；**缺系统性 clear→import→exit 循环 stress** → M0/M3 |
| destroyed widget 丢弃旧 callback 的**通用**封装 | ◐ | 各处手写 QPointer+epoch；**缺一个复用 helper**（DeadDrop/protected callback）→ M0 沉淀 |
| QPointer/lifetime/cross-thread 审计清单 | ✘ | → M0 审计 + REVIEW_LOG 记录 |

## B. State model / command authority

| 能力 | 状态 | 缺口 |
|---|---|---|
| ContextFacts（selection/raster/vector/sar/edit/governance/broken/inflight） | ✔ | M1 扩展：no-project/empty-project、active view、tool mode、dialog/panel visibility、task 细分状态 |
| ContextRules 纯函数 + unavailabilityReason + suggestedNextAction | ✔ | 新 facts 的规则补齐 |
| CommandRegistry（唯一 id/shortcut owner，重复拒绝） | ✔（登记侧） | **消费侧双权威**：main_window_menus 裸 action 持有 Ctrl+N/O/S（F1）→ M2 |
| menu/toolbar/palette 全部引用 registry | ◐ | palette/ribbon 走 registry；主菜单裸 action → M2 |
| empty-state CTA 指向真实 command | ◐ | CTA 已接 importLayer()；pipeline dock tooltip 撒谎（F2）→ M2 |
| duplicate shortcut 机械检测 gate | ◐ | registry 注册时拒绝；**缺 shell 级全量审计测试**（注册后的 shortcut 唯一性 + 非 registry QAction 不持 canonical）→ M2 |
| plugin commands 进 registry | ✘? 待核 | M8：plugin declarative command → registry 统一 |

## C. QGIS canvas / views

| 能力 | 状态 | 缺口 |
|---|---|---|
| 多视图 + add/removeView + bridge | ✔（display manager） | M3 窗口销毁顺序测试补强 |
| dual viewport sync | ✔ | — |
| async render callback 安全投递 | ◐ | M3 审计 canvas job 回调与 destruction order |
| map tools 生命周期 | ◐ | M4 审计 tool 切换/销毁 |

## D. Layer & data interaction

| 能力 | 状态 | 缺口 |
|---|---|---|
| layer 可见性/selection/active layer/style refresh/edit mode | ✔（主窗口+layer tree menu） | M4 broken layer 处理一致性 |
| invalid/broken layer 状态 | ◐ | hasBrokenLayer fact 有；UI 呈现一致性 → M4 |
| 大 layer tree（分组、上千层）| ◐ | M4/M7 边界 |
| Data Manager 与 canvas 一致 | ◐ | 8.0 做了 catalog index；M4 一致性回归 |

## E. SchemaForm / workspace / UX

| 能力 | 状态 | 缺口 |
|---|---|---|
| SchemaForm 4.0（嵌套/数组/async check/a11y） | ✔ | — |
| 生产 `SchemaEnumProvider` | ✘ | **M6 核心**：dataset/layer/model/operator 枚举装配 |
| preserving values across schema refresh | ◐ | 8.0 部分；M6 验证补齐 |
| oneOf/variants | ✘（有意拒绝） | 维持，除非真实 producer 出现 |
| catalog 20k 行有界渲染 | ✔ | M7：200k+ 逻辑行、分页/虚拟化、内存度量 |
| store 侧 query pushdown | ✘ | M7 经 data seam 定义 UI 意图接口 |
| plugin declarative UI（menu/toolbar/dock/prefs） | ◐（8.0 protocol 1.1 + plugin_shell_ui 雏形） | M8 完整 surface + unload/reload/crash 状态 |
| 主题/HiDPI/a11y/empty-loading-error 全覆盖 | ◐ | theme parity 测试有雏形；M9 系统化 |

## 禁止重复实现清单（先证明后动手）

- 第二个 layer model / catalog 权威（只用 AssetCatalogIndex + QgsProject）；
- 第二个 executor/scan 线程池（只用 RsScanPool / TaskCenter）；
- 第二个 schema/form 框架（只扩展 schema_form_builder）；
- 自研 dock/toolbar 管理（用 registry projection + QMainWindow 机制）；
- 自研枚举存储（provider 只读既有 stores/registries）。
