# OWNERSHIP — geospatial-data-fabric-9

## This track owns

- `src/geospatial/**` (the core I/O authority — readers, writers, remote,
  range cache, STAC, multidim, doctor, formats, atomic FS, util)
- `src/operators/io/**` (io:* operator family)
- geospatial doctor / identity / cache I/O seams
- STAC / data transport parsers inside `src/geospatial`
- geospatial I/O tests (`tests/test_io_*.cpp`, new `tests/test_io_*.cpp`) and
  I/O docs (`docs/io/**`)
- new: `src/geospatial/catalog/**` (query primitives), `src/geospatial/hints/**`
  (locality hints), disk layer of the range cache

## Explicitly NOT owned (must not be restructured by this track)

- DataManager UI + stores (`src/data/**`, `src/app/**` workbench widgets)
- generic dataset/experiment metadata store (`src/dataset/**`)
- scheduler / task center (`src/processing/framework/**`,
  `src/workflow/**`) — we provide hints/contracts only
- Agent runtime (`src/agent/**`)
- model runtime
- cartography, plugins, help system

## Shared files — minimal-increment rule

Shared files are touched only at milestone boundaries, minimal hunks:

- `.gitignore` (one line: planning exception)
- `tests/CMakeLists.txt` (test target registrations, incremental)
- `CMakeLists.txt` only if a new subdirectory must be wired (expected: none —
  `src/geospatial/CMakeLists.txt` is ours)
- `CHANGELOG.md` (one entry at the end)
- `data/help/**` NOT touched (help/track-10 owns mechanical help governance;
  io operator schema truth is asserted by tests we own)

## Conflict avoidance vs the two active -9 sibling tracks

- `feat/execution-concurrency-lifecycle-9`: owns workflow/task-center
  concurrency. The M8 hints seam is a NEW leaf consumed later by them — no
  shared file edits required from our side beyond our own module.
- `feat/scientific-algorithms-9`: owns processing algorithms. No overlap.
- The local-only master commit `8f6293bceb` fixes #850 (vector_writer) among
  others. This track re-implements that narrow fix on its own base (our fix is
  broader: explicit rollback semantics + move transfer + regression). At the
  final `origin/master` sync, if `8f6293bceb` has landed, the vector_writer
  hunk may conflict; resolution rule: keep the union (both the moved-state
  transfer and the explicit rollback), keep both tests.
