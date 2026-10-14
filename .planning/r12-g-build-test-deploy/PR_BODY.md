# R12-G｜构建 / 本地验证 / 离线交付加固

独立 worktree `exp-rs-r12-g-build-test`，基线 `origin/master = 28abef358`。范围严格限定在
`scripts/**`、`packaging/**` 与 `tests/CMakeLists.txt` 的短租改动，**未触碰 `src/**`，未删除任何测试**。

## 修了什么

### 1. 构建与资源纪律（G06）
- `scripts/build.sh` 的 `build` / `test` 真正接入已有的跨进程租约 `scripts/dev/resource_guard.py`（此前该租约无人调用，纯装饰）。
- R1 指出的 3 个 P1 全部修复：
  - `SICNU_BUILD_REQUIRE_GUARD=1` 原先被进程替换吞掉（无租约构建仍 exit 0）→ 改为主 shell 决策，现在真实拒绝（exit 1，payload 未跑）；
  - `--jobs` 原先绕过 `-j<=2` 上限校验 → 现在过 `resolve_jobs`，`--jobs 8` 被拒（exit 2），而 `SICNU_BUILD_JOBS_CAP=99 --jobs 8` 这条刻意抬 cap 的路径仍放行；
  - `SICNU_BUILD_REQUIRE_GUARD` 与 `SICNU_BUILD_NO_GUARD` 同时设定改为显式冲突。
- 守卫新增 `--max-parallel` 护栏：默认 2，`--parallel 8` 被拒（exit 2），`--max-parallel 99` 放行。

### 2. 可选依赖的精确失败（G05）
- netcdf 原先只有 `find_library`，3 个 TU `#include <netcdf.h>` → 库在头不在的环境里伪成功。改为 `find_library` + `find_path` 成对探测。
- 首版 census 漏了第 4 个 TU（`test_io_roundtrip_matrix`），由 R1-03 指出并补齐。
- 真实 configure ×2：`exit=0`，`NETCDF_INCLUDE_DIR` 命中 `vcpkg_installed/x64-windows/include`。

### 3. 验证器不再"零证据也算通过"（G12 / G14）
- `verification_ladder.py`：追加 `--no-tests=error`（带能力探测）+ "No tests were found" 字符串双判据；新增 `executed_items / planned_items`，planned>0 且 executed==0 判负。
- `scripts/bench/perf_observatory_verify.py`：第三份独立 `run_ctest` 同样是空匹配恒绿，且 O1 打印**硬编码** `13/13 green`、O3 在 0 vs 0 记录间空集恒等 PASS。三处全部修掉。
  对照实测（同一个空注册构建目录）：
  ```
  旧: PASS O1   run 1: 13/13 green (offline, fixed seed)      ← 0 个测试也绿
      PASS O3   structural indicators identical across the two runs
  新: FAIL O1   run 1: FAILED (offline, fixed seed) — see .../run-a/ctest.log
      FAIL O3   ... only 0 / 0 records (expected >= 13): comparing them proves nothing
  ```

### 4. 离线交付包校验（G09 / G08）
- **junction 逃逸（P1，安全）**：Windows 目录 junction 的重解析 tag 是 `0xA0000003`，`os.path.islink()` 返回 False，`os.walk` 直接下降进包外目录。实测把包外 `outsidedir\secret.txt` 登记为 `jlink/secret.txt` 后，**旧版 `BUNDLE VERIFY PASS / exit 0`**；现改为 realpath containment + 重解析点 prune，两条路径都 FAIL 并给出 typed 诊断。
- **pwsh 7 误判（P1）**：`$ceiling -isnot [int]`，而 PS7 的 `ConvertFrom-Json` 产出 Int64 → 每个正确 bundle 都被判 FAIL。同文件 142 行早已接受 `[long]`，两处不一致。影响随包分发给教师的 `packaging/bundle/VERIFY.ps1`。修复后同一正确包：5.1 与 pwsh 7 均 PASS。
- **两个审计未报的新缺陷**（本机复现后修）：
  - `VERIFY.ps1` 在 bundle root 为非规范路径（8.3 短名 `%TEMP%`）时，`Get-RelPath` 的字符串前缀比较失配 → **抛未处理异常**，教师只看到堆栈。改为 `[IO.Path]::GetFullPath` 归一。
  - `_unsafe_bundle_rel` 对 `/etc/passwd` 这类 rooted 路径：Linux `isabs`=True 判 unsafe，Windows ntpath 返回 False 降级为 `required missing` → 同一清单跨平台结论不一致。已显式拒绝。
- 回归：`tests/fixtures/bundle_manifest/conformance.py` 由 **18 checks / 1 failed** 变为 **18 checks / 0 failed**（那条 red 是既有的，用 HEAD 版对照确认）。

### 5. 打包脚本不再谎报成功（G13）
- `build-appimage.sh` 的 OTB 段注释写 required，四个 `cp` 全是 `|| true`，却无条件打印 `OTB bundled successfully`。改为统计实际复制文件数，0 个即 `exit 1`。
  实测：旧块空源 → `OTB bundled successfully` + exit 0；新块空源 → `ERROR: OTB is required but 0 files were copied` + exit 1；有内容源 → `OTB bundled successfully (4 files)` + exit 0。
- GDAL（optional）消息改为打印真实计数。

## 证据

| 层 | 结果 |
|---|---|
| L0 静态 | `bash -n scripts/build.sh` OK；三个 py `py_compile` OK |
| L1 hermetic | `scripts/build.sh selftest` → `selftest: ALL PASS`（含新增"busy build dir refused (exit 75); payload never ran"）；`check_presets.py` → `ALL PASS` |
| L2 configure | 真实 `cmake -S . -B build-dev` ×2 → `exit=0`，`NETCDF_INCLUDE_DIR` 入缓存 |
| L3 编译 | `test_paths.exe`（2,393,600 B）真实产出 |
| L4 ctest | ❌ BLOCKED：`0xc0000135`（DLL 未找到）。已试 Qt bin / vcpkg debug bin / VC DebugCRT 三种 PATH 补全均无效，与本次改动无关 |

## 明确没做（不虚报）

- **G13-1**：AppImage 工具链校验和仍为 `PENDING`，`fetch_tool` 必失败，连 `--tools-dir` 预置也不可用。修复需联网抓固定 tag 产物做 sha256，本机离线 → BLOCKED（步骤已写入审计）。
- **G10 全部 P2**：均为 `.cmd/.bat`，本机 cmd.exe 被安全策略阻断，改了无法验证 → 未动。
- **G02 / G03 / G11**：属其他方向所有权（跨方向 `_exit` TU、`src/core` 依赖、非 pid scratch root），只审计不修。
- **R2 独立审查**：子代理配额 429（重置 2026-10-14 23:00 UTC+8）。**R1 已完成**（general-purpose-4，3 个 P1 已全部修复并复现验证），**R2 未执行** —— 合并前需补。
- `.gitignore` 未加 `*.agent-build.lock`（R1-04），属共享文件，本次未动；本 worktree 根目录下的残留锁文件未提交。

## 反作弊自查

未删除/禁用任何测试；未放宽任何超时；未改 golden；未把 NOT_RUN 记为 PASS；所有新增判据方向都是**收紧**。
唯一"变绿"是 conformance 中那条既有 red（`unsafe required absolute`），系真实缺陷修复所致。
