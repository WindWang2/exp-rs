# Ticket TICKET-41: 活动视图追踪 Seam

- **类型**: `task`
- **状态**: 已关闭 (Closed)
- **父级地图**: [MAP_qgis_display_manager_auto_registration.md](file:///home/kevin/projects/exp-rs/docs/tickets/MAP_qgis_display_manager_auto_registration.md)

## 问题 (Question)

如何在 `QgisDisplayManager` 中维护活动视图跟踪状态？

## 决议 (Resolution)

在 `QgisDisplayManager` (`src/app/display/qgis_display_manager.h`) 中：
1. 增加 `setActiveViewId(DisplayViewId viewId)` 与 `activeViewId() const` 接口。
2. 当 `createView` 被调用且当前无活动视图时，自动将新建视图设为活动视图。
3. 增加 `activeViewChanged(DisplayViewId newViewId)` 信号。
