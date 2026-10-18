# R16-A 独立评审 第二轮(Reviewer:另一未参与实现的独立子代理)

完整报告原文:/tmp/r16a-review-round2.md(只读审计,零仓库改动)。

## 结论:PASS(无未解 P0/P1),附 PR 前低成本补救条件

## 第一轮修复判定(全部核验为真)

| 项 | 判定 |
|---|---|
| P1-1 stackLimit | 已修复(grader_json.cpp 钉 256;上游 1.9.5/1.9.8 tag 源码亲核;测试 254 过/300 拒语义一致) |
| P2-1 输出预算绕过 | 已修复(终检在四形态唯一漏斗;key 超长面亦被最终 size 覆盖;放大算术成立) |
| P2-2 schema/loader | 已修复(双向逐例一致) |
| P2-3 负数下界 | 已修复(与 requireCount 逐行对称) |
| P3-1/2/3 | 已修复/部分(T01 文档残留 → 本轮已更正) |
| P3-5 深树析构 | 如实在案 |
| P3-4 faultlab 复跑 | **未修复且初稿处置失实 → 升级为本轮 P2-1,已补救**(见下) |

## 第二轮发现与处置(本文件即处置记录)

| 级别 | 发现 | 处置 |
|---|---|---|
| P2-1(本轮) | `^test_faultlab::` 正则匹配零个测试(无 TEST_PREFIX,实名 "fault lab: …"),171/174 面从未含既有 faultlab 测试;t02 "47 项/43=35+8/params 4 个" 计数均失实 | **已补救**:以精确注册名模式复跑,最终面 223×2 全绿(verifier 111 + fault lab 43 + params_r8 5 + r16a_faultlab 11 + grader 49 + r16a_grader 3 + evidence_seam 1,ctest -N 分族实测);TEST_RESULTS.md 重写并置顶更正说明;REVIEW_ROUND1.md P3-4 行、T02 TEST_MATRIX 两处、T02 IMPLEMENTATION WP-D 段同步更正。治本(给两个 legacy 目标补 TEST_PREFIX)属跨 PR 事项,记入 PR 正文遗留 |
| P3-A | T02 IMPLEMENTATION "reader 默认 stackLimit=256" 残留 | 已更正(附 P1-1 修记) |
| P3-B | T01 IMPLEMENTATION "bool 保持原可接受语义" 残留 | 已更正(探针实证表述) |
| P3-C | REVIEW_ROUND1 P3-6 "已精确化" 声称未发生 | 已更正为"无需动作"的如实记录 |
| P3-D | 开放 PR 实为 7 个(+#1517/1518/1519),tests/CMakeLists 交集 4 个 | 已刷新矩阵(逐一亲核;7 PR 均不碰本 PR 模块文件) |
| P3-E | TEST_RESULTS 未随修复更新;T02 引用不存在的 test_fault_matrix | 已重写/更正 |

## 回归扫描结论(评审原文)

修复 commit 恰好波及预期 10 文件;CMake 零新增链接、三处纯追加;git diff --check 干净、
工作区干净;全量 diff 复扫未见新代码问题;locale-matrix 环境阻塞证据链具体可证伪、
明确不称 PASS,接受为完整可信。
