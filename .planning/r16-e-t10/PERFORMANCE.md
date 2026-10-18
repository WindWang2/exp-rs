# R16-E T10 PERFORMANCE — 实测耗时与资源

宿主：40 核 / 62GB（本轨全程 RSS 空闲 ≥21GB，未触发降档）；磁盘空闲 346GB；无 ccache。
纪律：编译硬上限 `-j4`；ctest `-j1` + `QT_QPA_PLATFORM=offscreen`；共享锁
`/home/kevin/project/exp-rs-r16-build.lock` 全程持锁（configure 与 build 均未绕过）。

## Configure（build-r16-e-t10，Ninja/Debug 轻量组合）

| 尝试 | 结果 | 耗时 |
|---|---|---|
| 1. 默认环境 | FAIL：FindGDAL 找不到 gdal-config | ~1 分钟（到 GDAL 探测） |
| 2. +`CMAKE_PREFIX_PATH=/home/kevin/pwb-sdks/root/usr` | FAIL：Catch2 FetchContent 走 `127.0.0.1` 代理克隆 github.com 失败（重试 3 次） | ~3 分钟（含探测/feature probes） |
| 3. +`FETCHCONTENT_SOURCE_DIR_CATCH2=/home/kevin/.cache/exp-rs-fdeps/catch2-v3.7.1` | **成功**：`Configuring done (9.7s)` / `Generating done (24.6s)` | ~3.5 分钟 |

锁等待：三次尝试均未阻塞（等锁 <1s；与其它 4 个并行 Prompt 无长冲突）。

## Targeted build（WP1）

`cmake --build build-r16-e-t10 --target test_io_vector_contract --parallel 4`
（flock 持锁）：**176/176 步成功，wall ≈ 2 分钟**（15:03:46 启动，15:05 链接出
15MB 可执行文件）。`test_io_vector_contract` 经 `sicnu_add_io_test` 只链
`Catch2::Catch2WithMain + Sicnu::Geospatial`（Qt-free），**不拉 qgis_core**——
prompt 预估的"1-2 小时 qgis_core 依赖链"对该目标不适用。

## Test 运行

- `ctest -j1 -R io_vector_contract`：14 用例，real 2.43s。
- conformance.py（20 检查，subprocess 驱动 verifier）：秒级。
- census static：1007 文件扫描 + 注释剥离匹配：<2s。

## 脚本级验证（WP3/WP4/WP5）

- lupdate 全量扫描（13→14 目录，~23.6k strings）：单次 ~1-2 分钟内完成（300s timeout 内）。
- 合成 bundle / 合成 presets 反例：全部 mktemp 临时目录，用后即删，未入库。

## 对比参考（RISK_REGISTER R3 预估）

本轨因"目标性构建 + io 套件轻链"实际总构建成本 ≈ 2.5 分钟，远低于 70-120 分钟全量
预估；验证了"只建目标性 target"策略在该套件族上成立（sicnu_add_io_test/sicnu_add_sdk_test/
sicnu_add_study_test 同族均可分钟级构建；sicnu_add_test 重链族才会拉 qgis_core）。
