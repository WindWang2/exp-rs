# 基线（2026-10-12 实测）

- origin/master HEAD: 8a08642760e2e8ffd2c4b965fc4683c01d3e4758 (= BASE_SHA)
- 远端分支: 仅 master
- 开放 PR: 0
- 开放 Issue (20): #1447 #1448 #1449 #1450 #1453 #1454 #1455 #1456 #1457 #1458 #1459 #1460 #1461 #1462 #1463 #1464 #1465 #1466 #1467 #1468
- 已合并: #1469(fix/review-p0-p1-wave) #1470 #1471 #1472 #1473 #1474(hardening/r8-ui-teaching-context, merge 84d782c40)
- #1474 review: 5 threads 全部 isResolved=false, isOutdated=false (chatgpt-codex-connector):
  T0 fault_transforms.cpp:392 cell-size mutation before target validation (P2)
  T1 guided_workflow_widget.cpp:493 Next enabled during async job (P2)
  T2 rs_scan_pool.cpp:57 destroyed-owner generations pruned → stale worker resumes (P2)
  T3 script_adapters.cpp:56 Windows Store alias accepted unprobed (P2)
  T4 script_adapters.cpp:131 truncation flags discarded by verify/drift (P2)
- worktree: ../exp-rs-r9-ui-teaching (本目录), preset dev-default → build-dev
