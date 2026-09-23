fix(teaching-foundation): fail-closed LabSpec runtime, v3 gate parity, hostile-shape hardening — hardening 13/20 (curriculum-lab-recipes-foundry)

## 目标

本 PR 是教学底座（curriculum harness / LabSpec v1-v3 runtime / recipe
compiler / sample foundry / lab packs & grading fixtures）的可靠性强化
slice：只修既有模块的缺陷、门禁漂移、测试盲区与确定性缺口，不做新方向。

## Recon 基线

启动时 master `a9dc33fa7`（#1236 刚合并）。本 track 覆盖的模块做全量
recon（调用/所有权图 + 现状矩阵见
`docs/development/hardening/curriculum-lab-recipes-foundry/01-recon.md`），
确认 16 项疑点（S1–S16），其中 14 项在本 PR 修复或记录，2 项归位。

## 与并行任务的去重（重要）

- **#1246（master 已合并）做了同一件事的接线部分**：src/recipes 的
  CMake target、全部孤儿测试注册、wiring-drift oracle。本分支 rebase 到
  abc07b715 后**丢弃了重复接线**，只保留新增的
  `test_teaching_foundation_e2e` 注册。经 `git diff
  a9dc33fa7..origin/master` 核对，master 的推进**未触碰本分支修复的任何
  源文件**——下述全部行为修复仍然新颖、仍然需要。
- #1259（teaching cockpit）/ #1260（teaching_admin）已合并：本分支未触碰
  `src/teaching/**`、`src/app/teaching/**`、`src/teaching_admin/**`。
- `agent/flash-lab-foundry-determinism` 只作线索矿：其 hostile-env 修复
  master 已具备；**仍 live 的缺口是 GDAL pin 不恢复现场**（S13）——本分支
  以 RAII 修复并补 ADR 0164 条款，其余不移植。
- `rs14-unified-verifier` 未触碰；`src/verify` 仍是唯一权威。
- open issues：0（启动时与提交前各核对一次）。

## 修复清单（每个行为修复绑定一个 RED oracle）

| # | 缺陷 | 修复 | Oracle |
|---|------|------|--------|
| 1 | `spec_runtime` "parsing never throws" 被 4 处未守卫的 `asString()` 打破（prompt_zh/objective_zh/text_zh/choices），对象值直接 `Json::LogicError` 穿透 | 全部改为 typed `lab.runtime.*` 拒绝 | RED：预修二进制 `unexpected exception: Type is not convertible to string`（test_lab_runtime） |
| 2 | `expected_numeric` 非数值/缺失边界静默取 0.0（判分基线被篡改）；hint level 非整数静默塌缩为 1 | typed 拒绝；双边界必须为数值；缺省 level=1 契约保留 | RED：4 个断言失败 |
| 3 | session envelope 只在顶层拒绝 unknown key，嵌套条目可夹带任意字段过 load/save | 每层词表镜像 `sessionToJson`；含 checkpoint evidence 两层注入用例 | RED：5 个断言失败 |
| 4 | `session_store::create()` 对任意长序号 strtoll（饱和溢出 UB）；lab 目录被文件占位时静默复用 seq 1 | 18 位上限（与读路径一致）+ typed `lab.session.io` | RED：20 位序号文件 → 非预期失败 |
| 5 | `recordToolUse` 接受调用方伪造 seq（破坏 latest-wins 门语义） | 台账统一由 `nextSeq()` 分配 | RED：`500 == 1` |
| 6 | Windows save 无 fsync 对应物（头文件契约声称 old-or-new） | rename 前 `FlushFileBuffers`（仅编译验证） | P3 记录 |
| 7 | `check_lab_registry.py`/`check_curriculum.py`/`curriculum_catalog` 卡死 v1/v2，与 loader/schema 的 v3 矛盾——合法 v3 lab 被门禁拒绝、被 curriculum 路由为 unknown | 三处全部接受 v3；`runtime` 键 v3-only + python 侧 shallow 检查（深校验仍在 sicnu_lab_runtime） | RED：旧脚本对 v3 报 3 项违规；新脚本放行 |
| 8 | wrapper 与 registry 的 grading 指针双源无一致性校验——指向另一个存在文件时全门禁绿灯 | pointer-parity 检查 | mutation kill：fork 指针被抓 |
| 9 | `gen_lab_packs.py` generated 条目按本地文件存在性决定 bytes pin——pack 输出是机器的函数 | generated 条目永不 pin bytes（与 committed 状态一致；verifier 缺 bytes=不检查）；committed fixture 缺失改为 loud fail | property：本地存在生成文件时 write 模式零 diff |
| 10 | foundry 的 GDAL env pin 不恢复（库形态泄漏进宿主进程） | `ScopedConfigOption` RAII 恢复（含 unset 方向）；ADR 0164 append-only 增补 | RED：`"NO" == "YES"` |
| 11 | recipe registry/validator/lookup/sidecar 扫描面对非字符串 schema/recipe_id/stage kind/hook kind/modality 直接 `asString()`/`asBool()` 崩溃 | `stringOrEmpty`/`isString` 守卫 → typed 诊断 | hostile-shapes 套件（registry 4 形态 + lookup modality 形态） |
| 12 | **test_labspec 在 master 上两个独立原因失败**：(a) loader 测试仍断言 v3 被拒（ADR 0174 已接受 v3）；(b) #1190 提交了互相漂移的 README/lab16 对 | 测试改为 v4 + pinned 消息；README 重新生成 | 两处均为 master 现状直接失败 |
| 13 | **test_curriculum 的 shipped-manifest 用例在 master 上必失败**：`SICNU_TEST_SOURCE_DIR` 无任何注册定义（回退 "."，仅源码根 cwd 下碰巧过） | 注册处补 define | 直接失败 → 通过 |
| 14 | 新增纯底座 E2E：foundry 生成（seed+manifest 校验）→ 读取 lab（registry 解析 wrapper）→ recipe 编译/校验/字节漂移门 → pack/grading/pipeline 引用可解析（含 corpus sha256 抽查）→ v3 runtime session 过 gate checkpoint（真 FileProbe）→ 持久化/漂移拒绝 | `test_teaching_foundation_e2e`（Qt-free，不碰 UI） | 新增，211 断言 |

