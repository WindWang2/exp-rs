# R18-D Cross-Track Findings（跨轨发现，不顺手修）

1. **qgis_app_facade.cpp 编辑路径绕过 Edit Lease（W1b，本轨域但需独立 WP）**
   - `QgisApp::toggleEditing/saveEdits`（qgis_app_facade.cpp:181-278）直接 startEditing/commitChanges，不持 DataManager Edit Lease；属性表（qgsattributetabledialog.cpp:930/:706）走此路径。后果：asset 层可被双编辑；commit 后 asset revision 不推进、兄弟 Display Layer 不刷新。
   - 本轨 R18-D 已落 **W1a**（editingStarted/editingStopped 监听 → 状态诚实），但 lease 路由需要 facade→shell 回调 + commitEditLease 同步重定位（层指针失效）危害处理，风险等级高，独立成 WP 再做。
   - 影响轨道：R15-B（数据持久化）的 lease 语义；修复时应与 data 侧确认 `rollbackEdit` 对"acquired-but-never-started" lease 的语义（本轮已核验：等价纯释放，安全）。

2. **produce 链 legend 危害守卫信任声明 layers（C4）**
   - `produce.cpp:102-116` 只查 spec 声明；声明无实体层的 ref（mapspec_compiler.cpp:459-484 放行）+ 无 columns 的自动 legend 可构成已知 legend paint 死循环前置（produce.cpp:82-88 注释）。RED 测试若真调 produceMap 会挂死，需要先把守卫抽成可单测的纯函数——独立 WP。
   - 关联：R16-D T08 已修的 legend 空层集拒绝只覆盖"声明也为空"的形状。

3. **Issue #1467 的 core-fork cluster**：非本轨（src/core/processing），未动。
4. **layout:export 与 cartography:export 的 DPI 下限不一致**（1 vs 72）：C7 换成否定区间式后，(0,1) 的旧放行值（如 0.5）现在按既有错误文案 "between 1 and 1200" 拒绝（文案与行为终于一致；已核对 layout_tools.cpp 默认 300、无调用方/测试依赖 sub-1 dpi）。区间本身（1 vs 72）未动——对齐决策留给 layout owner。
