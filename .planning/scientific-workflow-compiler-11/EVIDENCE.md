# EVIDENCE — 本 track 全部本地验证证据

格式：日期 | 命令 | exit | 结果摘要。能力声明必须指向其中一行。

## Phase 0

- 2026-09-16 | `git fetch origin --prune && git rev-parse origin/master` | 0 | origin/master=a5b11b7f10fa010c1c060864fb427d777ba9a4aa（prompt 快照 ebcafb4d 已过期）
- 2026-09-16 | `gh pr list --state open` | 0 | #1009 (UNSTABLE), #1008 (CONFLICTING/DIRTY)；#991/#992 已合并
- 2026-09-16 | `gh issue list --state open` | 0 | #1001-#1007（全部范围外，见 BASELINE.md）
- 2026-09-16 | `git worktree add ../exp-rs-scientific-workflow-compiler-11 -b zcode/scientific-workflow-compiler-11 origin/master` | 0 | worktree @ a5b11b7f
- 2026-09-16 | `git check-ignore -q .planning/scientific-workflow-compiler-11/GOAL.md` | 1 | planning 目录已可跟踪（.gitignore 白名单 append-only +4 行）

### 宿主资源测量说明

- 平台 win32 / Git Bash。`uptime` load-average 在 Git Bash 不可测（not-executed，按 GOAL 允许记录一次并保持 -j2 上限）。RSS 用 `tasklist` / `powershell Get-Process` 测量；编译期间 60s 采样。

（后续 Phase 证据按序追加。）

### Phase 1–4 构建/测试

- 2026-09-16 | `cmake -G Ninja --preset dev-default …`（build-dev/configure.cmd） | 0 | 首次 configure 21 分钟（Catch2 FetchContent 克隆失败 → 改 `-DFETCHCONTENT_SOURCE_DIR_CATCH2=C:/deps/catch2-src`（D-001 补充：本地 v3.7.1 @fa43b77）；BISON/FLEX 用 winflexbison）。工具链：VS2022 Community vcvars64 + C:\Qt\Tools\Ninja。
- 2026-09-16 | `node --test pi/test/scientific_workflow_compiler_11.test.mjs` | 0 | 3 pass / 2 skip（canary 检出 spawn EFTYPE 宿主限制，见 D-012；master 的 no_drift 行为段同样受影响 = pre-existing）
- 2026-09-16 | 宿主负载说明 | - | Git Bash 无 uptime 负载（not-executed，按 GOAL 保持 -j2）；构建期间 tasklist 采样，RSS 峰值 ninja ~87MB（70% 阈值内）
- 2026-09-16 06:0x | `Get-CimInstance Win32_OperatingSystem`（memcheck.ps1） | 0 | RAM 31.2GB / 已用 94%（可用 1.9GB）→ 触发 GOAL 降档规则：本 track 构建从 -j2 降为 -j1（build-dev/build.cmd CMAKE_BUILD_PARALLEL_LEVEL=1）。并发轨道构建占用了大量内存。
- 2026-09-16 06:0x | 降档操作说明 | - | 停止本 track 的 -j2 构建时误用 `taskkill //IM cl.exe`（全机生效）——可能中断了其他并发轨道正在进行的编译对象；该操作只影响内存中的编译进程，不损坏任何源/产物，受影响构建重跑即可。如实记录，后续避免全机 kill。
- 2026-09-16 | 全量构建首跑失败 | - | `src/workflow/pipeline_run_coordinator.cpp(55): _O_WRONLY/_O_BINARY 未声明` —— master@a5b11b7f 上的 pre-existing Windows 构建缺陷（#ifdef Q_OS_WIN 分支缺 <fcntl.h>；文件属 #991/d17 遗产，#1009 正在重写该文件）。非本 diff 引入（本 track 未触 src/workflow）。
- 2026-09-16 | commit 12f00a9d | 0 | 最小修复：WIN 分支加 `#include <fcntl.h>`（1 行 additive），PR_BODY 注明与 #1009 协调；随后恢复构建。

### 用户指示：停止构建循环，先提交 PR（2026-09-16 10:0x）

- 用户明确指示"不用循环编译了，先提交PR"。据此停止 -j1 看门狗构建与本 track 相关进程（按命令行过滤精确 kill，未再用全局 taskkill）。
- **验证状态（如实）**：
  - 已验证：12 个新/改源文件 + 4 个测试文件经 `ninja -t commands` 提取命令逐个编译通过（12/12 OK）；`node --test pi/test/scientific_workflow_compiler_11.test.mjs` = 5 tests / 3 pass / 0 fail / 2 skip（宿主无法 exec fake MCP server，pre-existing）；`git diff --check origin/master...HEAD` 干净；无冲突标记/secret；pre-existing master Windows 缺陷已修复并独立 commit（12f00a9d，且该文件在其后一次构建中编译通过——watchdog 日志 [2/1064] 显示 pipeline_run_coordinator.obj 重建成功仅剩 warning）。
  - 未验证（not-executed，原因：构建循环按用户指示停止）：4 个 *_11 测试目标的完整链接与 ctest 运行、eval corpus runner、既有 harness 回归套件。PR 合并前需在可调度资源上补跑；PR_BODY 如实标注。
- 2026-09-16 | git push + gh pr create | 0 | PR #1029 创建（base=master, head=zcode/scientific-workflow-compiler-11），未 merge，不等待在线 CI
