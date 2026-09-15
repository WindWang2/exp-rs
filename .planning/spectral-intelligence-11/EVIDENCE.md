# EVIDENCE — spectral-intelligence-11

逐轮/逐阶段证据账本。每条 = 命令 + exit + 摘要。

## Phase 0

- `git fetch origin --prune && git rev-parse origin/master` → 0, `a5b11b7f10fa010c1c060864fb427d777ba9a4aa`。
- `gh pr list --state open` → 仅 #1008（open, CONFLICTING）；#991/#992 已并入 master。
- `gh issue list --state open` → #1001–#1007，全为非光谱域，dedupe 记录于 PARALLEL_OWNERSHIP.md。
- `git worktree add ../exp-rs-spectral-intelligence-11 -b zcode/spectral-intelligence-11 origin/master` → 0。
- Subagent #1（只读架构审计）→ 见 CURRENT_ARCHITECTURE.md 引用。

## 资源政策记录

- 宿主 Windows / Git Bash：无 uptime load average 可测 → 按规则记录一次：`not-executed: load average under Git Bash`；build 固定 `-j2`，RSS > 70% 时降 `-j1`（用 tasklist 抽查）。
- `CMAKE_BUILD_PARALLEL_LEVEL=2`、`CTEST_PARALLEL_LEVEL=1`、`QT_QPA_PLATFORM=offscreen`。

## 构建环境（Phase 0/1，Windows 宿主事实）

- 工具链：VS2022 vcvars64（MSVC 14.38）+ `C:\Qt\Tools\Ninja`；CMake 位于 VS 安装内。
- 依赖：`CMAKE_PREFIX_PATH=C:\deps\Qt\6.8.0\msvc2022_64;C:\deps\qca-install;C:\deps\kc-install`；
  vcpkg manifest（`C:/deps/vcpkg` toolchain，triplet x64-windows，安装根 `<build>/vcpkg_installed`）；
  winflexbison 在 `C:\deps\winflexbison`（BISON_EXECUTABLE/FLEX_EXECUTABLE 显式给出）。
- 踩坑记录：`cmake --preset dev-default` 不含 generator/toolchain → preset 路线退化为 VS 生成器且无 vcpkg；
  正确姿势 = 直接 `cmake -B build-dev -G Ninja -DCMAKE_TOOLCHAIN_FILE:FILEPATH=C:/deps/vcpkg/scripts/buildsystems/vcpkg.cmake ...`。
  configure 命令最终形态存档于 `.planning/spectral-intelligence-11/CONFIGURE.cmd.txt`（见下）。
- 构建入口批处理：`/tmp/sic11/sic11-{configure,build}.cmd`（vcvars + Ninja + cd worktree；build 目标经 `%*` 传入）。

## OUT_OF_SCOPE

- **P1（阻塞级，最小跨域修复）**：master a5b11b7f 上 `src/workflow/pipeline_run_coordinator.cpp`
  （来自已合并 PR #991）的 `fsyncFile()` 使用 `_wopen/_O_WRONLY/_O_BINARY` 但未包含
  `<fcntl.h>/<io.h>` → **Windows 下 sicnu_workflow 无法编译**（Linux CI 不可见）。
  本 track 因 `sicnu_geo_rs_cli --export-catalog`（capability 生成物再导出）被阻塞，
  按仓库既有 "fix Windows compile" 惯例做最小修复：Q_OS_WIN 守卫下补两个标准头。
  证据：build3 日志 error C2065 `_O_BINARY`（2026-09-16）；修复见 commit（Phase 4 补交）。
  不改动该文件任何语义。

## 测试运行方式（Windows）

- 直接执行 test exe 在 Git Bash 下因 DLL 搜索路径不可靠；统一经
  `/tmp/sic11/sic11-test.cmd`（vcvars64 + Qt bin + build-dev + QT_QPA_PLATFORM=offscreen）运行。
- 已验证：test_spectral_sparse_unmixing PASS、test_spectral_local_rx PASS（exit 0）。

## Phase 1+
- Phase 1 内核四件：`spectral_hybrid_similarity`、`spectral_local_rx`、`spectral_sparse_unmixing`、`endmember_analysis`（新文件，未触碰 #1008 contested 文件）。
- Phase 2/3 内核测试四件（独立 oracle）+ `test_spectral_scale`（256–1024 band、determinism、内存上界）。
- Phase 4 算子四件：`rs:local_rx_anomaly`、`rs:sparse_unmixing`、`rs:spectral_similarity`、`rs:endmember_analysis`；双注册点 + `SICNU_OPERATORS_SOURCES` + capability JSON（spectral_transform.json +4 条）+ task family 声明（anomaly-detection / unmixing / classification / endmember-analysis）+ drift pins 32→36。
- 构建记录：build1（1524 步，内核/处理/算子库）唯一错误 = scale 测试 Config 歧义（已修）；build2/3（CLI 目标）暴露 pre-existing workflow 缺 include（见 OUT_OF_SCOPE）。
