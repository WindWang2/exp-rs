# Georeferencer v1.6 — Production Usability Polish 设计

**日期:** 2026-07-15  
**Phase:** 11.6（几何校正总路线图阶段 1）  
**状态:** 设计已确认，待写实现计划  
**前置:** Phase 11.4 / 11.5 完成  
- `docs/superpowers/specs/2026-06-02-georeferencer-design.md`  
- `docs/superpowers/specs/2026-06-03-georeferencer-v15-design.md`  

## 1. 目标与范围

### 1.1 背景

几何校正（`Raster > Georeferencer`）在 11.4 + 11.5 已具备三模式、变换、SIFT、RPC 精化等能力。生产使用中仍有明确债务：关闭不提示未保存 GCP、设置几乎不持久、REF 标记跨 CRS 漂移、RPC 精化前后 RMS 未接线、Move/Delete 未接线、`contains()` stub、Image→Map 取点未打到主应用地图。

### 1.2 总路线图中的位置

「几何校正全部优化」拆为五阶段；本 spec 仅覆盖 **阶段 1**：

| 阶段 | 主题 | 本 spec |
|------|------|---------|
| 1 | 生产可用性 polish | **是** |
| 2 | 教学 / 实验体验 | 否 |
| 3 | 算法 / 精度 | 否 |
| 4 | 性能 | 否 |
| 5 | 代码结构 / toolbox·Agent 集成 | 否 |

### 1.3 范围内（A + B + C）

| ID | 项 | 说明 |
|----|-----|------|
| P1 | 关闭未保存 GCP | 脏标记 + 保存/不保存/取消 |
| P2 | 工作流设置持久化 | 窗口几何、模式、变换/重采样、路径、同步缩放等 |
| P3 | REF 标记 CRS 重投影 | `destinationPointCrs` → REF canvas CRS |
| P4 | RPC 精化前后 RMS | 双跑 transformer，面板 `setRefinementRms` |
| B | 主地图选点 | Image→Map / RPC：主应用画布；Image→Image：REF 画布 |
| C | 命中 / 拖点 / 删除 | 实现 `contains()`；接线 Move/Delete 工具 |

### 1.4 明确不在范围内

- 拆分 `qgsgeoreferencermainwindow.cpp`（阶段 5）
- 改变换数学、SIFT 算法、RPC 高阶精化（阶段 3）
- 教学向导 / Lab 流程（阶段 2）
- 性能 profiling / warp 加速（阶段 4）
- 主应用地图上的 destination 拖拽（QGIS `mToolMovePointQgis`）；v1.6 仅在 REF 画布拖 destination，主地图只用于 Add 流程取点
- 修改 `.points` 文件格式
- 表格列宽、SIFT 参数等 UI 细节持久化

### 1.5 方案选择

采用 **轻量会话状态 + 工具接线**（方案 2）：

- 新增 `RsGeorefSessionState` 收口脏标记与 settings
- 画布 CRS、命中、Move/Delete、取点画布、RPC 双跑仍在现有类中外科修补
- 不对主窗口做大 refactor

---

## 2. 架构与模块边界

```
┌─────────────────────────────────────────────────────────┐
│  QgsGeoreferencerMainWindow  （编排 only）                │
│  closeEvent / tools / showCoordDialog / recomputeFit     │
└────────────┬──────────────────────────┬─────────────────┘
             │                          │
             ▼                          ▼
┌────────────────────────┐   ┌────────────────────────────┐
│ RsGeorefSessionState   │   │ 现有组件（改动，不换职责）   │
│ · dirty / last .points │   │ QgsGeorefDataPoint::contains│
│ · settings R/W         │   │ updateMarkers + CRS xform  │
│ · 不持有 GCP 所有权     │   │ Move/Delete tool 接线      │
└────────────────────────┘   │ MapCoords 目标画布选择      │
                             │ recomputeFit 双跑 RPC       │
                             └────────────────────────────┘
```

### 2.1 新文件

| 文件 | 职责 |
|------|------|
| `src/app/georeferencer/rs_georef_session_state.h` | 会话状态 API |
| `src/app/georeferencer/rs_georef_session_state.cpp` | 脏标记 + `QgsSettings` 读写 |

### 2.2 依赖方向

