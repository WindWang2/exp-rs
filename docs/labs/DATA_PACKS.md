# Lab Data Packs — deployment & integrity contract

A lab data pack (`sicnu.lab-pack/1`) is ONE lab's machine-checkable data
contract: which files the lab needs, where they come from, and how far a
classroom machine can trust them. Packs answer the teacher's 23:00 question —
"will lab 4 grade on this machine?" — with typed evidence instead of a
run-time surprise.

Packs are the *deployment* authority. They complement, and never replace, the
other lab contracts:

| contract | authority for |
|---|---|
| `sicnu.lab-data-spec.v1` (`data/labs/data-specs/`) | scientific REQUIREMENTS (grid, epochs, sensor truth, acceptance) |
| `sicnu.lab.rules/1` (`data/labs/grading/`) | GRADING assertions and tolerances |
| `sicnu.lab-pack/1` (`data/labs/packs/`) | DEPLOYMENT: presence, checksums, sizes, licenses |

## Provenance tiers

Verification strength follows the declared `provenance` of each input:

- `committed-fixture` — in-repo deterministic file (`tests/fixtures/lab/…`).
  `sha256` + `bytes` are REQUIRED and hard-checked. Missing or corrupt input
  FAILS the pack.
- `generated-samples` — produced on the target by `sicnu_generate_samples`
  (fixed seed 42). Presence checked; declared byte size is informative
  (GDAL-version drift) and a mismatch only degrades.
- `generated-tmp` — produced by `scripts/gen_lab_fixtures.py` under
  `data/labs/_tmp/` for headless pipeline verification; same policy as
  generated-samples.

## Files

- `data/labs/packs/<lab_id>.pack.json` — one pack per lab id (16) plus
  `grading_corpus.pack.json`, the deployment unit of the committed grading
  fixtures (checksum-pinned so accidental edits are detected).
- Loader/verifier: `src/agent/lab_data_pack.{h,cpp}` — read-only, streams
  hashes in 1 MiB chunks, Unicode-path safe, never writes.
- Authoring/regeneration: `scripts/gen_lab_packs.py` (deterministic;
  `--check` exits non-zero on drift).
- Diagnostics: `lab --self-check` verifies packs (all, or `--lab-id <id>`)
  alongside the offline gate, projection authority and rules parsing.

## Authoring rules

- Input paths are repo/bundle-relative with FORWARD slashes.
- `committed-fixture` entries MUST declare `sha256` (64 lowercase hex) and
  `bytes`; regeneration via `gen_lab_packs.py` refreshes them.
- Each input declares `role` (`sample|aux|truth|fixture`) and, where
  meaningful, a `sensor_truth` statement (band roles, CRS, dtype, date
  convention) so a teacher can audit what the data claims to be.
- Every pack declares `license`; all current packs are
  `generated-in-repo` (synthetic teaching data, no third-party rights).
- `declared_offline_bytes` (when all inputs are committed) quotes the disk
  footprint a teacher can plan around.

## Tests

`tests/test_lab_data_pack.cpp` holds the known-answer, negative, Unicode and
no-write contract tests plus the drift guards: every lab id has a pack, every
committed fixture matches its pack checksum, and `gen_lab_packs.py --check`
reports zero diff.
