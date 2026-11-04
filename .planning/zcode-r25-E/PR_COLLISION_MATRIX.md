# PR_COLLISION_MATRIX — R25-E

## 初查（2026-10-29）
- Open PR = 0（gh pr list 实查）。远程无 r25 分支。
- 本地兄弟轨分支（未 push）：r25-A(science-processing)/r25-B(data-model-runtime)/r25-C(pi-mcp-execution)/r25-D(qt-qgis-cartography)。
- 预计共享文件：tests/CMakeLists.txt（五轨均追加）——按 union 协议只在本轨标记块内追加、不重排。
- scripts/windows/run_lab.cmd：r25 无兄弟轨声明；r23-E(已合并) FINAL_REPORT 未列此文件；单写者=本轨。
- src/teaching_admin、src/lab、src/grader、src/verify_adapters：本轨独占（五轨边界约定）。

## push 前复查（2026-10-29 回填）
- fetch 后 origin/master=2610cb4741 未漂移；gh pr list：open PR = **#1724 (r25-D)**（推送时刻唯一）
- 与 #1724 changed-files 交集：`.gitignore` + `tests/CMakeLists.txt`（五轨共享 append 文件）
  - .gitignore：双方均为文件末尾 append（D 块 `!.planning/zcode-r25-D/`、E 块 `!.planning/zcode-r25-E/`），git 三方合并可自动并集；未重排/未删任何既有行
  - tests/CMakeLists.txt：双方均为 R23-B 块之后的 append（D: BEGIN R25-D 标记块 ×N；E: 3 个 R25-E 标记块），无重排/无去重；merge gate（check_merge_registration_integrity.py --require-refs）已在 E 侧 exit 0（1063→1069, lost 0, dup 0, marker 0）
- src/ 零交集（D 轨改 src/app/{main_window,cartography,shell,workbench}、src/agent/mapspec；E 轨改 src/lab、src/teaching_admin、src/app/teaching_admin）
- push 已执行；PR #1725 已创建（非 draft，base=master）。**不等待远端 CI，不自动合并。**
