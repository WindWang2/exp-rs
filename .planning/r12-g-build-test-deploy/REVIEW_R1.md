# R12-G 第 1 轮独立评审（reviewer: general-purpose-4）

- 工作区：`C:/Users/wangj.KEVIN/projects/exp-rs-r12-g-build-test`，分支 `hardening/r12-g-build-test-offline`，基线 `origin/master = 28abef358`
- 被评审 diff：4 文件 / +184 −10（`scripts/build.sh`、`scripts/dev/resource_guard.py`、`scripts/verification_ladder.py`、`tests/CMakeLists.txt`）
- 评审方式：只读。实测命令均在 bash / python 中真实执行；未运行重型 C++ 编译；未执行任何 git 写操作。
- 本机环境事实（用于解释实测结果）：Git Bash（PortableGit 1.2.0）+ **Windows 原生 Python 3.13.14**；`ctest` **不在 PATH**；`python3`/`python` 解析到 `C:\Users\wangj.KEVIN\.workbuddy\binaries\python\versions\3.13.12\`。

---

## 1. 结论

# 3 个 P1 阻塞项，0 个 P0。

| ID | 严重度 | 一句话 |
|---|---|---|
| **R1-01** | **P1** | `SICNU_BUILD_REQUIRE_GUARD=1` 被进程替换吞掉：硬拒绝退化成 exit 0 的无租约构建（ledger 声称的"可升级为拒绝"在行为上不成立） |
| **R1-02** | **P1** | 去掉 `--parallel` 的 `choices=(1,2)` 后，`build.sh build --jobs 8` 从"被守卫拒绝 exit 2"变成"真的跑 -j8"，`-j<=2` 纪律被绕开 |
| **R1-03** | **P1** | G05-1 census 漏了 1 个 TU：含 `netcdf.h` 的是 **4** 个不是 3 个，`test_io_roundtrip_matrix` 未加 `target_include_directories` |

没有任何一条改动构成"把失败写成成功 / 吞退出码 / 放宽超时 / 删测试 / 改 golden / NOT_RUN 记 PASS"的反作弊红线违规 —— 所有新判据的方向都是**更严格**。
三条 P1 的共同点是：**修复不完整**，而不是修复方向错误。

次要项：4 个 P2、6 个 P3（见下表）。

---

## 2. 问题表

### R1-01 · P1 · `SICNU_BUILD_REQUIRE_GUARD=1` 被进程替换吞掉，硬拒绝变成 exit 0

- **位置**：`scripts/build.sh:188-193`（`guard_prefix` 内的 `fail`）；调用点 `scripts/build.sh:416-417`、`:439-440`
- **问题**：`guard_prefix` 是在**进程替换** `< <(guard_prefix ...)` 里调用的，即运行在子 shell 中。其内部的 `fail "..."` → `exit 1` 只结束那个子 shell；`while read` 循环拿到 EOF 后 `prefix` 为空，`cmd_build` 便**照常执行无租约的构建**，脚本最终 **exit 0**。

  ```bash
  # scripts/build.sh:415-421
  local prefix=()
  while IFS= read -r line; do prefix+=("$line"); done \
      < <(guard_prefix "$build_dir" "$jobs")     # ← 子 shell；里面的 exit 1 对外不可见
  if [ ${#prefix[@]} -gt 0 ]; then
      cmd=("${prefix[@]}" "${cmd[@]}")
  fi
  run_logged build "${cmd[@]}"
  ```

  ```bash
  # scripts/build.sh:188-193
  if [ ! -f "$GUARD" ] || ! py=$(guard_python); then
      note "resource-guard unavailable ..."
      if [ -n "${SICNU_BUILD_REQUIRE_GUARD:-}" ]; then
          fail "SICNU_BUILD_REQUIRE_GUARD=1 but the lease cannot be taken"   # ← exit 1 只杀子 shell
      fi
      return 0
  fi
  ```

- **复现（实测，已执行）**：

  ```bash
  # 构造：PATH 上无 python，且 stub cmake 会打印 PAYLOAD-RAN
  mkdir -p /tmp/g4test/bin /tmp/g4test/bd
  printf '#!/usr/bin/env bash\necho "PAYLOAD-RAN: $*"\nexit 0\n' > /tmp/g4test/bin/cmake
  chmod +x /tmp/g4test/bin/cmake

  env -i PATH=/usr/bin:/bin:/tmp/g4test/bin HOME=/tmp TMPDIR=/tmp \
      SICNU_BUILD_REQUIRE_GUARD=1 SICNU_BUILD_LOG_DIR=/tmp/g4test/logs \
      bash scripts/build.sh build --build-dir /tmp/g4test/bd
  ```

  实测输出：

  ```
  stdout: PAYLOAD-RAN: --build /tmp/g4test/bd --parallel 1
  stderr: build.sh: resource-guard unavailable (.../scripts/dev/resource_guard.py / python) — running WITHOUT the cross-process lease
  stderr: build.sh: SICNU_BUILD_REQUIRE_GUARD=1 but the lease cannot be taken
  EXIT_CODE=0            ← 期望 1，实际 0；构建在无租约状态下"成功"
  ```

- **为什么是问题**：
  1. `SICNU_BUILD_REQUIRE_GUARD` 是这份 diff 提供的**唯一**"租约不可用就别跑"的强制手段；它失效后，G06 只剩下"打一行 stderr 提示然后继续"。
  2. 失效是**静默的**：退出码为 0，CI 无从分辨。日志里有提示，但没有任何自动化会读它。
  3. ORACLE_LEDGER.md:39 明确把该开关写成修复的一部分（"`SICNU_BUILD_REQUIRE_GUARD=1` 可升级为拒绝"）。**该声称在行为层面为假** —— 这正是"声称有闸门、实际没有"的类型，虽然方向不是"把失败写成成功"，但属于同一类证据失真。
  4. 现实触发路径并不罕见：CI runner 只装 `py`（无 `python3`）时 `guard_python` 能过；但只装 `python3` 的容器 + `GUARD` 被 `.gitignore`/打包裁剪掉时就会走到这条分支。

- **建议修法**（任选其一，推荐第 1 种）：
  1. 让 `guard_prefix` **不自己 exit**，而是返回一个可判别的状态，由**主 shell** 决策：
     ```bash
     guard_prefix() {          # 成功：打印 argv 并 return 0；不可用：不打印并 return 1
         ...
         if [ ! -f "$GUARD" ] || ! py=$(guard_python); then
             note "resource-guard unavailable ..."
             [ -n "${SICNU_BUILD_REQUIRE_GUARD:-}" ] && GUARD_REFUSED=1
             return 1
         fi
         printf '%s\n' ... ; return 0
     }
     # 调用点：
     local prefix=() GUARD_REFUSED=0
     while IFS= read -r line; do prefix+=("$line"); done < <(guard_prefix "$build_dir" "$jobs")
     ```
     问题：`< <(...)` 里的变量改动不会传回主 shell，**必须改成临时文件**：
     ```bash
     local gf; gf="$(mktemp)"
     guard_prefix "$build_dir" "$jobs" >"$gf" || { rm -f "$gf"; [ -n "${SICNU_BUILD_REQUIRE_GUARD:-}" ] && fail "..."; }
     local prefix=()
     while IFS= read -r line; do prefix+=("$line"); done <"$gf"
     rm -f "$gf"
     ```
  2. 或退一步：把 `fail` 换成 `kill -TERM $$`（进程替换的子 shell 与主 shell 同 `$$`），能杀掉主脚本但语义丑陋，不推荐。
  3. 最小改动：保留 `fail`，但调用点改为 `mapfile -t prefix < <(guard_prefix ...); ...` 之后**再显式检查**一个由 `guard_prefix` 写到固定临时文件里的标记位。

  另外：`guard_prefix` 的 stderr 提示建议改成大写 `WARNING:` 前缀，便于日志检索。

---

### R1-02 · P1 · 去掉 `choices=(1,2)` 后，`--jobs N` 彻底绕开 `-j<=2`

- **位置**：`scripts/dev/resource_guard.py:282`（去掉 `choices`）、`:293-300`（新增 `<1` 拒绝）；`scripts/build.sh:404-412`（`--jobs` 从不进 `resolve_jobs`）

- **问题（两处叠加）**：

  **(a) 既有缺陷**：`cmd_build` 把 `--jobs` 的值存进 `jobs`，却用**无参**调用 `resolve_jobs`，于是 `--jobs` 永远走不到上限校验：

  ```bash
  # scripts/build.sh:406, 410
  --jobs) jobs=$2; shift 2 ;;
  ...
  if [ -z "$jobs" ]; then jobs=$(resolve_jobs); fi   # ← 从不传 "$jobs"
  ```
  `resolve_jobs` 支持参数（`:528` 的 `--print-jobs` 就传了），只是这里没传。对照 `scripts/build.sh:38` 的自述：*"SICNU_BUILD_JOBS_CAP=99 --jobs 8 must not succeed"* —— 这个不变式对 `--jobs` 从未成立。

  **(b) 本 diff 引入的回归**：在 (a) 存在的前提下，守卫是**唯一**拦住 `-j8` 的东西。基线版 `resource_guard.py` 用 `choices=(1,2)` 硬拒绝 `--parallel 8`（argparse exit 2）；放宽成 `>=1` 后直接放行。

- **复现（实测，已执行）**：

  ```bash
  # 1) build.sh 真正下发的 argv（取自 run_logged 写入的 SUMMARY.txt）
  SICNU_BUILD_LOG_DIR=/tmp/g4test/logs bash scripts/build.sh build \
      --build-dir /tmp/g4test/bd --jobs 8
  # /tmp/g4test/logs/build/SUMMARY.txt:
  #   command: python3 .../resource_guard.py --lock-dir /tmp/g4test/bd --wait 1800
  #            --parallel 8 -- cmake --build /tmp/g4test/bd --parallel 8
  #                          ^^^^^^^^^^^^^^                  ^^^^^^^^^^^^^^

  SICNU_BUILD_JOBS_CAP=99 SICNU_BUILD_JOBS=8 bash scripts/build.sh build --build-dir /tmp/g4test/bd
  #   command: ... --parallel 8 -- cmake --build ... --parallel 8

  bash scripts/build.sh build --build-dir /tmp/g4test/bd      # 对照：正常路径
  #   command: ... --parallel 1 -- cmake --build ... --parallel 1   ✓

  # 2) 守卫本身的前后对比
  python /tmp/g4test/base/resource_guard.py --lock-dir /tmp/g4test/bd --parallel 8 -- python -c "print('RAN')"
  #   resource_guard.py: error: argument --parallel: invalid choice: '8' (choose from '1', '2')
  #   BASELINE_EXIT=2                                    ← 基线：拒绝
  python scripts/dev/resource_guard.py --lock-dir /tmp/g4test/bd --parallel 8 -- python -c "print('RAN')"
  #   RAN
  #   CURRENT_EXIT=0                                     ← 当前：放行
  python scripts/dev/resource_guard.py --lock-dir /tmp/g4test/bd --parallel 0 -- python -c "print('RAN')"
  #   refused: --parallel must be >= 1, got 0
  #   EXIT=2                                             ✓ 新增的非法值拒绝有效
  ```

- **为什么是问题**：
  1. 这是**本 diff 新开**的口子。改动前 `build.sh build --jobs 8` 会被守卫拒在 exit 2；改动后 `-j8` 真的跑起来，并且 `governed_env(8)` 还会把 `CMAKE_BUILD_PARALLEL_LEVEL=8` **覆盖**主 shell 已 clamp 的值（`resource_guard.py:258-271`），`clamp_env_exports`（`build.sh:97-101`）的努力一并作废。
  2. 触发不需要恶意：`--jobs` 是 `build.sh` 的**公开文档化参数**（`scripts/build.sh:19`），任何照文档写的调用者都可能写大值。
  3. 作者给放宽 `choices` 的理由（"调用方 build.sh 才是 `-j` 上限的唯一权威"）本身**成立且有道理**，但它的前提是 build.sh 的上限真的生效 —— 而 (a) 证明它没生效。放宽与 (a) 组合 = 净削弱。
  4. 放宽本身没有破坏单元测试：`scripts/dev/tests/test_resource_guard.py` 全部用例都依赖**默认** `--parallel=2`，`_launch()`（`:93-99`）从不传 `--parallel`，所以 `choices` 的移除对测试零影响（这也是作者对 `SICNU_BUILD_JOBS_CAP` 抬升路径的正当诉求）。

- **建议修法**（两条都要，缺一条就还在漏）：
  1. **`scripts/build.sh:410`**：让 `--jobs` 走校验
     ```bash
     if [ -z "$jobs" ]; then jobs=$(resolve_jobs); else jobs=$(resolve_jobs "$jobs"); fi
     ```
  2. **`scripts/dev/resource_guard.py:282`**：保留放宽，但加一个**上限护栏**，把"调用方是权威"和"守卫是第二道防线"都保住，例如新增 `--max-parallel`（默认 2），`--parallel` 超过它时 `EXIT_REFUSED`；`build.sh` 传递 `--max-parallel "$JOBS_CAP"`。这样 `SICNU_BUILD_JOBS_CAP=99 --jobs 8` 仍按作者意图放行，而 `--jobs 8`（cap=2）被拒。
  3. 顺手修 `scripts/build.sh:38` 的注释与行为不一致（注释说"必须不成功"，代码从未如此）。

---

### R1-03 · P1 · G05-1 漏了第 4 个含 `netcdf.h` 的 TU：`test_io_roundtrip_matrix`

- **位置**：`scripts/../tests/CMakeLists.txt:445`（只有 `target_link_libraries`，没有 `target_include_directories`）；对照 `:443-444`、`:578-579`、`:616-617`

- **问题**：本 WP 的核心论据是"3 个 suite 直接 include `netcdf.h`"，但全量 census 是 **4 个**：

  ```bash
  grep -rn "netcdf.h" tests/ --include=*.cpp --include=*.h --include=*.hpp
  # tests/test_io_fabric_multidim_11.cpp:21:#include <netcdf.h>   ← :617 已加 include dir
  # tests/test_io_multidim.cpp:17:#include <netcdf.h>            ← :444 已加 include dir
  # tests/test_io_roundtrip_matrix.cpp:27:#include <netcdf.h>     ← :445 ✗ 只有 link，没有 include dir
  # tests/test_io_subdataset_inventory.cpp:16:#include <netcdf.h> ← :579 已加 include dir
  ```

  `tests/CMakeLists.txt:429` 的新注释和 ORACLE_LEDGER.md:94 都写"3 个 TU"—— census 少算一个。

- **为什么是问题**：
  1. `sicnu_add_io_test`（`tests/CMakeLists.txt:152-164`）只加 `${CMAKE_SOURCE_DIR}/src` 和 `${CMAKE_BINARY_DIR}`，不含任何 netcdf 路径；全仓库也没有覆盖 vcpkg include 的全局 `include_directories`（已核查 `tests/CMakeLists.txt` 无全局项，`CMakeLists.txt` 的 7 处 `include_directories` 分别指向 QCA / nlohmann / SPATIALINDEX / lazperf / `CMAKE_BINARY_DIR` / JSONCPP，均与 netcdf 无关）。
  2. 于是该 TU 能看到 `netcdf.h` **当且仅当**它在编译器默认 include 搜索路径里。
     - Linux + `libnetcdf-dev`：`/usr/include/netcdf.h` 是编译器默认路径 → 4 个 TU 都能编过 → 漏掉的这个暂时无害。
     - **Windows / vcpkg（也就是作者自己的机器）**：`find_path` 通过 `CMAKE_PREFIX_PATH` 命中 `.../vcpkg_installed/x64-windows/include`（ORACLE_LEDGER.md:106 的 GREEN1 就是这个路径），但**它不是编译器默认 include 路径**。结果：另外 3 个 TU 靠新增的 `target_include_directories` 编过，**`test_io_roundtrip_matrix` 仍然 `fatal error: netcdf.h: No such file or directory`**。
  3. 也就是说，这次改动**恰恰没消除它声称要消除的失败模式**，只是把它从 4 个 TU 缩到 1 个 TU。而且因为它同时把"库在头不在"的非对称环境变成 configure 期 FATAL_ERROR，头/库**对称但非默认路径**的环境（vcpkg 正是此类）就完全落在漏网的那一个 TU 上。
  4. 讽刺的是 FATAL_ERROR 文案（`tests/CMakeLists.txt:435-438`）自己就点名了 `test_io_roundtrip_matrix`，说它 "use it directly" —— 文案与实现不自洽。
  5. 未被发现的原因：ORACLE_LEDGER.md:12/108 自己记着 G05-1 的 **GREEN2（真实 configure 复核）从未执行**，唯一"GREEN"是 `cmake -P` 探针，而 `cmake -P` 不编译任何 TU，不可能暴露这个问题。

- **建议修法**：
  1. `tests/CMakeLists.txt:445` 后补一行
     ```cmake
     target_include_directories(test_io_roundtrip_matrix PRIVATE ${NETCDF_INCLUDE_DIR})
     ```
  2. 把 `:428-431` 注释和 ORACLE_LEDGER.md:94 的"3 个"改成 **4 个**。
  3. **务必跑一次真实 configure + 编这 4 个 target**（这是 ledger 自己欠的 GREEN2），否则这条 P1 无法关闭。

---

### R1-04 · P2 · 租约残留会把每次构建变成"阻塞 30 分钟然后 exit 75"；且新产物未被 `.gitignore` 覆盖

- **位置**：`scripts/build.sh:182`（`wait=${SICNU_BUILD_GUARD_WAIT:-1800}`）；`scripts/dev/resource_guard.py:83-107`（`_is_stale`）；`scripts/dev/dev_common.py:235-253`（`pid_alive`）；`.gitignore:8`

- **现场证据（本 worktree 当前状态）**：

  ```bash
  $ cat build-dev.agent-build.lock
  { "pid": 66716, "host": "kevin", "started_utc": "2026-10-13T17:30:41Z",
    "command": ["C:\\Qt\\Tools\\CMake_64\\bin\\cmake.exe","--build","build-dev","--parallel","2","--target","test_paths"] }

  $ python scripts/dev/resource_guard.py --lock-dir build-dev --status --json
  { "held": true, "holder": {"pid": 66716, ...}, "holder_alive": true }
  ```
  该锁已存在约 29 分钟，而 `pid_alive(66716)` 仍为 `true`。

  ```bash
  $ SICNU_BUILD_GUARD_WAIT=5 SICNU_BUILD_LOG_DIR=/tmp/g4test/logs \
        bash scripts/build.sh build --build-dir build-dev; echo "BUILD_EXIT=$?"
  # resource-guard: build dir busy (holder pid 66716, ...); waiting
  # refused: build dir ... is locked by pid 66716 (waited 1.5s); another build is writing it
  # build.sh: FAILED (build) — see /tmp/g4test/logs/build/SUMMARY.txt
  BUILD_EXIT=75
  ```

- **为什么是问题**：
  1. `build-dev` 是 `cmd_build`/`cmd_test` 的**默认** build dir（`scripts/build.sh:401`、`:425`）。默认 `SICNU_BUILD_GUARD_WAIT=1800` 意味着：**任何一个残留锁都会让后续每一次 `build.sh build` 静默阻塞 30 分钟然后 exit 75**。默认 1800 s 对一个"排队"语义来说偏长，且 `build.sh` 没有在失败时提示 `--status` 诊断命令。
  2. `pid_alive` 在 Windows 上是 `OpenProcess` + `GetExitCodeProcess`（`dev_common.py:239-253`）。**PID 可复用**：持有者死后，其 pid 被无关进程复用时，锁会被永久判定为"live"，`--stale-after`（默认 3600 s）这条兜底**永远不生效**（`_is_stale` 要求 pid 已死 **且** 超龄，两者都满足才破锁）。这不是理论风险——上面的 66716 就是活生生的例子（虽然尚未确认是复用还是真存活，但两者都会导致同样的后果）。
  3. 新产物**不受 `.gitignore` 保护**：
     ```bash
     $ git check-ignore -v build-dev.agent-build.lock ; echo $?
     1                      # ← 不被忽略
     $ git status --short
     ?? build-dev.agent-build.lock
     ```
     `.gitignore:8` 只忽略 `build-*/`，而锁文件是 `build-dev.agent-build.lock`（在 build 树**旁边**），模式不匹配。于是任何一次默认构建都会让 worktree 多出一个未忽略的未跟踪文件，`git add -A` 会把锁提交进仓库。本 diff 引入了一类新的根目录产物，却没同步 `.gitignore`。
  4. 作者自己的验证跑就留下了这个锁 —— 说明"跑一次就留一个锁"是默认行为，不是异常。

- **建议修法**：
  1. `.gitignore` 追加 `*.agent-build.lock`（和 `*.agent-build.lock.*.staged`）。
  2. `run_logged` 失败且 `rc == 75` 时，追加一行诊断：
     `python scripts/dev/resource_guard.py --lock-dir <dir> --status`
  3. 默认 `SICNU_BUILD_GUARD_WAIT` 从 1800 降到 300~600（多 agent 排队仍能覆盖一次重链接，CI 不会被拖 30 分钟）。
  4. （可选，治本）`pid_alive` 为 True 但 `started_monotonic` 已超 `--stale-after` 时，打印一条 `holder pid N still alive past stale-after — refusing to break` 的显式警告，让"卡死"可自证。

---

### R1-05 · P2 · G12-1 没有**任何**功能性 GREEN 证据，却被标为 PASS；`--list` 根本不经过新代码

- **位置**：`.planning/r12-g-build-test-deploy/ORACLE_LEDGER.md:11`（表格行）、`:83-90`（§3）

- **问题**：ledger 表格对 G12-1 记的是 `状态 = **PASS**`，而 GREEN1 写的是 `语法/--list OK`，GREEN2 写的是 `待跑对空树`。

  静态核对：`verification_ladder.py --list` 在 `:436-446` 就 `return 0` 了，**完全不执行** `:601-612` 新增的 executed/planned 聚合逻辑，也不执行任何 `run_*`。也就是说 GREEN1 连"新代码被跑到"都不能证明，更不用说"行为正确"。表格头自己写着"RED → 修复 → **GREEN×2**"，而 G06（`GREEN2 = 待跑第 2 次`）和 G12-1（`GREEN2 = 待跑`）都只跑了不到两次。

- **为什么是问题**：本轮评审的主题之一就是"有没有把没跑写成通过"。G12-1 恰恰是"零证据 → 不能算 PASS"这条治理规则的产物，它自己的证据栏却遵循了它要治理的模式：`状态=PASS` 与 `GREEN2=待跑` 同时出现。这会被后续读者当成已验证。

- **建议修法**：
  1. 把状态列改为 `PASS（GREEN2 待补）` 或 `IN_PROGRESS`，并在 FINAL_REPORT 收尾前真正跑一次对空树的 ladder，贴出
     `OVERALL NOTE: no item actually executed (N planned, 0 executed)` + `overall=attention` + `exit 1` 的完整输出。
  2. 顺手补一个反例对照：有 1 个 item 真 passed 其余 not-built 时，`executed==1`，overall 仍为 passed —— 这是**既有**语义（不是本 diff 引入），但 ledger 应显式记录，避免以后被误读成"只要有一个绿就全绿"是新引入的缺陷。

---

### R1-06 · P2 · `SICNU_BUILD_NO_GUARD` 优先级高于 `SICNU_BUILD_REQUIRE_GUARD`，可静默废掉硬拒绝

- **位置**：`scripts/build.sh:183-186` 先于 `:188-193`

  ```bash
  if [ -n "${SICNU_BUILD_NO_GUARD:-}" ]; then
      note "resource-guard disabled by SICNU_BUILD_NO_GUARD — ..."
      return 0                       # ← 直接返回，永远看不到下面的 REQUIRE_GUARD
  fi
  ...
  if [ -n "${SICNU_BUILD_REQUIRE_GUARD:-}" ]; then fail "..."; fi
  ```

- **静态论证**：`SICNU_BUILD_NO_GUARD=1 SICNU_BUILD_REQUIRE_GUARD=1` → 走第一个分支，打印 note，返回 0，prefix 为空，无租约执行。即"要求强制租约"被一个"关闭租约"的环境变量无声覆盖。CI 若在环境里残留了 `SICNU_BUILD_NO_GUARD`（例如某次调试 export 后被复用），`REQUIRE_GUARD=1` 这道阀门等于不存在。

- **建议修法**：把 `REQUIRE_GUARD` 的检查提到最前，二者同时设置时直接 `fail`（显式冲突优于静默优先）：

  ```bash
  if [ -n "${SICNU_BUILD_REQUIRE_GUARD:-}" ] && [ -n "${SICNU_BUILD_NO_GUARD:-}" ]; then
      fail "SICNU_BUILD_REQUIRE_GUARD=1 and SICNU_BUILD_NO_GUARD are mutually exclusive"
  fi
  ```
  另外，`SICNU_BUILD_NO_GUARD` 的 note 建议升级为大写 `WARNING:` 并同时写进 `run_logged` 的日志目录（目前只在 stderr，不落 SUMARY.txt）。

---

### R1-07 · P2 · selftest 第 4 组的时间窗口偏窄，且 selftest 总耗时从秒级涨到 ~2 分钟

- **位置**：`scripts/build.sh:282-289`（8 s 持锁 + 6 s 轮询预算）、`:283`

- **问题**：
  1. 轮询预算是 `for _ in $(seq 1 60); do ...; sleep 0.1; done` ≈ **6 s**。持锁者的 payload 是 `time.sleep(8)`，python 启动另需 ~1 s（本机实测 0.9~1.1 s）。若持锁进程启动 >6 s（多 agent 满载时完全可能，正是 G06 要保护的场景），会误报
     `selftest FAIL: lease was never taken (probe setup broken)`。
  2. 更窄的一处：子进程 `build.sh build` 必须在持锁者释放前完成"bash 启动 + python 启动 + 取锁尝试"，窗口是 `8 s + payload 启动时间 − 子进程自身启动开销`。`test_resource_guard.py:114-123` 的作者自己就写过注释"a fixed short budget made this test flaky under parallel agents"—— 同样的教训没有应用到 selftest。
  3. 实测 `bash scripts/build.sh selftest`：**exit 0 / `selftest: ALL PASS` / 约 2 分 6 秒**。CI 常见的 60 s 单步超时会直接把这步打死。

- **建议修法**：
  1. 把持锁 payload 从 `sleep(8)` 提到 `sleep(30)`，轮询预算提到 ~30 s（`seq 1 120` + `sleep 0.25`），并在轮询命中后立即进入下一步（现状已是 break 即止）。
  2. 更稳的做法：让持锁 payload 自己写一个 marker 文件（像 `test_resource_guard.py` 的 `first.started` 那样），selftest 等 marker 而不是等锁文件 —— 消除"锁出现/消失"的竞态。
  3. selftest 自带耗时打印（`echo "selftest: took ${SECONDS}s"`），让 2 分钟这件事在 CI 日志里可见。

---

### R1-08 · P3 · `acquire()` 的 `waited` 统计只计 `sleep`，系统性少报；默认 wait 下会严重误导

- **位置**：`scripts/dev/resource_guard.py:184-185`（`time.sleep(0.5); waited += 0.5`）

- **实测**：
  ```bash
  # --wait 2 → wall=3.75s，报告 "waited 0.5s"
  # --wait 5 → wall=6.98s，报告 "waited 1.5s"
  ```
  每轮循环除 `sleep` 外还有 `_holder_record`（json）、`os.open`/`os.write`/`os.link`、`_is_stale` → `pid_alive` → `OpenProcess` 等开销（本机约 1.2 s/轮），这些都不计入 `waited`。wall 时间对 `deadline` 是准的（已验证），但报给人和 `--json` 的 `waited_for_lock` 只有真实值的 ~1/4。

- **影响**：`build.sh` 默认 `--wait 1800`，真出问题时日志会写"waited ~430s；another build is writing it"，值班的人会按 7 分钟去查，实际卡了 30 分钟。另外 `wait "$lholder"` 之后的 `rm -rf "$lwork"`（`scripts/build.sh:306`）在持锁者尚未退出时执行会失败（已 `|| true`，无副作用）。

- **建议修法**：`waited` 改为 `time.monotonic() - t0`（`t0` 在 `deadline` 处一并记录）。

---

### R1-09 · P3 · 文档与实现漂移：`--parallel` 不再无条件夹到 ≤2

- **位置**：`scripts/dev/README.md:13`（"`--parallel N`/`-jN` clamped to ≤ 2"）、`docs/development/README.md:19`（示例用 `--parallel 8`）

- **问题**：`resource_guard.py` 的模块 docstring 已同步（`scripts/dev/resource_guard.py:9-12`），但两处外部文档没有：
  - `scripts/dev/README.md:13` 的"clamped to ≤ 2"现在是**有条件的**（仅在调用方未抬高 cap 时成立）；
  - `docs/development/README.md:19` 的示例 `--parallel 8` 之前会被 argparse 拒绝、现在会被接受——示例从"错误示范"变成了"合法调用"，语义变了却没改。

- **建议修法**：两处补一句"cap 由调用方声明，默认 2；`scripts/build.sh` 是 `-j` 上限的唯一权威"。

---

### R1-10 · P3 · `guard_python()` 信任 `SICNU_PYTHON` 而不验证

- **位置**：`scripts/build.sh:170-175`

  ```bash
  if [ -n "${SICNU_PYTHON:-}" ]; then printf '%s\n' "$SICNU_PYTHON"; return 0; fi
  ```
  若 `SICNU_PYTHON` 指向不存在/不可执行的解释器，`guard_prefix` 照样返回 prefix，`run_logged` 执行后拿到 127 并写 SUMMARY.txt —— 会**失败**（不是静默通过），但错误信息是 `command not found`，与"租约不可用"混在一起，排查成本高。

- **建议修法**：`[ -n "${SICNU_PYTHON:-}" ] && command -v "$SICNU_PYTHON" >/dev/null 2>&1 && { printf ...; return 0; }`，落空时给出明确的 `SICNU_PYTHON=<x> is not executable`。

---

### R1-11 · P3 · `_ctest_supports_no_tests()` 失败时静默降级（**已被第二道检查兜住，故仅为 P3**）

- **位置**：`scripts/verification_ladder.py:347-364`、`:377-378`、`:387-392`

- **问题**：探测函数捕获 `(OSError, subprocess.SubprocessError)` 后返回 `False`，不打印任何提示。本机实测 `ctest` 不在 PATH → `supports --no-tests: False` → 不追加 `--no-tests=error`。也就是说**在探测失败的机器上，G12-2 的主手段完全不生效且无任何提示**。

- **为什么只算 P3**：diff 在 `:387-392` 保留了**第二道**判据——
  ```python
  combined = (proc.stdout + "\n" + proc.stderr)
  if "No tests were found" in combined:
      return ("failed", f"ctest matched no test for pattern {pattern!r}: ...", elapsed)
  ```
  ctest 在 `--no-tests=error` 缺省时仍会打印 `No tests were found!!!` 且 exit 0，这段字符串判据照样把它判成 `failed`。**两道判据互为兜底，主手段失效不影响结论正确性**——这一点做得很对，是本 diff 里质量最高的一处。

- **建议修法**：探测返回 `False` 时打印一行 `note: ctest --no-tests unsupported/unavailable; falling back to output inspection`，避免以后有人删掉第二道判据时误以为还有第一道。

---

### R1-12 · P3 · `executed` 实际统计的是 `passed`，`--resume` 会把它灌满

- **位置**：`scripts/verification_ladder.py:601-608`

  ```python
  for _item in _lr["items"]:
      planned += 1
      if _item["status"] == "passed":
          executed += 1
  ```

- **问题**：
  1. 命名与语义不符：变量叫 `executed`，实际是 `passed`。`1 passed + 50 failed` 时 `executed=1`；`0 passed + 50 failed` 时 `executed=0` 并额外打印一句"no item actually executed"—— 后者与 `overall_ok` 已因失败为 False 而重复，文案在"全失败"场景下略有误导（不是没执行，是执行了全挂）。
  2. `--resume` 场景：`:507-525` 从 state 文件恢复的条目被标成 `status="passed"` 且本次未真跑，`executed` 照样 +1。于是"全部 resume 命中、本次零执行"的 run 会 `executed == planned > 0`，不触发新判据，overall 仍为 passed。**这是 `--resume` 的既有语义（且带 identity 校验：`binary.mtime_ns + HEAD sha`，`:516-519`），本 diff 没有让它变差**，但新字段名会让人误以为它度量了"本次执行量"。

- **建议修法**：
  1. 把 `executed` 改成统计 `status not in ("skipped", "not-built")`，或改名为 `passed_items` 并另设 `executed_items` 排除 `note == "resumed from state file"` 的条目。
  2. 文案补一句 `--resume` 说明。
  3. 已核查下游不受影响：`scripts/collect_readiness.py` 只读 `ladder.get("lanes")`（`:153`）与 `ladder.get("finished_utc")`（`:228`），**不读 `overall`**，新增的 `executed_items`/`planned_items` 是 additive 键，不会破坏解析。

---

### R1-13 · P3 · `guard_prefix` 的行协议对含换行的路径不安全（理论项）

- **位置**：`scripts/build.sh:195-197` + `:416-417`
- **问题**：`guard_prefix` 用 `printf '%s\n'` 逐行输出 9 个 argv，调用方用 `while IFS= read -r` 逐行读回。若 `--build-dir` 的值本身含 `\n`，一个参数会被拆成两个。**含空格和 Unicode 的路径是安全的**（见 §4 明确认可项），换行在 Windows/Linux 路径里都合法但现实中不会出现。
- **建议修法**：若要彻底消除，改用 `printf '%s\0'` + `read -r -d ''`，或改用 bash 数组 + `declare -p` 输出。优先级低。

---

## 3. 复核 GREEN 证据

| WP | ledger 声称 | 我的复核 | 判定 |
|---|---|---|---|
| **G06** GREEN1 `selftest ALL PASS (real 2m10s)` | 我实测 `bash scripts/build.sh selftest` → 16 项 ok + `selftest: ALL PASS` + **exit 0**，耗时 2m06s | **可信且可复现** ✓（但耗时是真的，见 R1-07） |
| **G06** "`SICNU_BUILD_REQUIRE_GUARD=1` 可升级为拒绝" | 实测 exit **0**，payload 照跑（R1-01） | **不可信，行为层面为假** ✗ |
| **G06** "租约或 python 缺失时**显式告警**（不静默跳过）" | stderr 确实有提示；但"不静默"只体现在日志，退出码完全静默 | 部分可信（提示有，拦截无） △ |
| **G12-2** GREEN1 `STATUS = failed / DETAIL = "ctest matched no test..."` | 静态核对代码路径成立；本机 `ctest` 不在 PATH，探测返回 False，**但 `:387-392` 的字符串判据独立兜底**，结论不受影响 | **可信**（且比声称的更健壮） ✓ |
| **G12-2** RED 段 `ctest EXIT=8`（`--no-tests=error`） | 本机无 ctest 无法复现；`--no-tests=error` 是 CMake ≥3.18 的既有语义 | 采信 △ |
| **G12-1** 状态 `PASS`，GREEN1 = `语法/--list OK` | `--list` 在 `:436-446` return，**完全不执行**新增的 `:601-612`；GREEN2 = 待跑 | **零功能性证据**（R1-05） ✗ |
| **G05-1** 状态 `PASS（待重配置复核）`，GREEN1 = `cmake -P` 探针 | 探针路径指向**主 worktree** `projects/exp-rs/build-dev/vcpkg_installed/...` 而非本 worktree；GREEN2（真实 configure）**从未执行**；且 census 漏 1 个 TU（R1-03） → 探针不可能暴露该问题 | **不可信，且已证伪其完整性** ✗ |
| **G05** `ALREADY_FIXED` (`tests/CMakeLists.txt:427-435` FATAL_ERROR) | 查证当前 `:434-441` 确为 `FATAL_ERROR` | 可信 ✓ |
| **G01/G06** `check_presets.py ALL PASS` | 实测 `python scripts/check_presets.py CMakePresets.json` → `check_presets: ALL PASS`，exit 0 | **可信且可复现** ✓ |

**关于"有没有把没跑写成通过"**：
- **代码层面：没有。** 三处新判据（`--no-tests=error` / "No tests were found"→failed / `executed==0`→overall 不为 passed）全部是**收紧**方向，没有任何一处放宽超时、删测试、改 golden、吞退出码或把 NOT_RUN 记成 PASS。退出码传播实测正确（见 §4）。
- **证据层面：有。** G12-1 与 G05-1 两个 WP 在 ledger 里被标成 PASS，但它们各自唯一的"GREEN"一个是 `--list`（不经过新代码）、一个是 `cmake -P`（不编译任何 TU，且漏了 1 个 TU）。**"状态=PASS"与"GREEN2=待跑"并列出现，正是本轮要治理的模式的翻版** —— 这是 R1-05 与 R1-03 的根因，也是本轮最值得改的流程问题。

---

## 4. 明确认可项（看过但没问题的，附理由）

1. **退出码传播正确（实测）**——`run_logged` 的 `tee` 管道 + `PIPESTATUS[0]`（`:138`）在套上守卫后依然准确：
   ```bash
   SICNU_BUILD_GUARD_WAIT=5 ... bash scripts/build.sh build --build-dir build-dev; echo $?
   # → BUILD_EXIT=75
   ```
   守卫 busy 返回 75 → `run_logged` 捕获 75 → 写 SUMMARY.txt → `cmd_build` 返回 75 → 脚本 exit 75。守卫正常路径返回 `proc.returncode`（`resource_guard.py:372`），payload 失败码原样传递（`test_resource_guard.py:275-284` 亦覆盖）。**无吞码。**

2. **`while IFS= read -r line` 不会拆开含空格/Unicode 的路径**——`IFS=` 关闭了前后空白裁剪，`-r` 关闭反斜杠转义，`guard_prefix` 用 `"$build_dir"` 引用输出，调用方用 `"${prefix[@]}"` 展开。leader 点名的这个攻击面**不成立**；selftest 第 2 组（`:228-236`）本身也在验证同类 quoting，两组一起覆盖了含空格 + CJK + 组合 Unicode 的目录名。

3. **`wait "$lholder"` 不会挂**——持锁者 payload 是 8 s 后自然退出，`wait` 最多阻塞 8 s；实测 selftest 全程 2m06s 正常结束并 `exit 0`。

4. **`argparse` 的 `REMAINDER` + 双重 `--` 处理正确**——实测 `python 3.13.14`：
   ```
   ['--','cmake','--build','bd','--parallel','2','--','--target','t1','t2']
     → after strip: ['cmake','--build','bd','--parallel','2','--','--target','t1','t2']
   ```
   即 `cmd_build` 传目标时的 `cmd+=(-- "$@")` 产生的第二个 `--` 被完整保留在 payload 里（`:320-321` 只剥第一个）。**`build.sh build <target>` 语义未被破坏。**

5. **去掉 `choices=(1,2)` 没有破坏任何单元测试**——`scripts/dev/tests/test_resource_guard.py` 全部用例都依赖**默认** `--parallel=2`，`_launch()`（`:93-99`）从不显式传 `--parallel`。默认 cap 仍是 2，`test_cmake_parallel_clamped_to_two`（`:37-54`）针对的是 **payload** 的 `--parallel 8`，不受影响。作者对"抬高 `SICNU_BUILD_JOBS_CAP` 时守卫不该 argparse 报错"的诉求本身是正当的。

6. **新增的 `--parallel < 1` 拒绝有效（实测）**——`--parallel 0` → `refused: --parallel must be >= 1, got 0`，exit 2（`EXIT_REFUSED`）。

7. **G12-2 是双保险**——即使 `ctest --help` 探测失败导致 `--no-tests=error` 未被追加，`:387-392` 的 `"No tests were found"` 字符串判据仍会把空注册判成 `failed`。主手段 fail-open 被副手段覆盖，设计扎实（详见 R1-11）。

8. **`-R` 窄过滤导致假失败的场景不存在**——全仓 `run_ctest` 只有 L8 一个调用点，pattern 是 `.*`（`verification_ladder.py:193`）。0 匹配只可能意味着整个 ctest 注册为空，那正是要抓的缺陷。

9. **`collect_readiness.py` 下游解析不被破坏（已核查）**——它只读 `ladder.get("lanes")` 与 `ladder.get("finished_utc")`，不读 `overall`；新增的 `executed_items` / `planned_items` 是 additive 键，schema `exp.verification.ladder.v1` 保持兼容。

10. **`check_presets.py` 未受影响（实测）**——`check_presets: ALL PASS`，exit 0。

11. **G05-1 的三方合并安全（核查后非猜测）**——`tests/CMakeLists.txt` 共 **15640** 行；本 diff 的 hunk 在 `423-445` / `576-579` / `614-617`。`~1615` 处是 `test_spectral_indices` / `test_nodata_utils` 的 `add_executable`+`sicnu_discover_tests`（实测 `sed -n '1605,1630p'`），`~4115` 处是 `sicnu_env_gdal_driver_path` / `pi_bridge_lifecycle` 的 `add_test` 注册（实测 `sed -n '4100,4130p'`）。与最近的 hunk 相距 ≥1100 行，远超 git 默认 3 行上下文窗口 → **文本上不可能冲突**。语义上：#1483/#1484 都是**新增**测试注册，只会让 L8 的 `.*` 匹配数**增加**，不可能把 `--no-tests=error` 或 `executed==0` 判据从"通过"打成"失败"。**判定：无冲突。**
    （附带提示：这两个判据上线后，任何**减少/条件化移除**测试注册的 PR 都可能首次触发 L8 失败——这是预期行为，但建议在 PR 描述里写明，免得被当成随机红。）

12. **`target_include_directories` 的三处调用位置正确**——都排在各自 `sicnu_add_io_test(...)` 之后（`:422→443-444`、`:577→578-579`、`:614→615-617`），目标已存在，无 CMake "target not found" 风险；且 `NETCDF_INCLUDE_DIR` 为 NOTFOUND 时已在 `:434` FATAL_ERROR，不会把 `NOTFOUND` 字面量喂给 `target_include_directories`。

13. **`sicnu_add_io_test` 不提供 netcdf include 路径**（`:152-164` 只加 `src/` 和 `CMAKE_BINARY_DIR`），全仓也没有覆盖 vcpkg include 的全局 `include_directories`——这条事实同时是 R1-03 的论证基础，也说明三个已加的 `target_include_directories` 是**必要**而非冗余。

---

## 附：本轮评审实际执行的命令清单（可复核）

```bash
git status --short; git diff --stat
git diff -- scripts/build.sh scripts/dev/resource_guard.py scripts/verification_ladder.py tests/CMakeLists.txt
git show origin/master:scripts/build.sh | grep -n "resolve_jobs\|--jobs"

# R1-01
env -i PATH=/usr/bin:/bin:/tmp/g4test/bin HOME=/tmp TMPDIR=/tmp \
    SICNU_BUILD_REQUIRE_GUARD=1 SICNU_BUILD_LOG_DIR=/tmp/g4test/logs \
    bash scripts/build.sh build --build-dir /tmp/g4test/bd      # → exit 0, PAYLOAD-RAN

# R1-02
SICNU_BUILD_LOG_DIR=/tmp/g4test/logs bash scripts/build.sh build --build-dir /tmp/g4test/bd --jobs 8
SICNU_BUILD_JOBS_CAP=99 SICNU_BUILD_JOBS=8 bash scripts/build.sh build --build-dir /tmp/g4test/bd
bash scripts/build.sh build --build-dir /tmp/g4test/bd
grep -m1 "^command:" /tmp/g4test/logs/build/SUMMARY.txt          # 三种路径的真实 argv
git show origin/master:scripts/dev/resource_guard.py > /tmp/g4test/base/resource_guard.py
PYTHONPATH="$PWD/scripts/dev" python /tmp/g4test/base/resource_guard.py --lock-dir /tmp/g4test/bd --parallel 8 -- python -c "print('RAN')"   # → exit 2
python scripts/dev/resource_guard.py --lock-dir /tmp/g4test/bd --parallel 8 -- python -c "print('RAN')"                                        # → exit 0, RAN
python scripts/dev/resource_guard.py --lock-dir /tmp/g4test/bd --parallel 0 -- python -c "print('RAN')"                                        # → exit 2

# R1-03
grep -rn "netcdf.h" tests/ --include=*.cpp --include=*.h --include=*.hpp      # → 4 个 TU
sed -n '440,450p' tests/CMakeLists.txt
grep -n -A 12 "function(sicnu_add_io_test" tests/CMakeLists.txt
grep -n "include_directories" CMakeLists.txt

# R1-04
cat build-dev.agent-build.lock
python scripts/dev/resource_guard.py --lock-dir build-dev --status --json
SICNU_BUILD_GUARD_WAIT=5 SICNU_BUILD_LOG_DIR=/tmp/g4test/logs bash scripts/build.sh build --build-dir build-dev; echo $?
git check-ignore -v build-dev.agent-build.lock; echo $?         # → 1（未被忽略）
python -c "...; from dev_common import pid_alive; print(pid_alive(66716))"

# R1-07 / R1-08
bash scripts/build.sh selftest                                  # → selftest: ALL PASS, exit 0, ~2m06s
python - <<'EOF'  # --wait 2 → wall 3.75s / 报 0.5s；--wait 5 → wall 6.98s / 报 1.5s
EOF

# §3 证据复核
python scripts/check_presets.py CMakePresets.json               # → ALL PASS, exit 0
python scripts/verification_ladder.py --list                    # → exit 0
git ls-files .planning/r12-g-build-test-deploy/ | wc -l         # → 0（.gitignore:142 忽略，证据文档不入 PR）
```

> 说明：评审过程中仅在 `/tmp/g4test/` 下创建了临时夹具（stub `cmake`、`origin/master` 版 `resource_guard.py` 副本、日志目录）。`build-logs/` 下的写入落在已有的、被 `.gitignore:8` 忽略的目录中。仓库内除本报告外无任何文件改动，`git status --short` 与评审开始时一致（4 个 M + 作者遗留的 `?? build-dev.agent-build.lock`）。
