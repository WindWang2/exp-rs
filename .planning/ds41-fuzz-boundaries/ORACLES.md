# ORACLES — Track `ds41-fuzz-boundaries`（客观完成条件）

每个 Oracle 都有可复现验证命令；判定只用退出码/输出事实，不用主观判断。

## O1 — 精选 parser/IPC/path 入口对 corpus smoke 零 crash/UB

**判据**：以下 CTest target 全部通过（exit 0），且每个用例迭代都带硬 cap
（输入 ≤512B、迭代 ≤600/seed、固定 seed）：

```
ctest --test-dir build-dev -R "^(test_contract_fuzz_frame|test_contract_fuzz_payload|test_contract_fuzz_paths)::" --output-on-failure
```

- `[fuzz][ipc]` framing lane：`IpcFrame::writeJson`/`read` 在截断/超限/EOF/timeout/pending
  续传语料上 totality 成立（无 throw、无 Ok 误判）。
- `[fuzz][payload]` lane：`decodeEnvelope`、`validateWorkflowDocument`、
  `migrateWorkflowDocument`、`validatePluginUiSchema`、`validateUiEvent`、
  `PluginDiagnostic::fromJson`、`PluginPackage::versionSatisfiesRange` 的 totality 成立。
- `[fuzz][paths]` lane：`PathPolicy::checkRelativeLexically`/`checkPayloadInsideRoot`/
  `resolvesInsideRoot` 的 containment cage 性质成立（独立 oracle：`std::filesystem`
  重新解析并与 canonical root 比较）。

## O2 — 合法 round-trip property 与非法 typed-reject property 稳定

**判据**：同一命令连续两遍全绿（见 O7），且：
- round-trip：builder 输出喂给自家 parser 必须 Ok 且语义等价
  （frame payload 字节相等；envelope 逐字段相等；valid workflow/ui schema 的
  `normalized` 与输入 deep-equal；`migrateWorkflowDocument` 幂等）。
- typed-reject：非法输入必须 `false`/`!ok()` **且** diagnostics/errors 非空
  （禁止"沉默失败"与"异常逃逸"两条都不允许）。
- 拒绝路径无副作用：`validatePluginUiSchema` 失败时 `normalized` 为 null；
  拒绝的 package install 不留下安装目录（若实证）。

## O3 — mutator 能击中若干人工注入/历史缺陷模式

**判据**：至少 4 个 directed case 被 corpus 击中并断言：
1. IPC 长度前缀高位符号扩展（128–255B 帧，`test_exprs_ipc.cpp` 既有腿的 mutation 版本）。
2. envelope `v` 错类型/错 major → E6001 typed reject。
3. UI schema `commandId` 非字符串 → **必须在 fix 前抛 `Json::LogicError`**（历史缺陷模式），
   fix 后转为 typed reject（单测同时钉住两侧：`REQUIRE_NOTHROW` + `!ok()` + errors 非空）。
4. `ManifestPort::fromJson` 中 `required` 为字符串 → fix 前抛异常，fix 后 typed reject。
5. path 遍历链 `../../..`、绝对路径、空串、Unicode、尾部分隔符 → 拒绝且原因正确。

## O4 — 发现的 unresolved production defect 都有 minimized corpus 与证据

**判据**：`tests/corpus/<area>/README.md` 记录每个 defect 的：最小 fixture（1-minimal，
由 delta-debugging 得出）、root cause 文件:行、命中命令、修复 commit。若 fuzz 证明
某缺陷，本 Track 内修复（OWNERSHIP 例外条款），PR body 标注 `Fixes` 无（不开 issue，
直接 PR 修复 + regression）。

## O5 — Sanitizer lane

**判据**：以仓库既有 preset 配置 sanitizer 构建（本地，非 CI），只编译精选 target：

```
# 配置（worktree 内，一次性）
cmake --preset dev-default -B build-san -DENABLE_SANITIZERS=ON ...
# 编译精选 target（-j1）
ninja -C build-san -j1 test_contract_fuzz_frame test_contract_fuzz_payload test_contract_fuzz_paths
# 运行
ctest --test-dir build-san -R "^(test_contract_fuzz_frame|test_contract_fuzz_payload|test_contract_fuzz_paths)::" --output-on-failure
```

MSVC ASan 报告作为证据附在 PR（预期零报告；任何报告不隐藏，逐条处置）。

## O6 — 既有相关 suite 无回归

**判据**（本 Track 触及测试基础设施与 SDK 解析器时的回归门）：

```
ctest --test-dir build-dev -R "^(test_exprs_ipc|test_exprs_plugin_system|test_exprs_workflow_schema|test_plugin_ui_schema|test_plugin_ui_schema_host|test_plugin_manifest|test_contract_fuzz_ipc)::" --output-on-failure
```

全部 exit 0。

## O7 — 关键 gate 连续通过两次

**判据**：O1+O2 的命令连续运行两遍，两遍 exit 0 且用例数一致（无 skip 抖动）。

## O8 — 独立 review P0/P1 清零

**判据**：reviewer subagent（与实现角色分离，read-only，`origin/master...HEAD`）的
finding 清单中 P0/P1 = 0；P2 能修则修，不能修的写入 PR `Known limitations`。
reviewer 报告存 `.planning/ds41-fuzz-boundaries/REVIEW.md`。

## O9 — PR 已创建

**判据**：`gh pr create` 成功，输出 URL；PR body 含 baseline SHA、实时去重、范围/非范围、
设计、测试命令与两次结果、资源限制、review 处置、已知限制、冲突热点。
本 Track **不 merge**、不等 CI。

## 资源约束（所有命令遵守）

- `-j1`（必要时 `-j2`）编译；`ctest -j1`。
- 不反复全量重编 QGIS/OTB/ITK：只编译本 Track 的 target 及其闭包（sicnu_sdk 为静态 jsoncpp-only 库，闭包极小）。
- 不把 fuzz 目标加入长跑超时路径：TIMEOUT ≤ 300，iteration cap 固定。