## 测试证据

- 16 个测试套件 **连续两遍全部通过**（明细见 02-test-ledger.md），
  包括 master #1246 的 `test_build_wiring_drift`。
- RED 证据：S1-S16 中每个行为修复都在预修实现上以可执行/可复现方式失败
  （记录见 02-test-ledger.md；python 门禁的 RED 用 master 版脚本+探针转录）。
- 门禁：`check_lab_registry.py --strict-data` ok ·
  `check_curriculum.py` OK · `gen_lab_packs.py --check` in sync ·
  `gen_lab_docs.py --check` 17 in sync。
- 新增 warning：无（构建日志仅仓库预存的 Qca-qt6 include 警告）。

## 独立对抗 review

两轮独立 reviewer（第一轮 16 项发现 + 复验轮）。已修：P0-1（对 #1246 的
重复接线——rebase 丢弃）、P1-1（lookup modality 崩溃逃逸）、P1-2（E2E
discovery 缺 sanitizer LSAN 属性）、P2-1/P2-2/P2-3、P3-3。其余 P3
（诊断族混用、ScopedConfigOption 对"显式空串"与"未设置"不区分等）记录于
review 记录，不改行为。

## 已知限制 / 未做事项分类

- **已被其他 track 拥有**：`src/teaching/**`（#1259 已合并）、
  `src/teaching_admin/**`（#1260）、science_context 的 recipe router
  消费面（#1262）。
- **无法复现**：Windows `FlushFileBuffers` 运行时行为（本 lane 无
  Windows 宿主；仅编译验证 + 模式审查）。
- **明确未来方向（不实现）**：`data/labs/data-specs/*.json` 的机器消费者
  （S10，声明的未来层）；sicnu_lab_runtime 的生产 UI 消费者（底座 E2E
  已证明可消费）。
- **记录不修**：三份 SHA-256 实现的跨模块收敛（S12，Qt-free/Qt 依赖世界
  不同）；`lab_source.cpp loadRegistry` 坏 registry 的静默空 view
  （fail 方向正确，诊断归因可改进）。

## 回滚方案

除 `docs/labs/README.md`（按生成器再生成）与两个 pack 无关外，全部改动
可按 commit 粒度 revert；无 schema/major 版本变更（`sicnu.lab-session/1`
、`sicnu.lab.rules/1`、`sicnu.lab-pack/1`、`sicnu.scientific_recipe/1`
、LabSpec 1/2/3 全部原样，append-only 语义未破）。

**Online CI not awaited. 不自动 merge。**
