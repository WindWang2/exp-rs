# Phase 10B.0: OTB + ITK Vendored Infrastructure 设计

**日期:** 2026-06-04
**Phase:** 10B.0
**状态:** 设计完成，待写实现计划
**后续:** Phase 10B（OBIA 业务）依赖本 phase

## 1. 目标

把 OTB 算法库 + ITK 源码全部 vendor 进项目，让用户 `git clone` 后 `cmake -DSICNU_BUILD_OTB=ON .. && make` 一步直接得到带 OTB MeanShift 等算法的可执行体。无需用户单独 `apt install libitk-dev` / `apt install libotb-dev`。

不在范围（Phase 10B 业务）：
- OTB segmenter / classifier 的 C++ wrapper（`rs_segmenter_otb_meanshift.cpp` 等）
- OBIA UI / 模式 toggle
- 段级特征 / GLCM

## 2. 决策汇总

| 决策点 | 选择 | 备注 |
|---|---|---|
| OTB 版本 | v10.0.0（仓库已含） | 来自 `qgis_ref/OTB/CMakeLists.txt` |
| ITK 版本 | v5.4.0（git subtree） | OTB 10 兼容 |
| 模块策略 | 全 vendor，CMake opt-in 编译子集 | 升级时 `git subtree pull` 一次过 |
| GUI / Wrapping | 全关 | Qt / Python / Monteverdi / Mapla 都禁用 |
| LiDAR 路径 | 走项目已有 PDAL（`external/pdal_wrench/`） | 不引 OTB-LiDAR 模块 |
| 默认构建 | `SICNU_BUILD_OTB=OFF` | 开发者按需打开；CI 默认 ON |
| Build target 形态 | 静态库（OTB 已不支持插件，纯静态 OK） | 与 qgis_core 等保持一致 |
| 跨平台优先级 | Linux 首 | Windows / macOS Phase 10B.1 再补 |

## 3. 目录结构

```
exp-rs/
├── qgis_ref/          (1.3 GB QGIS 源码，不动)
├── otb_ref/           (97 MB OTB 10 源码 — 从 qgis_ref/OTB git mv 过来)
│   ├── CMakeLists.txt
│   ├── Modules/
│   │   ├── Core/
│   │   ├── Filtering/
│   │   ├── IO/
│   │   ├── Segmentation/
│   │   ├── Learning/
│   │   ├── FeaturesExtraction/
│   │   ├── Hyperspectral/      (可选模块，CMake 关掉)
│   │   ├── SAR/                (可选)
│   │   ├── StereoProcessing/   (可选)
│   │   ├── Remote/             (可选)
│   │   └── ThirdParty/         (OTB 自带的依赖 wrapper)
│   └── COPYING (Apache 2.0)
├── itk_ref/           (~300 MB ITK 5.4 — 新增 git subtree)
│   ├── CMakeLists.txt
│   ├── Modules/
│   │   ├── Core/
│   │   ├── Filtering/
│   │   ├── IO/
│   │   └── (其余可选)
│   └── LICENSE
├── external/
│   └── pdal_wrench/   (LiDAR 处理，未来 Phase 11.x 用)
├── src/
└── CMakeLists.txt
```

**关键操作：**

- 子任务 10B.0.1：`git mv qgis_ref/OTB otb_ref`（让 OTB 与 QGIS 物理解耦）
- 子任务 10B.0.2：`git subtree add --prefix=itk_ref ITK v5.4.0 --squash`（一次性引入 ITK）

## 4. CMake 集成

顶层 `CMakeLists.txt` 新增：

