# UI Scale 5.0 — benchmark envelopes (Workbench 5.0, Milestone O)

Measured on the development workstation (Windows, MSVC 14.38 Release, -j2
shared with parallel track builds) via `test_ui_scale_benchmark`
(`SICNU_WS3_STRESS=1` raises the level from 10k to 100k; raw JSON via
`SICNU_WS3_BENCH_OUT`).

## WorkspaceGovernanceModel paging pipeline (workspace browser)

| Level | First page | Full fill (all pages) | Pages | Filter (search) |
|---|---|---|---|---|
| 10k assets | 0 ms | 448 ms (49 pages) | 49 | 30 ms |
| 100k assets | 0 ms | 72.3 s (499 pages) | 499 | 278 ms |

Envelope policy (structural guards in the test, absolute budgets recorded
here — never CI-enforced as flaky gates):

- First page budget: 1500 ms (measured 0 ms — model answers from the first
  indexed page without materializing the catalog).
- Interactive filter budget: 800 ms (measured 30 ms @10k, 278 ms @100k).
- Full fill = pathological "fetch everything" enumeration; 100k completes
  and stays bounded (500 pages × 200 rows), memory O(pageSize).

## Structural certification (asserted, not timed)

- `rowCount` after the first query ≤ `kPageSize` (200) — no full
  materialization on open.
- `entityId` non-empty on every fetched row; empty beyond the fetched set.
- Filters narrow the row set and re-paginate.
- Data panel and governance model use delegates only (no per-row QWidget);
  thumbnails/metadata stay async with cancellation (4.0 contract) — the
  5.0 track verified the model path end-to-end at scale.

## Runner

- 10k: `runone.cmd` variant without `SICNU_WS3_STRESS`
- 100k: `SICNU_WS3_STRESS=1` (RUN_SERIAL, 900 s timeout)
