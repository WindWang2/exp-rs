# DECISIONS — ds41-capability-search-13

Autonomy: `autonomy=full`. Options considered and the taken default are
recorded here per the unattended-mode rule in `.agents/AGENTS.md`.

## D0 — Branch name: `track/` not `agent/`

- Prompt suggests `agent/ds41-capability-search-13`. The `agent/*` ref I
  created on 2026-09-20 was deleted by the shared-repo branch janitor while
  checked out (left an unborn HEAD — recovered via `git update-ref`).
- `track/*` refs demonstrably survive (`track/ds41-capability-help-sync`
  still exists post-merge). **Default: `track/ds41-capability-search-13`.**

## D0b — Worktree reuse instead of `new_worktree.py`

- Reclaimed `exp-rs-worktrees/ds41-capability-help-sync` (its branch was
  merged residue; reset to `79adfe78a` = same clean tree as a fresh
  worktree) to keep the warm `build-cap/` dir — a cold vendored-QGIS build
  at `-j2` would cost hours. Same end-state as a new worktree.

## D0c — Baseline repair committed inside this PR

Master `79adfe78a` does not build/pass on Windows MSVC:

- `tests/test_algorithm_meta_drift.cpp` + `tests/test_capability_knowledge.cpp`
  contain a raw CR (blob: raw LF) inside `'\r'` char literals → C2001.
  Fix: proper `'\r'` escape. Semantics (`lfOnly` strips CR) unchanged.
- spectral12 added `rs:cem_detection` + `rs:spectral_spatial_fuse`
  (154 rs: ops) without regenerating Layer-B capability sidecars or
  knowledge pages → parity/knowledge gates red. Fix: ran
  `capability_knowledge_tool gen-meta` + `gen-pages` (sanctioned
  generators; no hand-edits).

Both are inside this track's ownership (capability surfaces/tests) and are
a prerequisite for any green verification. They ride in this PR.

## D1 — One engine in `src/processing/framework/`

New: `algorithm_search.{h,cpp}`, `sicnu::processing` namespace.

- Chosen over `src/agent/tool_catalog/` because the authority
  (`AlgorithmDescriptor`/`AtomicAlgorithmRegistry`) lives in
  processing/framework, and CLI must link it without depending on
  `src/agent` internals. `search_tools`/`AgentToolCatalog` keep their own
  matcher (different surface: interaction/data/custom tools — out of
  scope, untouched).
- Engine signature takes `const std::vector<AlgorithmDescriptor> &`
  (universe injected by caller) → tests inject mutated corpora for
  mutation-potency oracles; no registry monkey-patching.

## D2 — Search universe = registry ∪ providers (revised at impl time)

- Original plan: search `listDescriptors()` only. **Changed during
  implementation** — the old handler filtered `list_algorithms` output,
  so provider algorithms (`gdal:`/`otb:`/`qgis:`/`native:`) were
  text/group-searchable; dropping them would silently shrink MCP
  discovery coverage.
- `handleSearchAlgorithms` now searches the **same union** as
  `handleListAlgorithms`: registry descriptors plus provider algorithms
  not already in the registry (id-deduped, registry wins).
- Providers enter through `ProviderAlgorithmAdapter(*alg).descriptor()`
  (typed input/output ports, provider tags, purpose) — the same view the
  old contract produced, where `findAdapter` lazily built and cached a
  ProviderAlgorithmAdapter for any provider id a filter touched. So
  text/group/tag/purpose and input/output-type facets all resolve for
  providers; only facets that live exclusively in authored
  `agentMetadata` (taskFamily, modality, largeRasterSafe) stay
  provider-inert, exactly as before. Temporary adapters keep coverage
  history-independent (no `mAdapters` cache mutation during search).
- CLI `algorithms search` keeps the registry-only universe (same as
  `algorithms list` today). Surface universes therefore mirror their
  list counterparts; the parity oracle asserts identical `rs:` slices
  across surfaces, not identical provider coverage (providers are
  MCP-only extras, same asymmetry `list_algorithms` always had).

## D3 — Query model (authoritative contract)

`AlgorithmSearchQuery` fields — every field maps to a declared descriptor
fact; nothing is inferred from prose:

