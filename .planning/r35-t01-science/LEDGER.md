# R35-T01 LEDGER（goal-loop 持久账本）

## 第1轮 | 状态刷新+worktree+差距分析 | 通过 | 2026-11-19

- master=120677e6（fetch 后与快照一致），open PR 0、branches 29。worktree `exp-rs-r35-t01-science-20261118T155456Z`，分支 `zcode/r35-t01-science-20261118T155456Z`。
- `cmake --preset dev-default` exit 0（双锁 guard，-j4）。
- WP01–WP06 静态差距全部确认（BASELINE.md 有行号证据）。未发现活动 PR 排他冲突；T04 整文件（rs_spectral_index_operator.cpp / rs_change_streaming.cpp）未触碰。

## 第2轮 | 实现首切片 WP01–WP06 | 构建验证中

改动（全部在 worktree，未提交）：
1. `src/processing/algorithms/satellite_products.cpp`：Landsat 系数关联正则 `^B(\d+)$` → `^(?:SR_)?B(\d+)$`（WP01）。
2. `src/processing/algorithms/image_enhancement.cpp`：processPcaFile/processMnfFile 在扫描前归一 `numComponents<=0 → bandCount`（WP02/WP03）。
3. `src/processing/algorithms/change_detection.cpp`：madFinalize 与 IR-MAD 迭代内 `!(trace>0)` 早拒绝（WP04，含 NaN trace）。
4. `src/processing/algorithms/spectral_unmixing.cpp`：VCA 整谱 isfinite 预过滤+有效集合运行（WP05）；PPI typed seam 恰好-requested-或-false（WP06a）；FCLS QA 仅有限行、0贡献者→NaN（WP06b）。
5. `src/processing/algorithms/spectral_unmixing.h`：两处合同文档更新。
6. 新测试：`tests/test_r35_t01_science_validity.cpp`、`tests/test_r35_t01_landsat_affine.cpp`；`tests/CMakeLists.txt` 末尾最小追加；`.gitignore` 追加本轨 planning 白名单。

验证计划：构建 6 个目标（2新+4旧）→ ctest 按前缀/目标跑，真实 case 数回填本账本。

## 第3轮 | 构建与验证 | 通过 | 2026-11-19 02:2x (本地)

- configure：`cmake --preset dev-default -DOpenCV_DIR=…/opencv5`，exit 0（双锁，-j4；首次缺 OpenCV_DIR 失败后修正）。
- 构建：`cmake --build build-dev --config Debug --parallel 4 --target test_r35_t01_science_validity test_r35_t01_landsat_affine test_pca test_change_detection test_spectral_unmixing_fcls test_satellite_products` → OUTER_EXIT=0（第三次；前两次分别为测试文件名字歧义与 vector::insert 笔误，均已修复）。
- 中途 27 处 GCC ICE（dwarf2out 段错误，多见于 qgis_core 大单元，重试后全部建成，疑为高负载暂态；最终构建无错误退出）。
- ctest 第1遍：`-R <78 个旧 case 精确名>` → **78/78 Passed，exit 0**（FAST label 合计 20.44s）。
- ctest 第2遍：`-R <78 旧 + r35_t01_science::.* + r35_t01_landsat::.*>` → **91/91 Passed（含 13 个新 case），exit 0**，Total 22.74s。
  - 13 新 case：r35_t01_science:: 9（WP02×1、WP03×2、WP04×2、WP05×1、WP05/06×1、WP06×2）；r35_t01_landsat:: 4（WP01×4）。
  - 旧 case 分布（R2 复核修正）：test_pca 8、test_change_detection 41、test_spectral_unmixing_fcls 8、test_satellite_products 21（合计 78；含 #680 L2 回归与 MAD 流式等价回归）。
- 环境边界：QT_QPA_PLATFORM=offscreen（本验证为 CTest，非可见桌面）；未跑完整 CTest 全集/Windows/Release/GPU——如实记录，不称全绿。

## 第4轮 | R2 + 交付 | 通过 | 2026-11-19

- R2 独立复查绑定 `621b6a2a1f`：**PASS**（0 P0/P1；1 条 P3 账本计数笔误已修，1 条 P3 预先存在不可达如实记录）。详见 REVIEW_R2.md。
- 提交前刷新：master 仍为 120677e6（无需 rebase）；open PR 仅 #2149（T06 i18n，零写集重叠，已记保留区）。
- 推送 `zcode/r35-t01-science-20261118T155456Z` → **PR #2150**：https://github.com/WindWang2/exp-rs/pull/2150
- 最终 HEAD `1c553ac1385ad3f32050bd5827dd3359f0cc3f42`（4 commits：fix / test / docs×2）。
- 切片1（WP01–WP06）闭环。剩余 WP07–WP20 为本轨后续切片候选（见 PLAN.md）。
