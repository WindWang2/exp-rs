# R16-D-T08 Performance Notes

No benchmark numbers are claimed this round: every landed WP is a correctness/robustness fix; the two perf-class findings from the audit (T08-F11) were deliberately deferred (see IMPLEMENTATION.md). This file records code-reading observations only — nothing here is a measured result.

## Observed hotspots (unchanged, quantified by code reading)
- `produceMap` runs `resolveCompositionPass` and then `MapSpecCompiler::compile` re-runs the identical solve (mapspec_compiler.cpp:431-433) — per repair iteration, i.e. worst case 2× full solver pipelines per iteration (solver bounded: ≤24 hard sweeps + ≤32 core simulations (now sliced per target) + ≤8 soft passes).
- Every compile re-encodes inline chart/colorbar PNGs even when inputs are unchanged (mapspec_compiler.cpp:1248-1297).
- `resolveTokenSet` (registry copy + variant merge + overrides + full-tree deep resolve) runs 3-4× per produce iteration (composition.cpp:1923, mapspec_compiler.cpp:412, quality.cpp:248/:1755).

## Perf-relevant changes that DID land (correctness-motivated, no numbers claimed)
- WP-11 keeps the global simulation cap while guaranteeing each unsat-core target ≥1 attempt — worst-case total simulations unchanged (still ≤ kMaxCoreSimulations).
- WP-9 tokenizer adds a constant-time consumed-length check per numeric token.
- WP-6 per-page lookups are map finds per item (O(log n), n = page count ≤ small).
