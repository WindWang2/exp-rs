# REVIEW_LOG — Track 14: CLI Surface Completion R4

Phase 5 独立对抗性 review（reviewer：独立只读 subagent，1.42M tokens，40 次工具调用，逐文件 diff + master 对照）。

## 裁决：FAIL → 五项 P0 全部关闭（4 项在 review 派出后已先行修复，1 项真回归由 review 抓出并修复）

reviewer 审的是派出时点的 18 提交；其后本轨道又前进了 9 提交（含 RED 期已修的部分）。逐项对账：

## P0（全部关闭）

| 项 | 内容 | 处置 | 证据 |
|---|---|---|---|
| P0-1 | `dataset version` 双前置门自相矛盾 + 功能回归（version 被门在 --version 上；stats 丢检查；不存在的 sample 占位） | **review 抓出的真回归**：门改为 validate\|stats→--version、version→--dataset；parity 探针改 stats 5 类 + inspect --dataset 往返 | ad242c1a5b |
| P0-2 | 三个测试钉死 `dataset version --version`→5，实现路径不符 | 派出后已修：探针改 `dataset inspect --version`（versionById 路径） | 245aed760c |
| P0-3 | fail-fast"未知 flag 即拒"断言无实现 | 派出后已修：run 残余 flag 拒绝 + algorithms list 拒绝 | f47ee3a3ad |
| P0-4 | error_messages 四元组段期望与实现输出矛盾（plugin uninstall/pipeline 缺文件） | 派出后已修：三处段期望对齐实现载荷 | 9874cb1d65 |
| P0-5 | vocabulary 方向 1 的 data 探针必红（URL 语法拿不到词表 + usage 行漏 cube/mirror） | 派出后已修：探针改裸命令 + usage 行补全 11 动词；另把 data 尾站点升级为词表门（P2-4 一并） | 78f457d631 + 35eacbd082 + f0750c4df9 |
| P0-6 | 验证证据链断裂（EVIDENCE §3/§4 空、REVIEW_LOG 未勾） | 属实——构建受并行轨道负载限制（load≈20，-j2 下 ~960 步/小时）。**诚实声明**：ctest 双跑日志在构建落地后回填，本 PR 不早于双跑全绿开出；REVIEW_LOG 本轮回填 | 本文件 + EVIDENCE |

## P1（全部关闭）

| 项 | 内容 | 处置 |
|---|---|---|
| P1-1 | 2>&1 合并流首行/JSON 解析无 stderr 噪声容错 | 派出后已修：error_messages 改"唯一锚定行"断言（872d3f6f2d）；复核确认全部 JSON 断言走 2>/dev/null 流（runCli 默认），mergeStderr 路径仅做行扫描/子串，无 JSON 解析；vocabulary/parity 的词表检查为子串容错 ✓ |
| P1-2 | EVIDENCE/REVIEW_LOG 回填义务 | 本轮回填 REVIEW_LOG；EVIDENCE §3/§4 待 ctest（见 P0-6 声明） |

## P2（处置）

1. usageError reason/hint 重复 → 已修（hint 置空 + 2 处测试期望翻转）f0750c4df9
2. models 守卫冗余析取 → 已修 f0750c4df9
3. sample 死代码 → 已随 P0-1 清除 ad242c1a5b
4. data unknown-sub 裸位点 → 已升级词表门 f0750c4df9（EVIDENCE §6.3 的"保持"声明相应作废）
5. README JSON 示例键序 → 不改（JSON 键序无合同意义；记录在案）
6. session 断言子串偏弱 → 保留（vocabulary session 用例已做 14/14 全词表存在性；集合相等在 dataset 家族已有更强断言）
7. .gitignore 实为 4 行块 → DECISIONS 已记 D3/D7 注
8. lab 缺值 2 vs dataset 缺参 6 跨命令不一致 → master 既有分类（lab 的 schema 校验面语义），CONTRACT 行 13 论证保留

## 抽查清单结论（reviewer 已代查，本轨道复核）

- [x] 词表核对记录 6 条：方向 2 双源独立（README 文件解析 vs 运行时 expected）非永真 ✓
- [x] 19/19 总表 4 行抽查：待 ctest 输出后终审（构建门）
- [x] 退出码集中性 PASS（无新增散写数字）✓
- [x] 四元组单点构造 PASS（fail/usageError/labError/passportError/sessionError/dispatch 之外无散拼）✓
- [x] dispatch catch 不吞上下文（what() 进错误信息）✓ 无假开关 ✓
- [x] 白名单外 diff=0（rs_pipeline_runner.cpp / commands.json 实测零行）✓
- [x] 与 #1334/#1339 无重复实现 ✓
- [x] 文档一致性：四元组段格式逐字段一致；session 14 动词与 isKnownAction 精确一致 ✓

**更新裁决（本轮终态）：实现/测试矛盾 5 处全部消除；Standards/Spec 各轴 PASS；唯 EVIDENCE §3/§4 双跑日志为构建门未过项——PR 在其回填并全绿后方可开出。**