| field | source | match |
|---|---|---|
| `text` | free text | tokens ANDed; token = substring of folded id/displayName/group/tag/purpose/description |
| `group` | `desc.group` | case-fold exact |
| `tags` (csv) | `agentMetadata.tags[]` | ANY-of; case-fold exact per entry |
| `purpose` | `agentMetadata.purpose` | normalized substring |
| `task` | `agentMetadata.taskFamily` | case-fold exact |
| `modality` (csv) | input-port `rsContract` `modality`/`modalities[]`/`dataKind` | ANY-of; case-fold exact |
| `input_type` | `dataTypeToString(port.type)` on inputs | case-fold exact (matches existing MCP semantics) |
| `output_type` | same on outputs | case-fold exact |
| `large_raster_safe` | `agentMetadata.largeRasterSafe` | bool |
| `limit`/`cursor` | — | pagination |

- **AND across dimensions, ANY within comma-lists, AND within text
  tokens.** Chosen ANY (not ALL) inside lists for consistency with the
  existing `search_tools` band_roles facet semantics and because lists
  express vocabulary alternatives ("sar OR insar"), not conjunctions.
- **Normalization**: `QString::normalized(NormalizationForm_KD)` +
  strip non-spacing marks + `toCaseFolded()` — deterministic,
  locale-free, accent-insensitive on both haystack and needles.
  Tokenizer keeps `QChar::isLetterOrNumber()` (all Unicode letters —
  CJK names stay searchable) plus `_` and `:`, so `rs:` ids and
  `spectral_index` survive.
- **Empty query**: legal; returns all filter-passing entries, id-sorted.
- **Structured filters are exact** (case-folded): no prefix/substring
  semantics on tags/groups/types — bounded and predictable. `purpose`
  alone is substring (it is prose by contract).

## D4 — Ranking

- Per-token field weights: id 8, tags 4, displayName 4, group 2,
  purpose 2, description 1. Whole-query == normalized id: +100.
- Order: score desc, then `id` asc (ordinal `std::string <`).
- Deterministic across platforms (no locale, no floats, total order).
- Rejected: reusing `searchTools`' scoring — it mixes prose heuristics
  (e.g. modality matching description text) that this contract forbids.

## D5 — Bounds / typed refusal

- `text` ≤ 1024 **bytes** (UTF-8 — stricter than chars for non-ASCII);
  each scalar filter ≤ 256 bytes; comma lists ≤ 16 values;
  `limit` clamp [1,500] default 50; negative `cursor` clamps to 0
  (not a violation — mirrors list_tools semantics).
- Violations → `AlgorithmSearchResult.error` (typed refusal, empty hits).
  MCP: handler throws `McpToolError` → `isError:true` tool result with
  `errorCode:"INVALID_QUERY"` (the established tool-error family — a
  protocol-correct refusal, better than an in-band empty list). CLI:
  `ExitCode::InvalidInput` (6) with envelope error.
- Unknown JSON args are ignored by the MCP dispatch (schema is the
  contract — documented in schema description). CLI: unknown `--*` arg →
  `InvalidInput` usage error.

## D6 — Zero-result diagnostics

Engine always computes `vocabulary` = sorted unique
groups/tags/taskFamilies/modalities/port-types present in the universe,
plus `suggestions` = ≤5 closest ids. Surfaces emit them **only on zero
hits** (normal responses stay compact).
Revised at impl time: the first token/prefix suggestion scheme was
vacuous (a token contained in an id is already a hit, so suggestions
could never fire). Replaced with **bounded Levenshtein ≤ 2** between
each query token and each folded id segment (`rs:sar_speckle` →
`rs`,`sar`,`speckle`), tokens/segments ≥ 4 chars, best 5 by
(distance, id). Bounded work per pair via early-exit rows; not a
ranking subsystem — it only runs when hits are empty.

## D7 — Projection parity (no shape drift)

- `handleListAlgorithms`' per-descriptor entry builder is extracted to a
  shared `algorithmEntryMap(desc)`, and the provider branch to a shared
  `providerEntryMap(alg)`, so `list_algorithms` and `search_algorithms`
  cannot emit different entry shapes for either universe slice.
- CLI `algorithms search` envelope `data` becomes
  `{algorithms:[…], count, total, limit, cursor, next_cursor, hints?}` —
  same object shape as MCP modulo key casing (CLI JSON convention uses
  snake_case: `next_cursor`/`display_name`; MCP keeps camelCase
  `nextCursor`/`displayName` — surface-consistent, same fields/values).
  `algorithms list` keeps its array shape. Recorded as the one
  intentional CLI schema change; the old array was under-specified for
  pagination anyway.

