# Georeferencer 双窗口重设计（Image 2 Image / Image 2 Map）

**日期:** 2026-07-20  
**状态:** 设计已确认（brainstorming）  
**前置:** Phase 11.4 / 11.5 Georeferencer（`docs/superpowers/specs/2026-06-02-georeferencer-design.md`、`2026-06-03-georeferencer-v15-design.md`）  
**实现路径:** 方案 A — 共享底座 + 两个壳  

---

## 1. 目标

将单一「进门即双画布 + 工具栏三模式切换」的 Georeferencer 改为：

1. **两个独立工作窗口**，语义清晰、布局固定  
2. 主菜单 **Image Registration** 下分项进入  
3. **RPC 物理模型**仅作为 Image→Map 的变换方法，不再是并列「第三模式」进门项  

### 1.1 非目标（本阶段）

- 同模式多实例（每个模式最多一个窗口）  
- Image→Map 内嵌可编辑主图或完整图层树编辑  
- 将 I2I 参考影像自动写入主工程图层树（仅支持「加载结果到主图」）  
- 分类模块任务列表与配准任务强制统一（配准继续用 `QgsMessageLog` / 既有进度 UI）  

---

## 2. 产品决定（已锁定）

| 项 | 决定 |
|----|------|
| 形态 | 两个 `QMainWindow`：I2I 与 I2M |
| 菜单 | `Image Registration` → `Image 2 Image` / `Image 2 Map` |
| 并发 | 两窗口可同时打开；各模式单例（再点菜单 raise） |
| I2I 布局 | 水平双栅格画布 SRC \| REF |
| I2M 布局 | 单窗口上下：SRC + **主工程图层镜像**地图预览 |
| RPC | 仅 I2M 参数面板中的变换方法 |
| SIFT | 仅 I2I（需要双影像） |
| 实现 | 共享 GeorefCore + 两个壳，禁止整文件复制两份主窗口 |

---

## 3. 入口与生命周期

### 3.1 主应用菜单

```
Image Registration
├─ Image 2 Image   → show/raise ImageToImageWindow 单例
└─ Image 2 Map     → show/raise ImageToMapWindow 单例
```

- 删除（或迁移）原单一 `Georeferencer` 菜单项，避免与双入口混淆。  
- 文案可用英文菜单名（与用户指定一致）；窗口标题建议：  
  - `Image Registration · Image 2 Image`  
  - `Image Registration · Image 2 Map`  

### 3.2 单例与关闭

- `QgisDesktopWindow`（或等价 app shell）持有两个 `QPointer`/`QPointer` 友好裸指针 + 惰性创建。  
- 重复菜单动作：若实例存在则 `show()` + `raise()` + `activateWindow()`。  
- 关闭：若 GCP/会话 dirty，提示保存 `.points`（沿用现有 session 行为）；关闭一个不影响另一个。  
- 两窗口状态完全独立（GCP 列表、路径、dirty、transform 方法选择）。  

---

## 4. Image 2 Image 窗口

### 4.1 布局

| 区域 | 内容 |
|------|------|
| 中央 | 水平 `QSplitter`：SRC 画布 \| REF 画布 |
| 底部 | GCP 表 + RMS 散点（现有组件） |
| 右侧 dock | 参数面板（变换方法、输出、Apply 等） |
| 工具栏 | Add/Move/Delete GCP、Sync zoom、Load/Export points、SIFT、Preview/Apply |

### 4.2 行为

- **File:** Open Source Raster / Open Reference Raster。图层进入窗口 **私有** `QgsMapLayerStore`。  
- **GCP:** 源点在 SRC 点选，目标点在 REF 点选。主路径不是 MapCoords 手输对话框（可保留为高级/调试，默认不强调）。  
- **Sync zoom:** 可选，复用 `RsTwinCanvasSyncController`。  
- **SIFT:** 启用；自动匹配 SRC↔REF 并写入 GCP 列表。  
- **变换方法:** 线性 / Helmert / 多项式 / 投影等 **GCP 拟合类**；**不提供 RPC**。  
- **Apply:** warp 源影像至与参考对齐的输出；可选加载结果到主工程。  

### 4.3 不包含

- 模式切换 toggle（Image→Map / RPC）  
- DEM / Z-offset 段（RPC 专用）  

---

## 5. Image 2 Map 窗口

### 5.1 布局

| 区域 | 内容 |
|------|------|
| 中央上 | SRC 源影像画布 |
| 中央下 | Map 预览画布（镜像主工程可见图层） |
| 底部 | GCP 表 + RMS |
| 右侧 dock | 目标 CRS、变换方法（**含 RPC**）、DEM/Z-offset（RPC 时）、输出、Apply |

中央为 **垂直** `QSplitter`（SRC / Map）。

### 5.2 行为

- **File:** Open Source Raster（私有 store）。**无** Open Reference 作为第二源影像。  
- **Map 图层:** 打开时及主工程图层增删/可见性变化时，将 **可见图层列表** 同步到 Map 画布 `setLayers`。不获取所有权；关闭 I2M **不**卸载主工程图层。  
- **GCP:** SRC 上点源点；Map 上点目标点（地图坐标）。手输坐标可作为辅助，非主路径。  
- **变换方法:** 含既有 GCP 方法 + **RPC 物理模型**。选中 RPC 时显示 DEM 路径、Z-offset、GCP 精化（对齐现有 `RsGeorefParamsPanel` / `QgsRpcGcpTransformer`）。  
- **SIFT:** 禁用或隐藏（无双影像参考）。  
- **Apply:** 按目标 CRS / RPC 配置 warp；结果可加载到主工程。  