- `RsGeorefSessionState` → 仅 Qt / `QgsSettings`（或项目已有 settings 封装）；**不**依赖 canvas / GCP list UI
- Main window → SessionState + 现有 GCP / tools
- CRS 变换复用 `QgsGcpPoint::transformedDestinationPoint()` + `QgsProject::instance()->transformContext()`

### 2.3 修改的现有文件

| 路径 | 变更 |
|------|------|
| `qgsgeoreferencermainwindow.{h,cpp}` | SessionState、closeEvent、tools、pickCanvas、recomputeFit 双跑、dirty 接线 |
| `qgsgeorefdatapoint.{h,cpp}` | `contains()` 真实现；`updateMarkers` CRS 重投影 |
| `rs_georef_params_panel.{h,cpp}` | `clearRefinementRms()`；供 session 读写的 getter/setter（若尚缺） |
| `src/app/georeferencer/CMakeLists.txt` | 新源文件 + 测试目标 |
| 测试 | 见 §6 |

**不改：** `qgsimagewarper`、SIFT 模块、analysis 多项式 transformer、`.points` v2 格式。

---

## 3. 会话状态与关闭流程（P1 + P2）

### 3.1 `RsGeorefSessionState` API

```cpp
class RsGeorefSessionState
{
  public:
    bool isDirty() const;
    void markDirty();
    void clearDirty();

    QString lastPointsPath() const;
    void setLastPointsPath( const QString &path );

    void saveWindow( QWidget *w );
    void restoreWindow( QWidget *w );

    /// Snapshot of workflow fields read from / written to the panel & window.
    struct WorkflowSnapshot
    {
      int mode = 0;                 // RsGeorefModeToggle::Mode as int
      int transformMethod = 0;
      int resamplingMethod = 0;
      QString lastSourcePath;
      QString lastRefPath;
      QString lastOutputPath;
      QString lastDemPath;
      QString lastPointsPath;
      bool syncZoom = true;
      // lastDestCrs remains under Georeferencer/lastDestCrs (existing key)
    };

    void saveWorkflow( const WorkflowSnapshot &s );
    WorkflowSnapshot restoreWorkflow() const;
};
```

Settings 键前缀：`Georeferencer/`（与已有 `lastDestCrs` 一致）。

### 3.2 脏标记语义

- **Dirty：** 自上次成功 `loadGcps` / `saveGcps` 之后，GCP 列表有实质变更（增删、坐标、启用、类型）
- **实现约定：** 在 load/save 之外，对 `QgsGCPList::changed` 调用 `markDirty()`；load/save **成功**路径在列表更新后 `clearDirty()`，并更新 `lastPointsPath`
- **不算 dirty：** 仅改变换方法、CRS、输出路径、模式、DEM 等（属 settings，不是未保存 GCP）

| 情况 | 行为 |
|------|------|
| 无 GCP 且 never dirty | 直接关闭 |
| dirty | 对话框：保存 / 不保存 / 取消 |
| 选「保存」 | 若有 `lastPointsPath` 直接写；否则弹出 `savePoints()` 文件对话框；**失败则不关闭** |
| 选「不保存」 | 关闭 |
| 选「取消」 | `event->ignore()` |
| warp 进行中关窗 | 先确认「校正任务仍在运行，仍要关闭？」；确认后关闭（不强制 kill task；可写 log） |

关闭前（真正 `accept` 时）调用 `saveWindow` + `saveWorkflow`。

### 3.3 设置键

| Key | 内容 |
|-----|------|
| `geometry` / `windowState` | 窗口几何与 dock 状态 |
| `mode` | ImageToMap / ImageToImage / RpcPhysical |
| `transformMethod` / `resamplingMethod` | 枚举 int |
| `lastSourcePath` / `lastRefPath` / `lastOutputPath` / `lastDemPath` | 路径 |
| `lastPointsPath` | 上次 `.points` 路径 |
| `syncZoom` | 双画布同步 |
| `lastDestCrs` | **已有**，保留语义与读写位置（params panel 现有逻辑可保留或迁入 session，避免双写冲突：v1.6 以 session 统一写出、panel 构造时仍可读同一 key） |

**时机：**

- **restore：** 主窗口构造末尾（panel / mode / actions 已创建后）
- **save：** `closeEvent` 在真正关闭前；用户成功选择路径时**即时**写入对应 path key（推荐）