```cmake
option(SICNU_BUILD_OTB "Build vendored OTB + ITK algorithm libraries" OFF)

if (SICNU_BUILD_OTB)
    # ─── ITK: 仅算法核心模块 ──────────────────────────────────
    set(ITK_BUILD_DEFAULT_MODULES OFF CACHE BOOL "" FORCE)
    foreach(m
        ITKCommon
        ITKImageBase
        ITKImageFilterBase
        ITKImageGrid
        ITKImageStatistics
        ITKImageIntensity
        ITKImageFunction
        ITKLabelMap
        ITKConnectedComponents
        ITKMathematicalMorphology
        ITKIOImageBase
        ITKIOTIFF
        ITKIOGDAL
    )
        set(Module_${m} ON CACHE BOOL "" FORCE)
    endforeach()
    set(ITK_WRAP_PYTHON OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    add_subdirectory(itk_ref)

    # ─── OTB: 仅算法模块 ──────────────────────────────────────
    set(OTB_BUILD_DEFAULT_MODULES OFF CACHE BOOL "" FORCE)
    foreach(m
        OTBCommon
        OTBImageBase
        OTBImageManipulation
        OTBObjectList
        OTBImageIO
        OTBStreaming
        OTBStatistics
        OTBLabelling
        OTBMeanShift
        OTBSegmentation
        OTBLearning
        OTBSupervised
        OTBFeaturesExtraction
    )
        set(Module_${m} ON CACHE BOOL "" FORCE)
    endforeach()
    set(OTB_USE_QT OFF CACHE BOOL "" FORCE)
    set(OTB_WRAP_PYTHON OFF CACHE BOOL "" FORCE)
    set(OTB_BUILD_MODULE_AS_STANDALONE OFF CACHE BOOL "" FORCE)
    set(OTB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory(otb_ref)

    set(SICNU_HAS_OTB TRUE)
endif()
```

子模块 (例如 `qgis_analysis`) 通过 `if (SICNU_HAS_OTB)` 守卫条件链接 OTB 静态库：

```cmake
if (SICNU_HAS_OTB)
    target_link_libraries(qgis_analysis PUBLIC
        OTBCommon OTBImageBase OTBMeanShift)
    target_compile_definitions(qgis_analysis PUBLIC SICNU_HAS_OTB=1)
endif()
```

## 5. 模块依赖图

### 5.1 ITK 模块（~12 个）

| 模块 | 用途 | 谁需要 |
|---|---|---|
| ITKCommon | `itk::Object` / `SmartPointer` / `Region` / `Index` | 所有 OTB filter |
| ITKImageBase | `itk::Image` / `VectorImage` 模板 | `otb::Image` 是 typedef |
| ITKImageFilterBase | `ImageToImageFilter` 基类 | 所有 OTB segmentation filter |
| ITKImageGrid | resample / pad / crop | OTB streaming pipeline |
| ITKImageStatistics | 像素统计 (mean / std) | OTB Learning + 部分 segmentation |
| ITKImageIntensity | 像素值变换 | OTB image manipulation |
| ITKImageFunction | Interpolator / NeighborhoodOperator | MeanShift 邻域算 |
| ITKLabelMap | label 图数据结构 | Segmentation 输出 |
| ITKConnectedComponents | 连通域 | MeanShift 后处理 |
| ITKMathematicalMorphology | erode / dilate / opening / closing | OTB morphological filters |
| ITKIOImageBase | image reader / writer 框架 | OTB 透传写中间结果 |
| ITKIOGDAL | GDAL 集成（OTB 主 IO 路径） | OTB 读写 GeoTIFF |

ITK 其他 ~38 个模块全部关闭（DICOM / Mesh / Registration / VTK Bridge / Python wrap 等）。

**预期补丁：** OTB 模块编译时若报 ITK 缺失，按需补 5-8 个进 ON 名单（10B.0.3 子任务里迭代）。

### 5.2 OTB 模块（~13 个）

