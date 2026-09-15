# PERFORMANCE — cn-eo-product-physics-11

## 资源模型（硬约束）

- Build：Ninja `-j2`（`CMAKE_BUILD_PARALLEL_LEVEL=2`）；RSS>70% 或负载过高 → `-j1`。禁止 `-j$(nproc)`。
- Test：`ctest -j1`、`QT_QPA_PLATFORM=offscreen`。
- 宿主：Windows（Git Bash）。load average 不可测（Git Bash 无 uptime 语义）→ 按协议记录一次 not-executed，恒定 `-j2`。RSS 用 `tasklist` 观测。

## 逻辑规模证据原则

- 不以 wall-clock 为 correctness gate；用内存上限、操作数上限、逻辑规模不变量。
- 高光谱（GF-5 330-band）：band axis 验证为 registry 数据检查（O(bands)）；stack 读为 GDAL 流式（逐 band window），峰值内存由既有 stackToGeoTiff 契约约束——测试用小尺寸 fixture（如 32×32×N band）+ 波段数真实性独立断言，不造 330×大影像。
- ImportPlan dry-run：O(目录条目)（bounded 512 上限已有），checksum 流式读取（64 KiB 块）。

## 实测记录（滚动补充）

| Phase | 命令 | -j | 时长 | RSS 峰值 | 备注 |
|---|---|---|---|---|---|

| Phase | 命令 | -j | 时长 | RSS 峰值 | 备注 |
|---|---|---|---|---|---|
| 1 configure#1 | cmake -S -B -G Ninja (vcpkg) | 2 | ~6 min | — | 失败:Catch2 网络克隆 |
| 1 configure#2 | +FETCHCONTENT_SOURCE_DIR_CATCH2 | 2 | ~5 min | — | exit 0 |
| 1 冷构建 | ninja sicnu_geospatial 等5目标 | 2 | >60 min | cl.exe ×2 140–430 MB | src/core(qgis_core) 为大头,~900+ obj |
| 1 快反馈 | ninja test_sensor_schema | 2 | ~2 min | — | 修3处编译/环境问题后全绿 |
