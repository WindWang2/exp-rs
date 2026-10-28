# PR_COLLISION_MATRIX — R25-E

## 初查（2026-10-29）
- Open PR = 0（gh pr list 实查）。远程无 r25 分支。
- 本地兄弟轨分支（未 push）：r25-A(science-processing)/r25-B(data-model-runtime)/r25-C(pi-mcp-execution)/r25-D(qt-qgis-cartography)。
- 预计共享文件：tests/CMakeLists.txt（五轨均追加）——按 union 协议只在本轨标记块内追加、不重排。
- scripts/windows/run_lab.cmd：r25 无兄弟轨声明；r23-E(已合并) FINAL_REPORT 未列此文件；单写者=本轨。
- src/teaching_admin、src/lab、src/grader、src/verify_adapters：本轨独占（五轨边界约定）。

## push 前复查义务
push 前重新 fetch + gh pr list，重算 changed-file 交集。
