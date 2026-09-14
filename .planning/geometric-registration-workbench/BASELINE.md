# BASELINE — D14 Geometric Registration Workbench

## Git 基线
- **Worktree**: `../exp-rs-geometric-registration-workbench`（物理隔离，master 只读）
- **分支**: `zcode/geometric-registration-workbench`
- **基线 Commit**: `007e70cff6f43151aef6cf7e501c14bcb94a5090`（origin/master，PR #985 合并点）
- **基线 diff 策略**: 全部新文件 + CMake/.gitignore 最小接线，禁止触碰 `src/workflow/`、`src/dataset/split.h`、辐射传输（D13/D15/D17 领地）。

## 工具链（宿主机实测）
| 组件 | 版本 | 备注 |
|---|---|---|
| GCC | 16.2.1 20260810 (`/usr/bin/g++`) | C++20 |
| CMake | 4.4.3 (`/usr/bin/cmake`) | ⚠ `~/.local/bin/cmake` 是损坏 shim，必须用绝对路径 |
| Ninja | 1.13.2 | 并发硬锁定 `-j2` |
| ccache | 4.14 | `CMAKE_*_COMPILER_LAUNCHER=/usr/bin/ccache`，主仓 Release 构建已预热缓存 |
| Qt6 | 系统 Qt6（`/usr/lib/cmake/Qt6`） | 测试运行 `QT_QPA_PLATFORM=offscreen` |
| Catch2 | v3.7.1（FetchContent, `CMakeLists.txt:756`） | `Catch2::Catch2WithMain` |
| GDAL | 系统（`/usr/lib/cmake/gdal`） | 基线已有 |

## 构建配置（与主仓 build/ 缓存一致以命中 ccache）
```
/usr/bin/cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_TESTS=ON -DENABLE_LOCAL_BUILD_SHORTCUTS=ON \
  -DCMAKE_CXX_COMPILER_LAUNCHER=/usr/bin/ccache \
  -DCMAKE_C_COMPILER_LAUNCHER=/usr/bin/ccache
```
- **资源红线**: `CMAKE_BUILD_PARALLEL_LEVEL=2`、`CTEST_PARALLEL_LEVEL=1`、`ninja -j2`（内存 RSS>70% 或 load>1.5×cores 时降级 `-j1`）。
- **零远端 CI**: 全部验证本地 ctest，绝不触发 GitHub Actions。

## 基线测试状态
- master 合并点（PR #985）为已验收绿基线；本 track 新增 9 个测试目标，基线既有测试不在回归责任范围（但 D14 测试必须在最终 HEAD 全绿）。
- 首次构建完成后以 `ctest -R "test_gcp_manager|..."` 记录 D14 套件从 Red 到 Green 的演进（见 EVIDENCE.md）。