**不持久化：** GCP 坐标（靠 `.points`）、SIFT 参数、表格列宽。

---

## 4. 画布 CRS、主地图选点、命中/拖点/删除（P3 + B + C）

### 4.1 REF 标记 CRS 重投影（P3）

在 `QgsGeorefDataPoint::updateMarkers()`：

- **SRC item：** `setWorldPos(sourcePoint)`（像素空间，不变）
- **REF item：**  
  `setWorldPos(transformedDestinationPoint(refCanvas->mapSettings().destinationCrs(), transformContext))`

变换失败：沿用 `QgsGcpPoint` 已有 catch（回退原坐标）+ debug log。  
模式切换 / dest CRS 变更后现有 `updateMarkers()` 循环保持。

### 4.2 取点目标画布（B）

```cpp
QgsMapCanvas *QgsGeoreferencerMainWindow::pickCanvas() const
{
  switch ( currentMode() )
  {
    case RsGeorefModeToggle::ImageToImage:
      return mRefCanvas;
    case RsGeorefModeToggle::ImageToMap:
    case RsGeorefModeToggle::RpcPhysical:
    default:
      return mainAppCanvas(); // from QgisInterface / injected pointer; null → mRefCanvas
  }
}
```

`showCoordDialog`：`new QgsMapCoordsDialog(pickCanvas(), ...)`。

- EmitPoint 临时 map tool 装在**目标画布**上；松开后恢复该画布原 tool
- 主画布指针为空：回退 `mRefCanvas`，状态栏提示一次
- 按钮文案：v1.6 改为中文「从地图取点」（替换现有 `"From Map Canvas"`）；不引入完整 i18n 资源大改

### 4.3 命中测试（C）

`QgsGeorefDataPoint::contains(p, type, distance)` 对齐 QGIS 上游：

1. `searchRadiusMM = QgsMapTool::searchRadiusMM()`
2. 像素半径 = mm × (logicalDpiX / 25.4)
3. Source：click 与 **SRC canvas item** 的 canvas 坐标距离
4. Destination：与 **REF canvas item**（item 位置已是投影后坐标）
5. 在半径内返回 true，并写出 `distance`；`findPoint` 取最近

### 4.4 Move / Delete 工具接线（C）

| 工具 | 画布 | 作用 |
|------|------|------|
| `mToolMoveSrc` | SRC | 拖 source 像素点 |
| `mToolMoveDst` | REF | 拖 destination 地图点 |
| `mToolDeleteSrc` | SRC | 点选删除 |
| `mToolDeleteDst` | REF | 点选删除（与 SRC 共用 `deletePoint` / `findPoint` 槽） |

工具栏：**移动 GCP**、**删除 GCP** 与 **添加 GCP** 互斥（`QActionGroup`）。

**Move 流程：**

1. `pointBeginMove` → `findPoint` → 记录 `mMoving` + `setStartPoint`
2. `pointMoving` → 临时改坐标 + `updateMarkers`（残差可 throttle 或 end 时再算）
3. `pointEndMove` → 写回 `QgsGcpPoint` → list `changed` → `recomputeFit` + `markDirty`
4. `pointCancelMove` / Esc → 恢复起点

**Delete：** `findPoint` → `removePointAt` → dirty。

**Hover（推荐）：** delete/move 时 `setHovered`；tool deactivate 时清除。

**不做：** 主应用地图上的 Move tool。

---

## 5. RPC 精化前后 RMS（P4）

面板已有 `setRefinementRms(before, after)`；`recomputeFit()` 须接线。

当 `method == RpcPhysical` 且 `enabledCount >= 3`：

1. **Before：** transformer A，`setRpcOptions(dem, z, useRefine=false)` → fit → 用与表列相同的 residual / RMS 算法 → `rmsBefore`
2. **After（工作变换）：** transformer B，`useRefine=true` → fit → 写 residual 到 GCP + scatter + 状态栏 → `rmsAfter`；**`mTransform` 保留 B**（Apply 使用精化后模型）
3. `mParamsPanel->setRefinementRms(rmsBefore, rmsAfter)`（after < before 时 after 标签绿色，已有逻辑）

非 RPC 或 GCP < 3：调用 `clearRefinementRms()`（新建），标签显示 `—`。

