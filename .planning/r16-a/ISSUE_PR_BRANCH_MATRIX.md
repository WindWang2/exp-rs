# R16-A Issue / PR / Branch Matrix(2026-10-17 现场采集;PR 面于推送前刷新)

## 开放 PR 与文件冲突矩阵(推送前 gh 实测:7 个)

| PR | 标题 | 状态 | 与本任务文件交集 |
|---|---|---|---|
| #1513 | r15-b 空间数据链路/数据集/实验存储(Draft) | open | **tests/CMakeLists.txt**(初查 head-40 截断漏看,已亲核 count=1) |
| #1514 | r15-a 遥感科学算法收敛 | open | 无 |
| #1515 | r15-c 模型推理/分块执行/GPU 容错 | open | **tests/CMakeLists.txt** |
| #1516 | r15-d Pi/MCP/Workflow | open | 无 |
| #1517 | r15-e Qt/QGIS 工作台/教学/帮助(执行期间新开) | open | **tests/CMakeLists.txt** |
| #1518 | r16-b science-context authority + decision-plane(执行期间新开) | open | 无 |
| #1519 | r16-c 教学会话状态/批量评测(执行期间新开) | open | **tests/CMakeLists.txt** |

- 7 个 PR 均不触碰 src/verification/**、src/grader/**、src/faultlab/**、data/faultlab/**(逐一
  gh pr diff --name-only 亲核)——模块所有权干净。
- tests/CMakeLists.txt 交集 4 个(#1513/#1515/#1517/#1519):本 PR 仅做纯追加注册
  (三处:verifier lane 块后 20 行、faultlab 块后、grader 块后),合并时预期无语义冲突,
  文本冲突由先合并方决定,后合并方 rebase 追加块即可。

## 远程分支分类(22 个)

- master + HEAD→master(1)
- 已合并 PR 残留(14):hardening/r12-{a,b,c,e,f}、r13-{g,h,i,j,k,l}、r14-{A,B,C,D,E}
  ——R12-R14 已合并(含 #1497-#1512 潮),保留不清理。
- 进行中 PR 分支(4):hardening/r15-{A,B,C,D} = PR #1514/#1513/#1515/#1516,只读。
- 未建立 PR 的分支(1):fix/all-open-issues,只读,禁止清理/重置。

## 本地 worktree(git worktree list --porcelain,GitHub 不可见)

- /home/kevin/project/exp-rs(master 主仓)
- exp-rs-hardening-planner-repair-capability-graph、exp-rs-hardening-verifier-grader-explain-evidence
  (本地开发 worktree,只读,不动)
- exp-rs-worktrees/{cpp-build-run-13,ds41-http-fetch-strict}、exp-rs-wt-rs14-exp-debugger
  (agent/* 一次性分支,只读)
- 新建:exp-rs-r16-a-{t01,t02,integration}(本任务)

## 开放 Issue(25 个)与本任务两轨道的关系

### 轨道 1(src/verification)直接相关

| Issue | 内容 | 现场核验状态 |
|---|---|---|
| #1488 | canonical_json 对超 INT64 整数 asInt64 抛 Json::LogicError 逃逸类型化边界 | #1509(r13-k)已合并;r14-E 已验证 master 侧 isUInt64 路径修复。T01 须在当前 master 重新实证(含异常屏障是否随 #1509 落地),已修复则写实证否定报告 |

### 轨道 2(src/grader + src/faultlab)直接相关

| Issue | 内容 | 现场核验状态 |
|---|---|---|
| #1464 | recordedAtUtc 原始字典序比较;声明的可选 scenario 参数被静默忽略 | r14-E 实证 master 两侧均已修复(带代码引用);T02 复核后写"已修复"证据,不重复造代码 |
| #1448 | stores fail-open(scan 错误当耗尽等) | 只读参考:stores 不归本任务所有;仅核对 grader/faultlab 是否有同型 fail-open 模式 |

### 跨模块参考(不写代码,只防回归)

- #1447(jsoncpp 无类型访问 9 处)——#1509 已合并;T01/T02 各自核验本模块无残留 asInt64/asString 裸调用逃逸。
- #1449(sqlite close 无锁 ×3、registry 悬垂)——不归本任务。
- #1465/#1466/#1467/#1468 —— 归其它模块;r14-E/r15 已处理或归属开放 PR,不动。

### 与本任务无直接关系(不展开)

#1489、#1490、#1491、#1494、#1495、#1485、#1486、#1454、#1455、#1456、#1458、#1459、
#1460、#1462、#1463 —— 各归属 i18n/CI/python/geospatial/operators/workflow/agent_ops 等模块,
本 PR 不触碰其文件。

## 处置纪律

- 对每个旧 Issue:输出"已修复(证据=PR/commit+测试)/ 重复 PR / 未复现 / 可复现(修复)"四态之一。
- 不机械执行旧 Issue 文本;Review 评论只当可验证线索。
- 不擅自关闭任何 Issue。
