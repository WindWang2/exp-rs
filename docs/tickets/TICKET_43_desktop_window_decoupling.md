# Ticket TICKET-43: 主窗口与 ActiveViewHost 代码解耦

- **类型**: `task`
- **状态**: 已关闭 (Closed)
- **父级地图**: [MAP_qgis_display_manager_auto_registration.md](file:///home/kevin/projects/exp-rs/docs/tickets/MAP_qgis_display_manager_auto_registration.md)

## 问题 (Question)

如何简化主窗口中的手动逻辑并通过单元测试验证？

## 决议 (Resolution)

1. 在 `ActiveViewHost` 与 `QgisDesktopWindow` 中利用 `QgisDisplayManager` 提供的 `activeViewId()` 和自动显示响应，简化手动的事件桥接代码。
2. 运行 `test_active_view_host_data_context` 和 `test_qgis_display_manager` 单元测试验证功能完备。