**单位：** 与表列 residual RMS 同一计算路径，保证 before/after 可比。文案可继续使用现有「精化前/后 RMS: x.xxx px」；若实际单位是地图单位，脚注以「与表列 RMS 同单位」为准，本阶段不改单位体系。

**开销：** RPC 下每次 fit 多一次 GDAL RPC transformer 创建；可接受。缓存留给阶段 4。

**before fit 失败、after 成功：** before 显示 `—`，after 正常。

---

## 6. 错误处理

| 场景 | 行为 |
|------|------|
| CRS 变换失败 | 标记用原坐标；不崩溃；debug log |
| 主画布指针空 | `pickCanvas` 回退 `mRefCanvas`；状态栏提示一次 |
| 命中无点 | Move/Delete 无操作，不报错 |
| Save 失败后仍关窗 | 不关闭 |
| 双跑 RPC 中 before 失败 | after 仍尝试；before 为 `—` |

日志：dirty save/discard、pick canvas、refinement RMS 可用 `SICNU_LOG_*`（tag `Georeferencing`）。**不强制**新 JSON event schema。

---

## 7. 测试矩阵

| 测试 | 断言 |
|------|------|
| `test_georef_session_state` | markDirty/clearDirty；workflow keys round-trip；window geometry 可选 |
| `test_georef_window` 扩展 | dirty 钩子 / clearDirty after save（可测 API） |
| `test_gcp_canvas_crs` | 跨 CRS 后 REF marker 位置接近投影坐标 |
| `test_gcp_contains` | 半径内 true、外 false、最近邻 |
| `test_pick_canvas_mode` | 模式 → canvas 映射（纯函数或 test hook） |
| move/delete 烟雾 | 添加 → 移动 → 删除后点数正确（直接调 slot 可 headless） |
| RPC refine RMS | 已知 bias：`rmsAfter ≤ rmsBefore`（容差）；`useRefine=false` 时 before≈after |
| 回归 | 既有 georef / GCP / RPC / SIFT / warper Catch2 全绿 |

---

## 8. 建议实现顺序

1. `RsGeorefSessionState` + dirty/settings 单测  
2. `closeEvent` + `markDirty` 接线  
3. `contains` + Move/Delete 接线  
4. CRS `updateMarkers`  
5. `pickCanvas` + `showCoordDialog`  
6. RPC 双跑 RMS + `clearRefinementRms`  
7. 集成与全量回归  

---

## 9. 完工标准（Done when）

1. 有未保存 GCP 时关闭 → 保存 / 不保存 / 取消；保存失败不关  
2. 重启后恢复：窗口几何、模式、变换/重采样、路径、sync、`lastDestCrs`  
3. Image→Image 跨 CRS 时 REF 标记与投影位置一致（测试绿）  
4. RPC ≥3 GCP：面板显示精化前/后 RMS 对比（改善时 after 绿色）  
5. Image→Map：从地图取点作用在**主应用地图**  
6. Move/Delete 可在 SRC（及 REF 拖 destination）工作；`contains` 单测绿  
7. 既有 georef 相关 Catch2 全绿，无 OpenCV/RPC 回归  

---

## 10. 风险

| 风险 | 缓解 |
|------|------|
| `QgisInterface` 无 mapCanvas | 构造注入 `QgsMapCanvas *`；空则回退 REF |
| dirty 与 load 信号顺序 | load/save 路径显式 `clearDirty`，避免 `changed` 再 mark |
| RPC 双跑耗时 | 仅 RpcPhysical 且 ≥3 GCP；阶段 4 再优化 |
| Move 过程中 residual 风暴 | end-move 再 `recomputeFit`，moving 仅更新 marker |
| settings 与 panel `lastDestCrs` 双写 | 统一 key；restore 只走一条路径 |

---

## 11. 决策记录（brainstorming）

| 议题 | 决定 |
|------|------|
| 总优化拆分 | 五阶段；本 spec = 阶段 1 |
| 阶段 1 范围 | A+B+C（P1–P4 + 主地图选点 + 命中/拖点） |
| 主地图选点 | 按模式切换目标画布（Image→Map/RPC→主地图；I2I→REF） |
| 未保存语义 | 相对上次成功 Load/Save 的脏标记 |
| 设置范围 | 工作流常用项（非最小、非最大化） |
| 实现方案 | 轻量 `RsGeorefSessionState` + 现有类外科修补 |
