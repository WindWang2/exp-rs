# R35-T01 PATH_OWNERSHIP

## 本轨写集（首切片）

- src/processing/algorithms/satellite_products.cpp（WP01，#2142 后回归 T01）
- src/processing/algorithms/image_enhancement.cpp
- src/processing/algorithms/change_detection.cpp
- src/processing/algorithms/spectral_unmixing.{h,cpp}
- tests/test_r35_t01_science_validity.cpp（新）
- tests/test_r35_t01_landsat_affine.cpp（新）
- tests/CMakeLists.txt（仅末尾最小追加，不动他轨块）
- .gitignore（仅追加本轨 planning 白名单）
- .planning/r35-t01-science/*.md

## 排他边界（本切片不触碰）

- src/operators/rs/rs_spectral_index_operator.cpp、rs_change_streaming.{h,cpp}（T04 整文件；rs_change_primitives.cpp 未改）
- 模型 runtime/GPU（T02）、data/geospatial（T03）、verify/workflow/教学后端（T04）、Agent/Python/Mission（T05）、UI/main_window/全局 TS/staging（T06）
- 其他轨 .planning/**（只读）

## 执行时保留区

- 快照时 0 open PR；创建分支时复查无新增。提交 PR 前按协议再次刷新，若出现新 PR 则读取 files/head 建立新保留区。

## 保留区（执行时新增 PR，2026-11-19 复查）

- **PR #2149**（T06 i18n，zcode/r35-t06-desktop-workflows-20261118T150535Z → master）：resources/translations/sicnu_zh_CN.ts、.gitignore、.planning/r35-t06-desktop-workflows/**。与本轨写集零重叠（.gitignore 双方均为各自段落 append）；不动其文件，合并时如遇 .gitignore 上下文冲突按段理解解决。
