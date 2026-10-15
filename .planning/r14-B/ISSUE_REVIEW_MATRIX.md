# R14-B Issue/PR/Review Matrix — 2026-10-15

| Ref | Old root cause claim | Current code evidence | Repro result | Status | B action |
|-----|----------------------|----------------------|--------------|--------|----------|
| #1455 P1 corpse VRAM ledger | shared identity key erased replacement reservation | per-instance ticket `model_runtime.cpp:927-929`, wrapper deleter 1190-1193, corpse release 1076-1078; regression test failure_matrix:827 | test run ×2 (see TEST_RESULTS) | FIXED_ON_MASTER (by #1501) | none; extend with randomized conservation stress (WP-E1) |
| #1455 P2 fabric io CancelToken | io_fabric_operators.cpp | src/operators/io/** NOT in B ownership | n/a | UNRELATED (owner: geo/io track) | none |
| #1455 P2 OTB watchdog 1h | otb_operator_base.cpp | src/operators/otb/** NOT in B ownership | n/a | UNRELATED | none |
| #1459 scratch digest whole-file | scratch_registry.cpp:124/199 | `streamFnv1a` streaming at :173-179/:250-256 | code verified | FIXED_ON_MASTER (by #1501) | none |
| #1459 workflow optional deref | workflow_runtime.cpp:598 | not B ownership | n/a | UNRELATED | none |
| #1459 checkpoint rename rule | workflow_checkpoint.cpp:200 | not B ownership | n/a | UNRELATED | none |
| #1447 jsoncpp cluster (9 sites) | untyped asX() | all sites outside B ownership | n/a | UNRELATED | none |
| #1499 master compile break | python_worker_process_pool.cpp brace | syntax check exit 0 at f72f0c117 | verified | FIXED_ON_MASTER (by #1501) | none |
| #1505 gpu_plane warm eviction | per-device session bound + VRAM valve | PR open, touches src/runtime/gpu/gpu_plane.cpp | n/a | FIXED_IN_OPEN_PR | do not duplicate; keep B changes off that function |
| #1504 geodata durability | — | src/geospatial,data,dataset,experiment | n/a | OPEN (other track) | read-only |
| swarm audits (this track) | see GOAL.md WP table | verified line-by-line by orchestrator | RED tests in test_r14_B_* | NEEDS_REPRO→FIX | implement |
