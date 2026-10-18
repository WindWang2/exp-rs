# R16-A Ownership

## 轨道 1(R16-A-T01,分支 hardening/r16-a-t01-verification,worktree exp-rs-r16-a-t01)

可写:
- src/verification/**(全部)
- tests/test_verifier_*_14.cpp(既有七 lane 的**增量**修改)及新测试文件 tests/test_r16_a_verifier*.cpp
- tests/CMakeLists.txt(仅最小增量注册新测试,追加在 verifier lane 块附近;不重排、不改写其它区域)
- .planning/r16-a-t01/**

只读:其它一切。特别禁止写 src/grader/**、src/faultlab/**、src/data/**、src/experiment/**、
src/agent/**、src/sdk/**、src/teaching*/**、以及 PR #1513-#1516 的全部变更文件。

## 轨道 2(R16-A-T02,分支 hardening/r16-a-t02-grader-faultlab,worktree exp-rs-r16-a-t02)

可写:
- src/grader/**(全部)
- src/faultlab/**(全部,含 util/)
- tests/test_grader_*.cpp、tests/test_faultlab*.cpp 的增量修改及新测试文件 tests/test_r16_a_grader*.cpp、tests/test_r16_a_faultlab*.cpp
- tests/CMakeLists.txt(仅最小增量注册,追加在 grader/faultlab 块附近)
- .planning/r16-a-t02/**

只读:其它一切。特别禁止写 src/verification/**、src/teaching_admin/**、src/teaching/**。

## 集成(R16-A,分支 hardening/r16-a-evidence-verification,worktree exp-rs-r16-a-integration)

- 仅做 cherry-pick(保留来源 SHA)、冲突解决(按最新 master 真实语义,禁止 ours/theirs 一键抹除)、
  集成级文档(.planning/r16-a/**)与最终 PR。
- 两子轨道并发期间使用不同工作树与不同 index;集成树不并发写入。

## 跨轨道合同

- 轨道 1/2 仅通过既有 API 协作(typed failure codes、digest/canonical json 契约、状态权威)。
- `tests/test_canonical_digest_parity.cpp` 是现有跨模块 canonical 契约测试:两轨道均不得破坏其
  语义;如需扩展 parity 断言,轨道 2 在自己 worktree 内追加独立测试文件,不改该共享文件。
- 发现对方轨道的接口问题时:记录到各自 LEDGER 的"跨轨道事项"节,由集成 Agent 仲裁;不越界写文件。