## D8 — CLI flag surface

`algorithms search [text] [--group g] [--tag a,b] [--purpose p]
[--task f] [--modality m[,m2]] [--input-type T] [--output-type T]
[--large-raster-safe] [--limit n] [--cursor n]`

- Bare `search` with no text and no filters → `InvalidInput`.
- Positional text remains first arg (back-compat with `search ndvi`).

## D9 — Pi stays a projection

`pi/exp-rs-spatial.ts` bridges MCP tools only (verified — no matcher).
Change = `pi/knowledge/spatial-algorithm-guide.md` gains a short
"search filters" section documenting the contract vocabulary; generated
capability pages remain the metadata authority.

## D10 — Rejected alternatives

- **New fields on AgentMetadata**: unneeded — all contract fields exist.
- **band_roles filter**: no descriptor declares `rsContract.bands[].role`
  today; adding it would be a filter that can never match. Vocabulary
  echo surfaces what's honest. (`search_tools` keeps its own facet.)
- **Provider algorithms in search**: see D2 (included via adapter
  descriptors — same typed-port view `findAdapter` produced).

## D11 — Additional baseline repairs folded into this PR

- `test_mcp_server.cpp` stale assertion: expected `"Invalid pipeline"`
  but master throws `"run_workflow: pipeline rejected; … invalid
  pipeline definition"` (verified identical at base SHA). Fixed the
  check to `"pipeline rejected"`. Pre-existing master failure, unrelated
  to the search work.
- `rs:cem_detection` + `rs:spectral_spatial_fuse` sidecars: generated
  skeletons via `gen-meta`, then **authored** `summary`/`failure_modes`/
  `applicability`/`teaching_use` (the documented hand-written fields —
  `gen-meta` preserves authored keys; completeness gate requires them).
  Content derived from the operators' declared `purpose`/`prerequisites`/
  `limitations` metadata, matching the Chinese authored convention of
  sibling hyperspectral sidecars. `gen-pages` re-run →
  `capability-hyperspectral.md` + `capability-index.md` refreshed
  (8→10 operators).
- **Reuse `AgentToolCatalog::searchTools`**: couples algorithm search to
  the tool-registry universe and its prose-matching heuristics; engine
  must be descriptor-native and prose-free.

## Review disposition (independent reviewer, pass 1)

Verdict SHIP, P0=0 / P1=0. All three P2s fixed; P3s fixed where cheap or
recorded below.

**Fixed**: provider adapter descriptors (D2 — old `findAdapter` DID give
providers typed ports; my lightweight descriptor silently narrowed the
old contract); guarded `rsContract` reads (string-or-array tolerant —
array `modality` previously threw `Json::LogicError` into every search);
differential-ranking oracle (pins weights 8/4/1, not just tie-breaks);
provider-union test (`QgisAlgorithmsProvider`, source=provider + id
uniqueness + `input_type` coverage); camelCase MCP hints vocabulary
(`taskFamilies`/`dataTypes`); CLI `takeValue` no longer eats `--flags`
as values (value-less flag → `InvalidInput`); tokenizer charset doc;
hostile-path asserts (negative cursor→0, limit=0→50, cursor>total→empty,
`errorCode=="INVALID_QUERY"`, accent/CJK folding, CLI rejections+hints);
hoisted per-descriptor `declaredModalities`/`taskFamily` fold and the
suggestions regex; final tie-break on raw index.

**Accepted P3s (documented, not fixed)**:

- `QVariant::toInt()` coercion of cursor/limit overflow → consistent
  with every sibling MCP endpoint; bounds make it harmless.
- CLI `source` vocabulary (`plugin`/`builtin`) differs from MCP's
  (`rs`/`provider`) — each surface mirrors its own `list` counterpart;
  cosmetic, noted for consumers.
- `io.parameters` in capability sidecars shows `"type":"string"` for
  array-typed params (`target`, `libraryMaterials` on
  `rs:cem_detection`): pre-existing generator projection limitation
  (`capability_catalog.cpp ioRow` drops `isArray`/`item_type`), identical
  on all 154 older sidecars — out of scope; follow-up ticket candidate.
