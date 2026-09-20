# ORACLES — ds41-capability-help-sync

客观完成条件与验证命令。全部为本地可复现命令；执行环境离线，不依赖线上 CI。

## O1 — live registry 与公开 capability/help/toolbox 集合无未解释漂移

```powershell
# worktree: WT=C:\Users\wangj.KEVIN\projects\exp-rs-worktrees\ds41-capability-help-sync
# 新测试（本 Track 新增）：surface parity
$env:QT_QPA_PLATFORM="offscreen"
& "$WT\build-cap\test_capability_surface_parity.exe"      # exit 0
& "$WT\build-cap\test_algorithm_meta_drift.exe"           # exit 0
& "$WT\build-cap\test_capability_knowledge.exe"           # exit 0
```

判定：三个测试全部 exit 0；任何未解释漂移（registry/sidecar A/sidecar B/help operator 主题/CLI algorithms list/MCP list_algorithms 名空间集合不一致）导致 FAIL 并给出生成命令。豁免必须是测试内显式枚举（id + reason），不允许魔法数字。

## O2 — generator 幂等：连续运行两次第二次零 diff

```powershell
& "$WT\build-cap\capability_knowledge_tool.exe" gen-meta  "$WT"
& "$WT\build-cap\capability_knowledge_tool.exe" gen-pages "$WT" --check
# 记录 git status；再跑一遍：
& "$WT\build-cap\capability_knowledge_tool.exe" gen-meta  "$WT"
& "$WT\build-cap\capability_knowledge_tool.exe" gen-pages "$WT" --check
git -C "$WT" status --short data/processing pi/knowledge   # 第二遍后必须为空
```

判定：第二遍 gen-meta 后 `git status` 对 `data/processing` 与 `pi/knowledge` 零输出；`gen-pages --check` 输出 `zero diff`。Layer-A 同理用 `sicnu_geo_rs_cli --export-catalog` 到临时目录两次比对字节一致。

## O3 — 篡改 fixture metadata 时 drift test 失败，恢复后通过

```powershell
# 1) 篡改一个 sidecar（如删除 rs-ndvi.json 的 summary 或改一个 derived 字段）
# 2) test_capability_knowledge / test_algorithm_meta_drift 必须 FAIL
# 3) git checkout 恢复后必须 PASS
```

判定：FAIL→PASS 双向实证，输出留档 EVIDENCE.md。

## O4 — CLI/MCP/Pi 的 schema/name projection 关键样本一致

- MCP `get_algorithm_schema` 返回的 input_schema 与 CLI `--schema <op>` 的算子 schema 在参数名/类型/默认值上一致（关键样本 ≥5 个算子，含 1 个 tolerance 分级算子）。
- MCP `list_algorithms` id 集合 == `AtomicAlgorithmRegistry` rs: 投影集合 == sidecar B id 集合（exemption 显式）。
- CLI `algorithms list --json`（子进程）id 集合与 registry 投影一致。
- Pi 默认 bridge 类别 ⊆ projection 类别（已有 gate，test_surface_parity — 回归不破坏）。

## O5 — 完整性 gate

```powershell
& "$WT\build-cap\test_capability_completeness.exe"   # exit 0
```

判定：每个一等 rs: 能力（非显式豁免）具备非空 summary、非空 failure_modes、非空 io.outputs、io.inputs 非空或在豁免清单内且带原因；units/NoData 系统性缺口以 WARN + known limitation 登记（不编造）。

## O6 — 关键 gate 连续两遍通过

O1 的三个测试 + O5 连续运行两遍，两遍全绿（同一天内、同二进制）。

## O7 — 独立 review P0/P1 清零，独立 PR 已创建

- 与实现角色分离的只读 reviewer 对 `origin/master...HEAD` 深审（覆盖正确性/边界、线程与 Qt 对象生命周期、取消/失败/异常路径、数值与科学语义、API/ABI/序列化兼容、Windows/Linux/macOS 可移植性、性能内存、安全边界、测试能否先红后绿、与并行 PR 冲突）。
- PR body 含：baseline SHA、去重结果、范围/非范围、设计、测试命令与两次结果、资源限制、review 处置、known limitations、冲突热点。
