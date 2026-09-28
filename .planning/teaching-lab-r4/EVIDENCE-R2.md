# EVIDENCE — Track 12 Round 2（收尾轮验证证据，2026-09-28 实测）

## 0. 分支与提交

- 分支 `hardening/r4-teaching-lab-r2`（worktree `exp-rs-teaching-lab-r4`），基线 `origin/master = a726d17a62`。
- 5 个原子提交（每个独立可编译）：
  1. `7999b5ad8a` fix(tests): 删除 tests/CMakeLists.txt:14728 孤立 `=======` 冲突分隔行（master 全仓 configure 破损恢复）
  2. `787ed8c0ed` fix(tests): cockpit smoke 4 处钉译文改钉 tr() 英文源串
  3. `29cabb6da4` fix(tests): packs-in-sync 门重定向/tail 路径错配修复 + 误入库的 stdout 产物删除
  4. `52b4e4ba55` refactor(teaching_admin): canonical file digest 委托 lab-pack 权威（含 bytesOut null-guard 缺陷修复）
  5. `12c44a4fa2` test(lab-pack): PackVerifier 直测面 6 用例
- 净变更：7 个代码文件（tests/CMakeLists.txt、test_teaching_cockpit_smoke.cpp、test_lab_data_pack.cpp、lab_pack.h、lab_pack.cpp、json_util.h、test_lab_pack_boundaries_r4.cpp）+ 误入库产物删除 1 个 + planning 工件 2 个。

## 1. 基线构建（43 过滤面目标）

- `/tmp/r2_baseline_build.log`：1378 边全跑完，`BUILD_RC=0`，0 条 FAILED。
- **#1335 恢复实证**：round-1 的 19 个 NOT_BUILT 目标二进制全部产出（含 test_lab_data_pack——master 既有编译破损已被 #1335 修复）；过滤面用例数从 round-1 的 148 实跑增至 **266**。

## 2. 基线红绿分布（改源码之前，`/tmp/r2_baseline_ctest.log`）

- **266 用例：259 过 / 5 失败（98%），rc=8。5 个失败全部 master 既有、全部在本轨白名单内**：
  - 4 × `test_teaching_cockpit_smoke`（:307/:462/:755/:792）——钉 tr() 译文/按译文查控件。该目标自断言编写起就在 19 个 NOT_BUILT 集合里从未实跑，#1335 复活后首跑即暴露；#1336 的全仓 i18n 扫描看不见从未运行的目标。产出方 `src/app/teaching/`（白名单外）的 tr() 源串为英文——修复按 docs/i18n.md 规则落测试侧。
  - 1 × packs-in-sync 门（test_lab_data_pack）——**门自伤**：重定向写 cwd、tail 读 `${TMPDIR:-/tmp}`，`&&` 链 rc 永远取自 tail（找不到文件=1），生成器"packs in sync"（rc 0，sink 文件实证）从未到达断言。该目标同为 #1335 复活后首跑。

## 3. 修复后验证

- 重建：`/tmp/r2_verify_build.log` 1378 边 `BUILD_RC=0`、0 FAILED（含两个修复提交的实编）。
- 委托后受影响目标：57/57 绿（boundaries 23 + admin_core + data_pack，含 48/48 仓库 pin 跨语言对账不变）。
- **终验双跑：`ctest -R "lab|teaching|pack|copilot|autonomy" -j1 --timeout 900` 连续两轮 RUN1=0 / RUN2=0，272/272 = 100%**（266 基线 + 6 新 PV 直测用例）；相对基线零新增失败、5 个基线红全部转绿。日志 `/tmp/r2_final_ctest1.log`、`/tmp/r2_final_ctest2.log`。

## 4. 遗留5 委托收口（52b4e4ba55）

- lab_pack.h 公开 `canonicalFileSha256(path, bytesOut=nullptr)`；实现移出匿名命名空间（内容不变）。
- json_util.h 两入口改薄委托；`detail::canonicalFileDigest` 镜像与其过时 MERGE-ORDER NOTE 删除；chunkBytes 参数删除（全仓无调用方传过）。
- **顺带真缺陷**：权威函数 `*bytesOut = total` 从未判空——verify() 内部调用点恒传实指针，digest-only 路径从未被执行，委托包装首次走到即 SEGFAULT（gdb 取证：`canonicalFileSha256(path=…/empty.bin, bytesOut=0x0)`）。已判空修复。
- CHUNK-1 fixture 重对齐：镜像"8000 字节头 + 64KiB"分块 vs 权威"offset 0 起 64KiB"——跨块 pending-CR 边界相差 8000 字节，fixture 改钉权威分块。
- 消歧结构保证：admin 与 agent 的 canonical 裁决从此由同一实现产出，Windows CRLF 检出下不可能再分叉（round-1 P1-1 的终局收口）。

## 5. 遗留3 PackVerifier 直测面（12c44a4fa2）

6 用例（29/29 目标内全绿）：PV-B01（CRLF 对 LF pins verified + verifiedBytes 字节精确）、PV-B02（孤 CR）、PV-B03（BOM 正/负两态；负例 typed——committed 先查字节量后查摘要，size_mismatch 即"BOM 是内容"的证明）、PV-CHUNK（权威自身 64KiB 跨块）、PV-B15（等长摘要漂移 → checksum_mismatch 点名资产）、PV-B06（缺失 → input_missing 点名资产）。

## 6. 资源纪律与环境异常存档

- 全程 -j2（RSS 峰值约 55%，未触发降 -j1）；CTEST_PARALLEL_LEVEL=1；GUI 用例 QT_QPA_PLATFORM=offscreen；未等待线上 CI。
- subagents：仅独立评审 1 个（另 2 槽未用）。
- **环境异常（如实入档）**：本构建目录存在 AUTOMOC/restat livelock——`.ninja_deps` 对部分边的记录停在 round-1 时代，边真实执行且 rc=0、产物正确，但同输入重编持续重新排队（`ninja -d explain` 取证：stored deps 1790457… vs 实际 1790553…；单目标构建后立即 dry-run 仍列同 4 边）。cmake/构建机制在白名单外，无法在本轨修复；"ninja -n 零待跑边"门禁在该环境不可达，验证口径以 rc=0 + 零 FAILED + ctest 双跑为准。所有二进制均为当前源码的真实编译产物（含两个测试修复与委托改动）。

## 7. 未解决项（移交）

1. `cmake/raise-compiler-stack.sh` 假绿与上述 deps livelock：机制层问题，白名单外（cmake/），移交基础设施侧。
2. round-1 遗留 #1（同一脚本）维持移交状态。
3. admin/agent 其余 verdict 面如发现第二实现，按本轮先例委托收口（未发现新镜像）。
