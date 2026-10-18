# R16-E T10 GOAL_LOOP_LEDGER — Build / CTest / Offline Packaging Robustness

Branch: `hardening/r16-e-t10-build-packaging` · Base: master `46791bc65` · Date: 2026-10-17
Worktree: `/home/kevin/project/exp-rs-r16-e-t10`（所有写操作仅在此树）

## 目标循环（Goal → Outcome）

每项修复遵循 RED（真实失败证明）→ GREEN（修复后通过）× 真实命令输出。

| WP | Goal | Outcome | 状态 |
|---|---|---|---|
| WP1 | #1491 `test_io_vector_contract.cpp:352` 本地裁决 | **不复现（本地+CI 双重证据）**：本地 14/14 通过；基线 SHA 的 CI Tier1 run 38843229045 中该套件同样 14/14 通过（0 失败），CI 侧 56 个失败全在其它套件 | DONE（无代码改动，结论型） |
| WP2 | 测试注册守卫强化（#1494 复核+盲区） | static census GREEN（1007 文件 / 0 孤儿）；修复盲区：子目录 `test_*.cpp` 不可见（RED: 合成孤儿 exit 0 → GREEN: exit 1）；census 递归化 + 子目录 CMakeLists.txt 纳入 + basename stem 匹配 | DONE |
| WP3 | 离线交付完整性审计 | conformance 18/18 GREEN 起步；同尺寸篡改反例被 sha256 逮住（真实检测）；**修复真实缺陷：可执行位不记录不校验**（FAT32/zip 剥位后 verify 仍 PASS）→ additive `exec` 字段，conformance 20/20 | DONE |
| WP4 | lupdate 范围行追加 `src/python`（cross-track spec） | 一行 diff；lupdate 6.12.0 实测 +1 context（SicnuPythonConsole）/ +1 string，无其它变化 | DONE |
| WP5 | 资源治理与 preset 一致性 | check_presets.py ALL PASS；**修复盲区：binaryDir 唯一性未断言**（RED→GREEN）；fixture 无界挂起类已全闭环（4 个 server 全有界） | DONE |
| WP6 | ci.yml 只读复核 + master CI 快照 | #1490/#1495 修复在位（只读确认）；快照：最近 25 push run = 10 cancelled + 15 completed-failure；基线 run = Tier1/Tier2 failed-test + macOS failed-build；gate FAIL（证据存 evidence/） | DONE（只读） |

## 关键裁决记录

1. **#1491 :352 = 不复现**。:352 属 TEST_CASE "unknown fields and malformed geometry are
   structured errors"（`tests/test_io_vector_contract.cpp:339`，:352 是其 driver-missing 分支）。
   本地（GDAL 3.13.3 / gcc 16.2.1 / Debug）与 CI（Tier1 run 38843229045）都全绿。
   #1491 的另一半（CI 无完成结果）由 #1490 修复覆盖——现在 run 能完成；当前失败是
   56 个**其它**套件（remote_validator/surface_snapshot/capability parity/help drift/
   workflow crash recovery 等），需按归属另行跟踪（见协调项）。
2. **WP3 可执行位**是审计中唯一"真实缺陷修复"：传输通道剥位 → 字节全同 → 摘要全绿
   → bundle 却跑不起来。修法是 fail-closed 单向的 additive 字段（声明了才校验；
   legacy /1 与 Windows 构建的 bundle 验证行为不变）。
3. **不修项**（记录不动）：PS 孪生对 `data/../manifest.json` 别名条目不拒（canonical 拒）
   ——无 pwsh 无法测试修复，记为环境限制；appimage 打包未实跑（按指令太重，静态审计）。

## 环境/资源约束遵守

- 编译 `-j4`（CMAKE_BUILD_PARALLEL_LEVEL=4），ctest `-j1` + QT_QPA_PLATFORM=offscreen。
- 共享锁 `/home/kevin/project/exp-rs-r16-build.lock` 每次 configure/build 均持锁。
- 无 ccache；GDAL/Catch2 依赖离线解析（pwb-sdks prefix + exp-rs-fdeps 缓存）。