| 模块 | 用途 |
|---|---|
| OTBCommon | Logger / TypeManager / 各种 macros |
| OTBImageBase | `otb::Image` / `VectorImage` / `MultiResolution` |
| OTBObjectList | 模板化的 list 容器（filter 内部用） |
| OTBImageIO | 读写 GeoTIFF 等（透传 ITK + GDAL） |
| OTBStreaming | 大栅格分块处理 pipeline |
| OTBImageManipulation | crop / pad / extract / multichannel |
| OTBStatistics | 像素 / 段 / 直方图统计 |
| OTBLabelling | label 图操作 + 邻接图 |
| **OTBMeanShift** | MeanShift smoothing + segmentation filter（主 angle） |
| OTBSegmentation | Watershed / ConnectedComponents 等其他分割 filter |
| OTBLearning | 分类器抽象（与 Phase 10A 互通空间） |
| OTBSupervised | 监督分类专用工具 |
| OTBFeaturesExtraction | GLCM 纹理 / SIFT / Morphological（与 Phase 10B 业务对接） |

OTB 其他 ~25 个模块全部关闭（Hyperspectral / SAR / Stereo / Remote / Markov / Bias correction 等）。

## 6. 子任务划分（6 实现 + 1 planning）

| # | 子任务 | 关键产出 |
|---|---|---|
| **10B.0.1** OTB 重组 | `git mv qgis_ref/OTB otb_ref`；修复内部 CMake 路径引用 |
| **10B.0.2** ITK 5.4 git subtree | `git subtree add` 命令 + `scripts/update_itk.sh` 升级脚本 |
| **10B.0.3** ITK 子集 CMake 配置 | 顶层 SICNU_BUILD_OTB option；ITK ~12 模块 ON；首次 `make ITKCommon` 绿 |
| **10B.0.4** OTB 子集 CMake 配置 | OTB ~13 模块 ON；关 Qt/Python/Wrapping；首次 `make OTBMeanShift` 绿 |
| **10B.0.5** sanity 测试 + SICNU_HAS_OTB | `tests/test_otb_smoke.cpp` 3 TEST_CASE；`#ifdef SICNU_HAS_OTB` 守卫 |
| **10B.0.6** CI / 文档 / .gitattributes | `scripts/build_with_otb.sh`；CONTRIBUTING.md vendored 说明；.gitattributes 标 *.hxx text + linguist-vendored |
| **10B.0.7** Planning files | task_plan / progress / findings 同步 |

## 7. 命令细节

### 7.1 子任务 10B.0.1：OTB 重组

```bash
cd /home/kevin/projects/exp-rs
git mv qgis_ref/OTB otb_ref
grep -rn "qgis_ref/OTB\|qgis_ref/.*/OTB" otb_ref/ 2>/dev/null
# 预期 0 处 (OTB 内部不引外部路径)
```

### 7.2 子任务 10B.0.2：ITK subtree

```bash
cd /home/kevin/projects/exp-rs

git remote add -f itk-upstream https://github.com/InsightSoftwareConsortium/ITK.git
git subtree add --prefix=itk_ref itk-upstream v5.4.0 --squash

cat > scripts/update_itk.sh <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
TAG="${1:-v5.4.0}"
git fetch itk-upstream "$TAG"
git subtree pull --prefix=itk_ref itk-upstream "$TAG" --squash
EOF
chmod +x scripts/update_itk.sh
```

## 8. sanity 测试

`tests/test_otb_smoke.cpp`（3 TEST_CASE）：

1. **OTB link + Logger** — 实例化 `otb::Logger::New()`，断言 `IsNotNull()`
2. **otb::Image 实例化** — 创建 32×32 float image，分配缓冲，写零，验证 size
3. **MeanShift filter header 可编译** — 实例化 `otb::MeanShiftSegmentationFilter` 模板特化

构建 ON 时 3/3 PASS；OFF 时全部 SKIP（通过 `#ifndef SICNU_HAS_OTB SKIP`）。

CMake 注册条件：

```cmake
if (SICNU_HAS_OTB)
    add_executable(test_otb_smoke test_otb_smoke.cpp)
    target_link_libraries(test_otb_smoke PRIVATE
        OTBCommon OTBImageBase OTBMeanShift
        Catch2::Catch2WithMain)
    sicnu_discover_tests(test_otb_smoke)
endif()
```

## 9. .gitattributes 增量

