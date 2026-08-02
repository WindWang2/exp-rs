# Wayfinder Map: Encapsulate Display Layer Auto-Registration in QgisDisplayManager

## 目标 (Destination)

深化 `QgisDisplayManager` (`src/app/display/qgis_display_manager.h`)，使其封装当前活动视图追踪 (`setActiveViewId` / `activeViewId`) 与 `DataManager::assetAdded` 自动显示监听，为桌面 UI 与 Headless 环境提供内聚的图层自动呈现与视图生命周期管理深层 Seam。

## 说明 (Notes)

- **领域词汇**: 参考 [CONTEXT.md](file:///home/kevin/projects/exp-rs/CONTEXT.md) 中的 Display View, Display Layer, Active View Host, Data Manager, Data Asset 术语。
- **架构词汇**: 运用 `/codebase-design` 深度模块术语 (**module**, **interface**, **depth**, **seam**, **adapter**, **leverage**, **locality**)。
- **相关 ADR**: ADR 0009 (Data Asset/Display Layer 分离), ADR 0019 (ActiveViewHost Single-Active View Routing).

## 决策记录 (Decisions so far)

- [TICKET-40: 目标与边界声明](file:///home/kevin/projects/exp-rs/docs/tickets/TICKET_40_display_manager_destination.md) — 确定 `QgisDisplayManager` 封装活动视图与自动图层呈现的目标与边界。
- [TICKET-41: 活动视图追踪 Seam](file:///home/kevin/projects/exp-rs/docs/tickets/TICKET_41_active_view_tracking_seam.md) — 在 `QgisDisplayManager` 中增加 `setActiveViewId` / `activeViewId` 跟踪。
- [TICKET-42: DataManager::assetAdded 自动显示订阅](file:///home/kevin/projects/exp-rs/docs/tickets/TICKET_42_auto_display_subscription.md) — 增加 `setAutoDisplayOnAssetAdded(bool)` 开关，自动监听并呈现新建资产。
- [TICKET-43: 主窗口与 ActiveViewHost 代码解耦](file:///home/kevin/projects/exp-rs/docs/tickets/TICKET_43_desktop_window_decoupling.md) — 简化桌面主窗口与 `ActiveViewHost` 中的桥接闭包，并通过单元测试验证。

## 待确定事项 (Not yet specified)

- 无。

## 超出范围 (Out of scope)

- 修改 QGIS 渲染引擎与 `QgsMapCanvas` 画布重绘机制。
- 修改 `DataManagerPanel` 的 UI 列表样式。
