# R21-E TEST_RESULTS — 测试与证据汇总（持续更新）

## 构建环境（可复现）
```
WT=/home/kevin/project/exp-rs-r21-E-20261023T215154Z（BASE=origin/master 911e0b17，gh API 复核）
export CMAKE_BUILD_PARALLEL_LEVEL=4 QT_QPA_PLATFORM=offscreen   # 锁空闲时 -j4，争抢时 -j2
cmake -S . -B build-r21-E -DENABLE_TESTS=ON -DENABLE_LOCAL_BUILD_SHORTCUTS=ON \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH=/home/kevin/project/paleo-singlefactor-closure/vendor/prefix/usr/lib/x86_64-linux-gnu \
  -DGDAL_INCLUDE_DIR=…/vendor/prefix/usr/include/gdal \
  -DGDAL_LIBRARY=…/vendor/prefix/usr/lib/x86_64-linux-gnu/libgdal.so \
  -DPROJ_DIR=…/cmake/proj -DGEOS_DIR=…/cmake/GEOS \
  -DGSL_INCLUDE_DIR=…/vendor/prefix/usr/include \
  -DNETCDF_INCLUDE_DIR=…/usr/include -DNETCDF_LIBRARY=…/libnetcdf.so \
  -DEXPAT_LIBRARY_RELEASE=/usr/lib/libexpat.so -DSQLite3_LIBRARY=/usr/lib/libsqlite3.so
flock -x /tmp/exp-rs-r21-build.lock -c 'cmake --build build-r21-E --target <targets> -j4'
```
依赖为宿主环境事实（vendor prefix 非本轨产物）。构建锁五轨共享，实测被 r21-D 全量构建持有期间本轨排队。

## 逐项结果（RED→GREEN×2）

### W2 faultlab verifier（#1613）
| 阶段 | 命令 | 结果 |
|---|---|---|
| RED@BASE | `./build-r21-E/tests/test_r21_e_faultlab_verifier` | **1 failed**（REQUIRE(result.ok) false——loader 拒绝 schema 文档化 verifier）2 passed |
| GREEN@HEAD v1 | 同上 | **All passed (8 assertions / 3 cases)** |
| GREEN@HEAD v2（扩展 runner 语义 + R1 修复后） | `./build-r21-E/tests/test_r21_e_faultlab_verifier` | **All passed (58 assertions / 9 cases)** |
| R1 修复过程实证 | 同 lane 逐轮 | 56/58（发现 vacuous 规则作用域 bug + helper 期望方向）→ 修后 58/58 |

### W1 verifier evidence（#1601）
| 阶段 | 状态 |
|---|---|
| RED@BASE 构建 | ⏳ qgis_core/gui 首编中（静态 expat 链接陷阱已排，见 CROSS_TRACK CT-1） |

### W5 批量压力（O-E2）
- 目标 `test_r21_e_batch_stress` 编译排队（同锁）。跑后补 100/500/1000 的耗时/RSS/记账表。

### W6 端到端（O-E1/O-E5）
- 脚本 `.planning/r21-E/e2e_offline_lab.sh` 就绪（样例生成→NDVI pipeline→grade 断言 verifier 证据在场→批量→审计→重跑确定性）。待 CLI 二进制链接完成后执行。

## 残余与 NOT_EXECUTED（诚实归因，随进度更新）
- Windows 侧 run_lab.cmd/grade_all.cmd 实机执行：**NOT_EXECUTED**（本宿主为 Linux；协议允许但需 Windows 环境在场）。
- macOS/TSan/ASan：NOT_EXECUTED（不在本轨资源面）。


## 最终状态（R2 修复后，2026-10-24）
| Lane | 结果 |
|---|---|
| test_r21_e_verifier_evidence | **All passed (22 assertions / 4 cases) ×2** |
| test_r21_e_faultlab_verifier | **All passed (66 assertions / 10 cases) ×2**（含 R2 P3-4 新增） |
| test_r21_e_batch_stress | **All passed (31 assertions / 3 cases) ×2**（含 R2 P0-1 记账修正） |
| test_r21_e_studio_verifier_hook | **All passed (11 assertions / 3 sections) ×2**（含 R2 P1-1 控制组） |
| 回归 sweep（28 在轨目标，round 2） | **pass=28 fail=0 skip=0** |
| e2e 离线全链路 | **ALL E2E STEPS PASSED**（exit 0） |

## 运行环境（测试执行必需，否则两 lane 环境性失败——R2 P1-2）
```bash
VENDOR=/home/kevin/project/paleo-singlefactor-closure/vendor/prefix/usr
SHIM=/home/kevin/project/exp-rs-r21-B-20261023T185101Z/build-r21-B/link-shim  # 兄弟轨依赖垫片
export LD_LIBRARY_PATH=$SHIM:$VENDOR/lib/x86_64-linux-gnu:/usr/lib   # 顺序关键：vendor Qt 旧版会遮蔽系统 Qt6.12
export PROJ_DATA=$VENDOR/share/proj GDAL_DATA=$VENDOR/share/gdal
export QT_QPA_PLATFORM=offscreen SICNU_LAB_RULES_DIR=$PWD/data/labs/grading
```
构建需追加（vendor 静态库无 -fPIC，会破坏 qgis_core 共享库链接）：
`-DZLIB_LIBRARY_RELEASE=/usr/lib/libz.so -DEXPAT_LIBRARY_RELEASE=/usr/lib/libexpat.so -DSQLite3_LIBRARY=/usr/lib/libsqlite3.so -DCMAKE_EXE_LINKER_FLAGS="-Wl,--allow-shlib-undefined"`
（EXPAT/SQLite/ZLIB 静态 .a 与 openblas 后缀符号问题与兄弟轨同源；见 CROSS_TRACK_FINDINGS CT-1。）

## 性能证据（W5，O-E2；计时/RSS 为证据非门——#1622 纪律）
见各 GREEN_r*_test_r21_e_batch_stress.txt 的 INFO 行（scale=100/500/1000 的 wall_ms 与 rss_hwm_kb）。
