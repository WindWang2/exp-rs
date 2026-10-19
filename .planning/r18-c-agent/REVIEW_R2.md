# R18-C Review Round 2 (second independent context; dispositions of R1 fixes verified)

## Verifications (all attempted falsification, all PASS)
- V1 R1-P1 fix sound: catalog branch keeps category/displayName; parity holds on both branches; findSurfaceTool exact-match + findTool underscore normalization both resolve; dual-source enumeration found exactly list_layers / describe_dataset / get_lineage (all Meta-first now, matching tools/list order).
- V2 R1-P2 fix sound: all four call sites inside the tools/call McpToolError catch → isError:true INVALID_PARAMETER result; toInt strict.
- V3 cancel keys type-disjoint end-to-end (JSON parse yields Double for numbers at both arming and cancel; qlonglong test variant covered by the explicit metatype list).
- V4 progress relay sound at every seam; CLI leg (CliIO::reportProgress) untouched and already fraction-scaled.
- V5 pi timeout notify: exactly one cancel per request on every path; abort/completion/id-null paths unaffected.
- V6 restoration byte-exact; only intended deltas (stat arithmetic reconciled).
- V7 #1513-era master content does not touch any restored file (only plugin_registry.{cpp,h}, unrelated).

## Findings
| # | Sev | Summary | Disposition |
|---|---|---|---|
| F1 | P2 | numeric cancel key 6-sig-digit formatting not injective; bounded map overwrites duplicates | FIXED in e3a2d4c9c (QString::number 'g',17) |
| F2 | P2 | search_algorithms + data-platform pageCursor still rewind bad cursors | search_algorithms FIXED in e3a2d4c9c; pageCursor → CT-3 (outside ownership) |
| F3 | P3 | catalog branch unguarded normalizedInputSchema() → listed-but-tool-error for malformed registrations | FIXED in e3a2d4c9c (raw fallback, parity with projection) |
| F4 | P3 | get_tool_schema builds the full projection per call (O(N) normalization) | RECORDED as known cost; correctness unaffected; caching is a follow-up |
| F5 | P3 | comment undercounts shadowed aliases (get_lineage) | FIXED in e3a2d4c9c (comment names all three) |

## Verdict: READY — no P0/P1. F1/F2/F3/F5 adopted and fixed same-day; F4 documented.
