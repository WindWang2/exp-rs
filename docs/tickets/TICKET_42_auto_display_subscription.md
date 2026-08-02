# Ticket TICKET-42: DataManager::assetAdded 自动显示订阅

- **类型**: `task`
- **状态**: 已关闭 (Closed)
- **父级地图**: [MAP_qgis_display_manager_auto_registration.md](file:///home/kevin/projects/exp-rs/docs/tickets/MAP_qgis_display_manager_auto_registration.md)

## 问题 (Question)

`QgisDisplayManager` 如何订阅 `DataManager` 资产生成信号并自动完成呈现？

## 决议 (Resolution)

1. 在 `QgisDisplayManager` 中支持 `setAutoDisplayOnAssetAdded(bool enabled)` 策略开关。
2. 构造时连接 `DataManager::assetAdded` 信号：若开启自动显示，自动查询资产元数据并调用 `addLayer(activeViewId(), assetId)`。
3. 若当前没有活动视图或添加失败，记录诊断日志并触发警示信号。
