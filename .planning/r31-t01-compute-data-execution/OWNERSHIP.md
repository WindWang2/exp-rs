# Ownership Summary

The authoritative detailed boundary and active-PR exclusion list is `PATH_OWNERSHIP.md`.

- T01 writes only minimal scientific/model/data/public-execution changes with a production call chain and observable oracle. Dispatcher, TaskCenter and OutputCommitter remain the single existing authorities.
- T02 owns Pi, Agent runtime/host, managed Python implementation and external process API. T01 consumes but does not modify those seams and preserves Python-specific publication behavior.
- T03 owns general desktop/cartography/mapspec/layout/translations and UI cancellation wiring. T01 supplies compatible backend behavior only.
- Open #2061 owns its current Studio/experiment/teaching file set; all are read-only while open. Shared `tests/CMakeLists.txt` receives only the marked R31-T01 block and `test_r31_t01_*` targets.
- `.gitignore` contains only the unique allowlist for this track's Markdown evidence. Build output, raw API snapshots and logs remain local in `build-dev/` and `.scratch/`.

No handoff is currently blocking independent T01 delivery; details are in `HANDOFF.md`.