### 5.3 Map 画布 CRS

- Map 预览画布的 **destination CRS** 与用户选择的 **目标 CRS** 一致（便于 GCP 目标点与输出一致）。  
- 若主工程图层 CRS 与目标 CRS 不同，依赖 QGIS 画布即时重投影渲染（与主窗口行为一致）。  
- 「跟随主画布 extent」为 **可选开关，默认关闭**，避免与用户在 I2M 内漫游冲突。  

---

## 6. 共享内核（GeorefCore）

### 6.1 职责边界

**GeorefCore（无 QMainWindow UI）负责：**

- GCP 列表生命周期与 fit / residual 重算  
- `QgsGeorefTransform` 与方法切换（调用方限制 RPC 是否暴露）  
- `RsWarpTask` 启动、取消、进度与结构化日志  
- 会话 dirty / 路径快照（可按模式 key 分文件，如 `Georeferencer/I2I/*` vs `I2M/*`）  
- SIFT 任务 API（仅 I2I 调用）  

**各 Window 负责：**

- 画布数量与 splitter 布局  
- File 菜单与「能否加载 REF」  
- 参数面板可见段（RPC/DEM/SIFT）  
- 与主工程的图层镜像（仅 I2M）  
- 工具栏与 dock 组装  

### 6.2 建议落点（实现时可微调命名）

| 单元 | 说明 |
|------|------|
| `georef_core.h/.cpp` 或重构现有逻辑进 helper | 非 UI 状态机 |
| `qgsgeoref_image_to_image_window.*` | I2I 壳 |
| `qgsgeoref_image_to_map_window.*` | I2M 壳 |
| 现有 `qgsgeoreferencermainwindow.*` | 迁移期可暂作 I2I 基线再删减，或改名为 I2I 并抽出 I2M |

禁止：整文件复制两份 2000+ 行 main window 后分叉维护。

### 6.3 删除/降级

- `RsGeorefModeToggle` 三按钮进门切换：**从两窗口 UI 移除**。  
- 若代码仍引用 Mode 枚举，内部可保留为「能力标志」但用户不可见，或删除并由窗口类型表达。  

---

## 7. 主应用接线

- `main_window_menus.cpp`（或等价）：添加 `Image Registration` 子菜单两项。  
- 槽：`openGeorefImageToImage()` / `openGeorefImageToMap()`。  
- 成员：`m_georefI2I` / `m_georefI2M` 惰性 `new`，parent 为主窗口或 `nullptr` + 显式生命周期管理（与现 Classification 独立窗一致）。  

---

## 8. 数据流（Apply）

```
用户确认源(+REF)与 GCP
    → Core 按当前方法 updateParametersFromGcps
    → RsWarpTask 后台执行
    → 成功：状态栏 + MessageLog JSON；可选 add 结果到 QgsProject
    → 失败/取消：清理 partial 输出（沿用现 warper 行为）
```

I2M + RPC：Apply 前校验源含 RPC 元数据；DEM CRS 警告沿用现有逻辑。

---

## 9. 测试计划（最小）

| 测试 | 断言 |
|------|------|
| 单例 | 两次 open I2I 返回同一窗口指针；I2M 同理 |
| 并发 | I2I 与 I2M 可同时 show |
| I2I UI | 存在双 canvas objectName；无 RPC DEM section；有 Open REF / SIFT |
| I2M UI | 垂直双 canvas；方法列表含 RPC；无 SIFT 或 SIFT disabled |
| Map 镜像 | project 增加可见栅格 → I2M map canvas layers 含该层 |
| 回归 | 现有 GCP/fit/warp/RPC unit 测试在 Core 抽出后仍绿 |

---

## 10. 风险与缓解

| 风险 | 缓解 |
|------|------|
| 拆窗口引入回归 | 先抽 Core + 菜单双入口，I2I 尽量保留现布局；再做 I2M 布局差量 |
| 主工程图层指针悬空 | I2M 只 setLayers 非拥有；layer removed 信号时刷新列表 |
| 用户混淆「REF 影像」与「地图」 | 窗口标题与 canvas 标签明确 SRC / REF vs SRC / Map |
| 会话 settings 键冲突 | 分前缀 `Georeferencer/I2I/` 与 `Georeferencer/I2M/` |

---

## 11. 实现分期建议

1. **P0:** 菜单双入口 + 单例；I2I = 现窗口去 toggle（固定双栅格）  
2. **P1:** 新建 I2M 壳：垂直 SRC+Map、图层镜像、RPC 方法入口  
3. **P2:** 抽 GeorefCore、去重、补测试与 lab 文档更新  

---

## 12. 成功标准

- 用户从 **Image Registration** 能打开两个语义不同的窗口，布局符合 §4–§5。  
- I2I 可完成双影像 GCP +（可选）SIFT + warp。  
- I2M 可完成源影像对主工程地图 GCP +（可选）RPC + warp。  
- 无第三「RPC 模式」进门按钮；RPC 仅出现在 I2M 方法列表。  
- 既有单元测试与关键 warp/RPC 路径不回退。  
