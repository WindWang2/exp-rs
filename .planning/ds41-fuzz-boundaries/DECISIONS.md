# DECISIONS — Track `ds41-fuzz-boundaries`

## D1 — 新 mutator 放新文件，不改 `tests/support/bounded_fuzz.h`

`bounded_fuzz.h` 是 Verification 7.0 起多 Track 共享的生成器；并行 Track（若有）可能正在
改它。本 Track 的扩展需求（结构化 JSON mutator、路径语料、frame 截断、delta-debugging
最小化器、corpus 记录器）全部放 `tests/support/fuzz_corpus.h`，只 include 不改共享文件。
代价：两套生成器并存；收益：零合并冲突面，且 `bounded_fuzz.h` 的既有语义对既有 suite
保持字节不变。

## D2 — 三个独立 target 而非一个大 target

`test_contract_fuzz_frame` / `test_contract_fuzz_payload` / `test_contract_fuzz_paths`
分开，而不是一个巨型 fuzz target：
- 失败归因更直接（ctest -R 即定位 lane）；
- 三个 target 闭包相同（Catch2 + sicnu_sdk），编译成本不变；
- 与既有 `test_contract_fuzz_{io,ipc,data,lang,agent,ops}` 的命名域一致（每个入口族一个 target）。

## D3 — 输入生成策略：directed fragment + 类型变异 + 深度/尺寸炸弹，不用覆盖引导

仓库既有风格（`test_contract_fuzz_*.cpp`）是"固定 seed + directed fragments + 硬 cap"，
CI-safe、可复现，且明确"不做公网 fuzz"（Track 非目标）。本 Track 沿用该策略并增加
三类结构化 mutator（来自 Track 必做工作包 2 的清单）：
- `jsonMutateTypes`：按 key 清单把值替换为错类型（string↔number↔bool↔array↔object↔null）。
- `jsonDepthBomb` / `jsonSizeBomb`：受控深度/尺寸输入，验证 cap 与 stackLimit 行为。
- `pathCorpus`：traversal/绝对/盘符/UNC/Unicode/尾分隔符/NUL 形态。

## D4 — corpus 最小化用 ddmin，谓词用"用例自己的 oracle"

每个 fuzz lane 的形式统一为 `std::optional<std::string> oracle(input)`（返回失败原因），
循环 `REQUIRE(!oracle(x).has_value())`。ddmin 以该 oracle 为谓词做 1-minimal 化，
保证"最小 fixture 仍复现失败"。记录器（`FaultRecorder`）默认不写盘；仅在
`SICNU_FUZZ_CORPUS_DIR` 设置时落盘，避免测试日常运行污染仓库。已固化的高价值 case
放入 `tests/corpus/<area>/` 并由 Catch2 已知答案腿断言（regression promotion）。

## D5 — 被证明的 P1/P2 缺陷：最小修复 + regression，不开 issue

OWNERSHIP 例外条款允许极小生产修复。三个候选（`plugin_ui_schema.cpp` 的
`commandId`/`contributionId` 错类型 asString、`plugin_manifest.cpp:83` 的
`required` asBool）修复方式统一为"**先类型检查再转换**"，即把既有自述契约落到代码：
改动 ≤10 行/处、无行为变化（合法输入路径不变）、无 API/ABI/序列化变化。
若 fuzz 证明的缺陷超出该量级 → 开 issue + 留 corpus，不修。

## D6 — Windows `cmd.exe` 不可用的编译驱动方式

本会话沙箱中 `cmd.exe /c` 只打印 banner 不执行参数；构建通过 PowerShell 驱动
（`vcvars64.bat` env 导入 + 直接调用 `cmake`/`ninja`）。脚本 `.trackbuild.ps1` 为
worktree-local 临时文件，不提交（见 OWNERSHIP）。

## D7 — sanitizer lane 只编译精选 target

仓库 `sanitizer-debug` preset 是全量 instrumentation。全量 sanitizer 构建代价过高
（QGIS/OTB/ITK/GDAL 全链），且本 Track 的 target 闭包是 jsoncpp-only 静态库 → 采用
"同一 preset 配置 + 只 build 精选 target"的方式，保持仓库支持的 sanitizer 语义
（`ENABLE_SANITIZERS=ON` 的编译/链接选项原样生效）而控制代价。MSVC 仅支持 ASan
（仓库 CMake 对此有注释）；UBSan 覆盖记入 Known limitations。

## D8 — ledger 不进 PR

`.goal-loop-ledger.md` 按 Track 指示"默认只留在 worktree"。`.planning/<track>/*.md`
按仓库既有白名单约定提交（markdown only）—— 这与最近 30+ 个 Track 的实践一致。