```
itk_ref/Modules/ThirdParty/** linguist-vendored
otb_ref/Modules/** linguist-vendored
itk_ref/** -text
otb_ref/** -text
```

防止 git 把 OTB / ITK 计入语言统计 + 不做行尾转换（避免大量误改）。

## 10. 文档增量

CONTRIBUTING.md / README.md 加段：

```markdown
## Vendored libraries

- `qgis_ref/` — QGIS source (subset)
- `otb_ref/` — Orfeo Toolbox v10 source (algorithm modules only)
- `itk_ref/` — ITK 5.4 source (image processing modules only, via git subtree)
- `external/pdal_wrench/` — PDAL (LiDAR processing)

OTB + ITK are vendored to remove the user-install dependency.
Build with `-DSICNU_BUILD_OTB=ON` to enable OTB-backed segmentation
(adds ~30–45 minutes to first build).

Default development mode keeps `SICNU_BUILD_OTB=OFF`. CI builds with
ON to ensure the path stays green.

### Upgrading ITK

```bash
./scripts/update_itk.sh v5.4.1
```

### Upgrading OTB

OTB lives in `otb_ref/` as a vanilla copy of the OTB source tree. Manual sync
when needed; no script (OTB upgrades are rare and require module-list audit).
```

## 11. 风险与缓解

| # | 风险 | 概率 | 影响 | 缓解 |
|---|---|---|---|---|
| 1 | OTB 10 是 pre-release，ITK 5.4 不兼容 | 中 | 高 | 10B.0.3 试编译，若失败降到 ITK 5.5-master 或 OTB 9.1 release |
| 2 | OTB 模块依赖暴露更多 ITK 模块 | 高 | 低 | CMake 报缺失时回填 ON 列表；预期最多再加 5–8 个 |
| 3 | 静态库（BUILD_SHARED_LIBS=OFF）下若有插件机制失败 | 低 | 低 | OTB 9+ 已无插件机制；纯静态 OK |
| 4 | 默认 OFF 让 OTB segmenter 在大部分开发者机器上不可用 | 已知 | 中 | 文档明示 + CI 跑 ON；Phase 10B 业务 `#ifdef SICNU_HAS_OTB` 灰显 |
| 5 | git 仓库 +300MB 让 clone 变慢 | 中 | 低 | 文档建议 `git clone --depth 1`；未来 LFS-track itk_ref/Modules/ThirdParty |
| 6 | OTB 10 的 LiDAR 模块未来加，依赖 PDAL 集成 | 低 | 低 | 走项目已有 PDAL（`external/pdal_wrench/`），不引 OTB-LiDAR 模块 |
| 7 | CI 全套构建 +45 分钟 | 中 | 中 | ccache 缓存；增量构建只跑实际改的；首次成功后增量 < 30s |

## 12. Done When

- `cmake -DSICNU_BUILD_OTB=OFF ..` 行为不变（开发者默认体验保留）
- `cmake -DSICNU_BUILD_OTB=ON ..` 首次成功 build（OTB+ITK 静态库都产出）
- `tests/test_otb_smoke.cpp` 在 ON 时 3/3 PASS，OFF 时 3 SKIP
- `SICNU_HAS_OTB=1` 编译宏正确传到 `qgis_analysis`
- `.gitattributes` + CONTRIBUTING.md 文档到位
- 6 commit（每子任务一个）+ 1 planning commit
- 总测试数 293 → 296（+ 3 sanity TEST_CASE）

## 13. 已知未决

- **Windows / macOS 构建** — Linux 首；其他平台 Phase 10B.1
- **OTB 模块清单可能要补** — 编译时迭代加，文档以 10B.0.4 commit 内最终 CMakeLists.txt 为准
- **首次构建文档化耗时** — 实测取决于机器（笔记本 ~45 分钟，工作站 ~25 分钟），写在 CONTRIBUTING.md
- **CI（GitHub Actions / GitLab CI）配置** — 不在 Phase 10B.0 范围；先有命令脚本 `build_with_otb.sh`，CI 接入留 Phase 10B.1
