# R15-E REVIEW ROUND 2

## 0. 独立性声明（必须先读）

任务书要求「至少两轮：Round 1 按 P0/P1/P2/P3 + 行号给出问题；实现者修复；Round 2 重新跑适当测试并签署报告」，
并且明确禁止「把同一实现者伪装成独立 Reviewer」。

本轮的事实是：

- 我**启动了**第二个独立会话的 reviewer agent（同一提示框架，未参与编写），要求它复核 Round 1 的 9 项修法、
  逐一推演六种输入、并检查新增的 `lab_pack` 修复。
- **该 agent 在本会话时限内没有返回报告**（此前同一宿主上已有两次 agent 因 429 频率限制失败）。
- 因此本文件**不是**独立审查报告，而是**实现者自查**。它不能替代独立 reviewer；把它当作 Round 2 的
  独立签署是不诚实的。

**结论：Round 2 的「独立」要求未满足。** 已如实记录在此，并把可自动化的部分（六种输入推演、两轮测试复跑）
做成了可复核的证据，供真正的人类 reviewer 或下一次独立会话接手时直接核对。

## 1. 六种输入推演（Round 1 P1 修法是否真的闭环）

针对 `src/teaching_admin/batch_assessment.cpp` 的 `recordSkip` 记账唯一性，逐路径推演：

| # | 输入 | 学生目录 pass | 根级扁平 pass | 记账次数 | 判定 |
|---|---|---|---|---|---|
| 1 | 根级隐藏目录 `.张三/` | `entryRefused` 不命中（是目录、非链接）→ `isHidden() && isDir()` → **recordSkip(studentId=".张三")** → continue | `!isFile()` → continue（不记账） | **1** | ✅ |
| 2 | 根级隐藏文件 `.x.tif` | `isHidden() && isDir()` 为 false → `!isDir()` → continue（不记账） | 非链接且 isFile → 通过首关 → `isHidden()` → **recordSkip(studentId="")** | **1** | ✅ |
| 3 | 根级隐藏符号链接 `.lnk` | `entryRefused` → `isSymLink()` → **recordSkip("symlinked entry refused")** | `isSymLink()` → continue | **1** | ✅（reason 取更具体的「链接」而非「隐藏」，两者都真） |
| 4 | 学生目录内隐藏文件 `王五/.hw.tif` | 内层：`entryRefused` 不命中 → `isHidden()` → **recordSkip(studentId="王五")** | 不经过（扁平 pass 只看根级） | **1** | ✅ |
| 5 | 学生目录内嵌套目录 `王五/sub/` | 内层：`entryRefused` 不命中（是目录）→ `isHidden()` false → `!isFile()` → **recordSkip("nested directory refused")** | 不经过 | **1** | ✅ |
| 6 | 学生目录内符号链接 `王五/leak.json` | 内层：`entryRefused` → `isSymLink()` → **recordSkip** | 不经过 | **1** | ✅ |

补充对照（不应被记账的）：
- 根级可见普通文件 → 只进扁平 pass，变成 `items`，**0 次 skip** ✅
- 根级可见目录 → 只进学生目录 pass，其内文件变成 `items`，**0 次 skip** ✅
- 学生目录内可见普通文件 → `items`，**0 次 skip** ✅

结论：六种异常输入各记 **1** 次，三种正常输入各记 **0** 次；不存在 0 次或 2 次记账的路径。
自动化护栏：`test_r15_E_batch_refusal_accounting` 的 `skipCount(path) == 1`（隐藏目录、根级隐藏文件两例）
+ `skips.size()` / `items.size()` 的组合断言。

## 2. 其余 Round 1 修法的自查

| Round 1 项 | 自查结论 |
|---|---|
| `isFailureStatus` 加 `"fail"` | 词表覆盖 `failed/failure/fail/error/aborted/canceled/cancelled`；测试遍历 7 种拼写（含大小写与前后空白） |
| `overall_percent` 按 labId 去重 | 用 `std::map<std::string,bool>` 的 `emplace` 取首次出现；同一 lab 在两会话模块中状态一致（同源于 `completed_lab_ids`），无二义；新增「同一 lab 出现在两个模块 → 100%」用例 |
| clamp 用例可达 | 用 `modules: []` 的清单使 `labsTotal == 0`，从而走「报告值夹取」分支，断言 `105 → 100`、`-7 → 0` |
| 非对象 row 拒绝 | `!rv.isObject()` → `error` + `false`；用例 `rows:[{...},42]` |
| warnings 串字 | 两个隐藏分支都 `clear()` |
| 详情面板观测值 | `formatProgressObserved()` 走 `observedText()`；`rs_job_panel.cpp` 内**不再有**别处直接拼百分比（列表两处 + 详情一处，全部走新头文件） |
| 子行偏好清理 | 删除父行前遍历子行移除 `m_loadToMain` |
| `lab_pack` 前导分隔符 | 见 §3 |

## 3. `lab_pack` 新增检查的误伤面

新增 `input.path.front() == '/' || '\\'` 的拒绝。逐一检查合法输入：

| 合法形态 | 是否被误伤 |
|---|---|
| `fixtures/a.tif`、`a.tif` | 否 |
| `sub/dir/b.tif` | 否 |
| Windows 盘符相对 `C:foo.tif`（无分隔符开头） | 否（本检查不命中；它也不被 `is_absolute()` 命中，但 `hasDotDotComponent` 与 canonical 包含性检查仍未覆盖此形态 —— **已知残留**，需要 canonical 解析成功才会被后续检查抓住） |
| UNC `\\server\share\x` | **命中**（以 `\` 开头）→ 被拒绝。这是**期望行为**：pack 输入必须是 pack 相对路径，任何绝对/UNC 形式都不合法 |
| 盘符绝对 `C:\x`、`C:/x` | `is_absolute()` 已命中 |
| POSIX 绝对 `/etc/passwd` | **本次修复的目标**，之前漏判 |

残留风险（如实列出）：`C:foo`（盘符相对、无前导分隔符、无 `..`）不会被词法检查抓住；它依赖
`canonical(root / "C:foo")` 的解析结果。本轨**未**改成「canonical 解析失败即拒绝」，因为那会把
「文件不存在」误报成「越界」（`lab.pack_input_missing` → `lab.pack_input_outside_root`），
与审计建议 D6 的第二半相反。这是一处**显式取舍**，留给后续轨道决定。

## 4. 复跑的测试（本轨自查时执行）

两轮逐字一致，见 `TEST_RESULTS.md` §1：9/9 本轨目标全绿；4 个既有失败已 A/B 归因为环境既有。

## 5. 仍需真正独立 reviewer 关注的点

1. `cancelActiveRun()` 的终态反馈（需推翻既有断言，本轮未改）。
2. `setFailed()` 无条件清结果卡的取舍（Round 1 的 P3，实现者选择保留）。
3. `C:foo` 形态的盘符相对路径。
4. 非 Windows 平台的行为（`makeHidden()` 的两条分支、`QFile::link()` 语义差异）。
5. GUI 真实交互（offscreen 之外）。
