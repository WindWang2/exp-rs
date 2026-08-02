# Ticket TICKET-40: 目标与边界声明 (Scope & Destination Statement)

- **类型**: `grilling`
- **状态**: 已关闭 (Closed)
- **父级地图**: [MAP_qgis_display_manager_auto_registration.md](file:///home/kevin/projects/exp-rs/docs/tickets/MAP_qgis_display_manager_auto_registration.md)

## 问题 (Question)

`QgisDisplayManager` 深化重构的精确目标、架构边界与成功标准是什么？

## 决议 (Resolution)

目标是将 `QgisDisplayManager` 建设为更加内聚的视图呈现管理器：
1. **活动视图管理**：管理者自带活动视图指针/ID 追踪，不必强依赖 UI 主窗口手动传递 `viewId`。
2. **自动资产呈现**：直接订阅 `DataManager::assetAdded` 信号，在收到带 `autoLoad` 或开启全局自动显示策略的资产时，自动加载至当前活动视图。
3. **隔离解耦**：彻底消除桌面主窗口中分散的 UI 适配与自动呈现信号转送代码。
