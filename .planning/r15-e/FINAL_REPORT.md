# R15-E FINAL REPORT

（本文件在测试与独立审查完成后定稿；下面为结构与已确定的事实。）

## 1. 身份与坐标

| 项 | 值 |
|---|---|
| 轨道 | R15-E · Qt/QGIS 工作台、实验教学、帮助与操作反馈质量 |
| 分支 | `hardening/r15-E-desktop-teaching-help` |
| worktree | `exp-rs-r15-e-20261017`（`C:/Users/wangj.KEVIN/projects/` 下） |
| BASE_SHA | `46791bc65c6f552ebbcf723bb91892736d56f319` |
| HEAD_SHA | 见文末「提交」小节 |
| 构建树 | `build-dev/`（本轨独立，不挂 vcpkg toolchain） |

## 2. 修改文件 + 工作包

见 `PR_BODY.md` 第 1 节修复索引；`git diff --stat` 见 `TEST_RESULTS.md`。

## 3. 问题归类（严格区分）

- `FIXED_ON_MASTER`（由他人 PR 已修，本轨只做回归验证）：#1488/#1486/#1485（#1509/#1505）、矩阵表 bounded 与 dock 线程（#1510）、教学证据（#1511）、矢量编辑/MapSpec/原子写（#1512）。
- `REPRODUCED_OPEN`（本轨复现并修复）：R15E-1 … R15E-11，共 14 个文件。
- `NOT_REPRODUCIBLE`：子代理报告的 `rs:atmospheric_correction.band` 与 `rs:mosaic.inputs` 两处「帮助参数漂移」经源码逐条比对**证伪**。
- `OUT_OF_SCOPE` / 所有权越界：`src/grader/grader_json.cpp` 缺 `stackLimit`（证据已记录）。
- `DEFERRED_WITH_EVIDENCE`：取消后无终态反馈、禁用态原因的 4 处独立实现、帮助 guidance 死代码/裸 id 渲染。

## 4. RED → GREEN 证据

见 `TEST_RESULTS.md`（含命令、exit code、断言数）。

## 5. 失败 / 限制

见 `PR_BODY.md` 第 4 节「仍开放的风险」；其中 E11/E13/E15/E16 明确标注 **NOT VERIFIED**。

## 6. 独立 reviewer 结论

见 `REVIEW_ROUND1.md` / `REVIEW_ROUND2.md`。

## 7. PR

见文末。
