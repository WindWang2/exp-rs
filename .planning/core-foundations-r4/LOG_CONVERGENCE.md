# Unified Log Contract — WP-E convergence list (core-foundations-r4)

统一入口契约:`src/core/sicnu_logging.h`(`SICNU_LOG_INFO/WARN/ERROR/DEBUG` → QgsMessageLog → LogPanel,带时间戳与模块 tag)。
范围:src/core 非 vendor 面。普查命令(2026-09-27 实测,可复跑):
`rg -n "fprintf|qDebug\(|qWarning\(|qCritical\(|std::cerr|std::cout" src/core -g '!*qgs*'`

## 散点清单(逐点处置;38 行 / 8 文件)

| 文件 | 行数 | 处置 | 理由(可复核) |
|---|---|---|---|
| `src/core/plugin_host.cpp` | 10 | **defer-#1334(避让)** | 文件属在途 PR #1334 独占清单(BASELINE §2);本轨道不写。移交清单已随 PR overlap 记录 |
| `src/core/pal/pointset.cpp` | 9 | **豁免(vendor)** | QGIS PAL 上游库(几何库,非 Qt 日志管路);`fprintf(stderr)` 为上游诊断通道,收敛将制造 merge drift |
| `src/core/pal/labelposition.cpp` | 9 | **豁免(vendor)** | 同上 |
| `src/core/pal/priorityqueue.cpp` | 3 | **豁免(vendor)** | 同上 |
| `src/core/pal/feature.cpp` | 3 | **豁免(vendor)** | 同上 |
| `src/core/pal/geomfunction.cpp` | 1 | **豁免(vendor)** | 同上 |
| `src/core/qobjectuniqueptr.h` | 2 | **豁免(vendor)** | QGIS 上游 `QObjectParentUniquePtr`(qWarning ×2,assign-nullptr-parent 诊断),上游类随 upstream 更新 |
| `src/core/providers/gdal/gdal_minmax_element.hpp` | 1 | **豁免(vendor)** | GDAL 交互头(vendor 层) |

## 结论

- **非 vendor、非避让的绕过点 = 0**。src/core 的日志面已收敛于 `SICNU_LOG_*`(轨道 #1283 系已完成的收敛在本轨道复核通过);全部 38 行散点或属 QGIS/GDAL vendor 上游(28 行,书面豁免),或属 #1334 独占文件(10 行,避让移交)。
- **诊断路径 stderr 豁免清单**:PAL 几何库(无 QgsMessageLog 依赖)的 `fprintf(stderr)` 6 文件 25 行 + `gdal_minmax_element.hpp` 1 行——按"诊断路径允许 stderr"条款入册,不改格式、只记录去向。
- 本 WP 为零代码改动交付(审计+豁免清单);依据 prompt:豁免须有可复核理由,vendor 边界与在途 PR 避让即理由,不为改而改。
