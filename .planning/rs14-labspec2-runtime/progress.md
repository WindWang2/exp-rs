# Progress — rs14-labspec2-runtime

## 2026-09-22

- Phase 0-2 done: recon/plan/slices committed (`79139ee14`), dynamic dedup vs 6 open PRs (notably #1190 curriculum, #1188 capsule) recorded in recon.md.
- Build baseline: worktree-local `build-dev` (preset dev-default + `CMAKE_PREFIX_PATH=/home/kevin/pwb-sdks/root/usr`, cmake from pwb-sdks with `LD_LIBRARY_PATH=.../usr/lib`). Configure ~40s; `test_lab_runtime` builds at -j1 in ~3min (Catch2 FetchContent one-time).
- **Slice A GREEN** (schema v3 + src/lab/spec_runtime + loader minimal v3 acceptance):
  - RED observed: configure failure (missing src/lab sources), then 1 compile error (value field vs fn) before green.
  - `test_lab_runtime`: **248 assertions / 3 test cases all green** (happy path mapping, 25-section negative matrix w/ typed codes `lab.runtime.schema|field|reference`, FIPS 180-4 vectors, fingerprint determinism).
  - `data/schemas/labspec.schema.json`: enum {1,2,3}, `runtime` $defs, v1 forbids runtime, v2 forbids runtime.
  - `src/app/widgets/lab_spec_loader.cpp`: accepts spec_version 3 (shallow runtime-is-object check; deep validation stays in src/lab — single deep validator), rejects runtime key in v1/v2 ("requires spec_version 3"). NOT yet compiled (heavy app target) — exercised in Slice F heavy build.
  - Known follow-up: `tests/test_labspec.cpp:230` uses spec_version 3 as its unsupported-version negative probe; must be updated to probe 4/99 in Slice F (tracked below — do not forget).
- Deferred to Slice F: check_lab_registry.py {1,2,3}; gen_lab_docs.py v3 rendering; test_labspec link sicnu_lab_runtime + v3 corpus cases; heavy build.
