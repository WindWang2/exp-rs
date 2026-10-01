# WP-A — Operator registration census (macro vs explicit), worktree `hardening/r6-registry-contract-source-truth` @ 1e28de867

Scope: every `REGISTER_RS_OPERATOR` family in `src/operators/` (rs, gdal, io, opencv, otb). All paths relative to repo root. Line numbers verified at this commit.

## 1. Registration mechanics (who populates the registry at startup)

`RSOperatorRegistry::instance()` — `src/operators/framework/rs_operator_registry.cpp:25-46` — is a
`std::call_once` singleton. Inside the once-chain it publishes the under-construction registry
through `sicnu::operators::rs::sRegistryUnderConstruction` and then runs, in order:

1. `rs::initBuiltinRsOperators()`      (rs_operator_registry.cpp:36)
2. `gdal::initBuiltinGdalOperators()`  (rs_operator_registry.cpp:37)
3. `opencv::initBuiltinOpenCvOperators()` (rs_operator_registry.cpp:39, guarded `#ifdef SICNU_HAS_OPENCV`)
4. `otb::initBuiltinOtbOperators()`    (rs_operator_registry.cpp:41)
5. `io::initBuiltinIoOperators()`      (rs_operator_registry.cpp:42)

The macro (`REGISTER_RS_OPERATOR`, `src/operators/framework/rs_operator_registry.h:80-91`) creates an
anonymous-namespace static initializer that calls `RSOperatorRegistry::instance().registerOperator(...)`.

### The #707 note (verbatim quote)

`src/operators/rs/rs_operators_init.cpp:317-319`, inside `initBuiltinRsOperators()`:

```cpp
  // The REGISTER_RS_OPERATOR static initializers in this TU are
  // dead-stripped when the linker decides no symbol is referenced, so the
  // explicit list below is the ONLY guaranteed registration path (#707).
```

Same rationale is restated per family:
- `gdal_operators_init.cpp:28-32` ("dead-strippable … the explicit list below is the guaranteed registration path (#707 — same rationale as the rs: family). The two paths are idempotent: registerOperator overwrites the map entry.")
- `io_operators_init.cpp:34-37` ("dead-strippable on some linkers, so this explicit list is the guaranteed registration path (#707 …). Idempotent.")
- `opencv_operators_init.cpp:25-29`, `otb_operators_init.cpp:24-28` (same wording).

**Dead-strip-safe path = the explicit `add()` list inside each `initBuiltin*Operators()`.**
Reason: a static initializer in a TU that nothing references can be discarded by the linker
(MSVC `/OPT:REF`, and ld with `--gc-sections`), while the `initBuiltin*Operators()` functions are
directly referenced from `instance()`'s once-chain, so their `add()` calls always run. Both paths
are idempotent (`registerOperator` overwrites the map entry, `rs_operator_registry.cpp:48-51`), so
the macro path is a redundant best-effort duplicate, not a second source of truth.

Re-entrancy guard: family init functions take the registry from `sRegistryUnderConstruction`
(never `instance()`) — calling `instance()` from inside the once-chain re-runs the chain and
clears `m_factories` (#707), see `rs_operators_init.cpp:307-315` and the comment at
`rs_operator_registry.cpp:28-35`.

## 2. Per-family macro registration sites

All macros live in the family init TU. There are no `REGISTER_RS_OPERATOR` uses anywhere else in
`src/` outside these five files plus the macro definition and the text-scanning census tool
`src/contracts/determinism_census.cpp:96-122` (which greps the macro shape and `registerOperator`).

| Family | File | Macro lines | Count |
|---|---|---|---|
| rs | `src/operators/rs/rs_operators_init.cpp` | 131-293 (two blocks; `#ifdef SICNU_HAS_OPENCV` block at 275-293) | 157 |
| gdal | `src/operators/gdal/gdal_operators_init.cpp` | 19-23 | 5 |
| io | `src/operators/io/io_operators_init.cpp` | 18-30 | 13 |
| opencv | `src/operators/opencv/opencv_operators_init.cpp` | 17-22 (whole TU under `SICNU_HAS_OPENCV`) | 6 |
| otb | `src/operators/otb/otb_operators_init.cpp` | 18-21 | 4 |
| **total macros** | | | **185** |

## 3. Per-family explicit `add()` sites

| Family | Init function | File:lines | Count |
|---|---|---|---|
| rs | `initBuiltinRsOperators()` | `src/operators/rs/rs_operators_init.cpp:307-485` (adds at 323-484) | 157 |
| gdal | `initBuiltinGdalOperators()` | `src/operators/gdal/gdal_operators_init.cpp:25-44` (adds at 39-43) | 5 |
| io | `initBuiltinIoOperators()` | `src/operators/io/io_operators_init.cpp:32-61` (adds at 44-60) | 17 |
| opencv | `initBuiltinOpenCvOperators()` | `src/operators/opencv/opencv_operators_init.cpp:24-42` (adds at 36-41) | 6 |
| otb | `initBuiltinOtbOperators()` | `src/operators/otb/otb_operators_init.cpp:23-39` (adds at 35-38) | 4 |
| **total explicit** | | | **189** |

## 4. Macro-vs-explicit diff per family

Machine-verified by parsing both shapes (macro `REGISTER_RS_OPERATOR(Class, "id")` vs
`add("id", [] { return std::make_unique<Class>(); })`), comparing id sets and class pairings.

- **rs: 157 macro / 157 explicit — zero diff** (no macro-only, no explicit-only, no class mismatch
  for the same id). PR #1398's alignment still holds on today's master; exactly 157 operators on
  both sides (12 SAR/temporal operators from #1398 remain in both lists).
- **gdal: 5 / 5 — zero diff.**
- **io: 13 macro / 17 explicit — 4 explicit-only**: `io:catalog_search` (init line 54),
  `io:cube_plan` (55), `io:cube_window` (56), `io:cache_prefetch` (57). Their classes are declared in
  `src/operators/io/io_fabric_operators.h:24,45,64,83` (`IoCatalogSearchOperator`,
  `IoCubePlanOperator`, `IoCubeWindowOperator`, `IoCachePrefetchOperator`). The io init TU includes
  `io_fabric_operators.h` (line 4) but never wrote macros for these four. **Direction of drift is
  the SAFE one** (explicit-only cannot be dead-stripped), but the macro list understates the family
  and any tooling that reads the macro shape (e.g. `determinism_census.cpp` regex at :108) sees 13,
  not 17.
- **opencv: 6 / 6 — zero diff.**
- **otb: 4 / 4 — zero diff.**
- **Macro-only (MSVC dead-strip exposure): NONE in any family.** The #1398 exposure class is closed.

### JSON summary

```json
{
  "families": {
    "rs":     { "macro": 157, "explicit": 157, "macro_only": [], "explicit_only": [] },
    "gdal":   { "macro": 5,   "explicit": 5,   "macro_only": [], "explicit_only": [] },
    "io":     { "macro": 13,  "explicit": 17,  "macro_only": [], "explicit_only": ["io:catalog_search", "io:cube_plan", "io:cube_window", "io:cache_prefetch"] },
    "opencv": { "macro": 6,   "explicit": 6,   "macro_only": [], "explicit_only": [] },
    "otb":    { "macro": 4,   "explicit": 4,   "macro_only": [], "explicit_only": [] }
  },
  "totals": { "macro": 185, "explicit": 189, "registered_ids": 189 },
  "macro_only_total": 0,
  "explicit_only_total": 4
}
```

## 5. Declared-but-unregistered operator classes

Scan of every `class … : public <base>` in `src/operators/**/*.h` against the union of classes on
both registration paths (189 ids / 189 classes). Classes declared but on NEITHER path:

| Class | Site | Verdict |
|---|---|---|
| `IoOperatorBase` | `src/operators/io/io_operators.h:17` | abstract base, not an operator |
| `OpenCvOperatorBase` | `src/operators/opencv/opencv_operator_base.h:40` | abstract base |
| `OtbOperatorBase` | `src/operators/otb/otb_operator_base.h:47` | abstract base |
| `RSOperatorError` | `src/operators/framework/rs_operator_error.h:83` | error type, not an operator |
| `RsFusionMethodOperator` | `src/operators/rs/rs_fusion_aliases.h:17` | abstract shared base of the 5 registered `rs:fusion_*` aliases (pure-virtual `name()`/`methodName()`); the five concrete subclasses are registered on both paths |

**No concrete operator class is registered on neither path.** Note: `src/operators/rs/rs_cn_import_operator.h`
looks like an operator by filename but contains only inline metadata-stamping helpers
(`writeCnImportMetadata`, `cnMissingDeclaredFields`) — not an `RSOperator` and correctly unregistered.
The `src/operators/python/` tree (`pybind_sicnu_operators.cpp`) exposes operators to Python; it adds no
registry entries.

## 6. Runtime visibility: how capability/agent layers observe the registry

1. **Static provider wiring.** `rs_operators_init.cpp:298-303` — a file-static
   `AtomicRsOperatorProviderRegistration` runs `installRsOperatorProvider()` at static-init time.
   `installRsOperatorProvider()` (`rs_operators_init.cpp:487-505`) forces `RSOperatorRegistry::instance()`
   (which runs the whole once-chain, registering all 189 operators) and calls
   `sicnu::processing::AtomicAlgorithmRegistry::setRsOperatorProvider(...)` with a callback that
   enumerates `operatorNames()`, `create()`s each operator and wraps it in
   `sicnu::processing::RsOperatorAdapter` via `registry.registerAdapter(adapter)`.
2. **Provider replay.** `AtomicAlgorithmRegistry::initialize()` / `reset()`
   (`src/processing/framework/atomic_algorithm_registry.cpp:28-35,53-64`) re-invoke the stored provider
   (idempotent: adapters overwrite), so population does not depend on static-init ordering.
3. **Early eager callers** (all force the chain before first use):
   `src/contracts/tool/contract_inventory_main.cpp:139`, `src/agent/spatial_tools/explain_step_tool.cpp:246`,
   `src/app/main_window_workbench.cpp:489`, `src/help/adapters/operator_help_source.cpp:19`.
4. **Observers downstream**: help (`operator_help_source.cpp`), the explain layer
   (`src/explain/adapters/registry_operator_knowledge.cpp`), the contract inventory tool, the
   capability sidecar pipeline (`data/processing/algorithm_meta/capability/*`, D8), and plugin
   runtime via `unregisterOperator` (`rs_operator_registry.h:41`, plugin unload).

## 7. Existing duplicate-prevention infrastructure (do not rebuild)

- `src/contracts/determinism_census.cpp` — bounded text scan of `src/**` for the exact
  `REGISTER_RS_OPERATOR\s*\(\s*(\w+)\s*,\s*"([^"]+)"\s*\)` shape plus `registerOperator` needles
  (lines 96-122), used by the Platform 11.0 determinism/contract census.
- The registry once-chain itself makes the explicit list authoritative; the macro list is
  documentation-grade. A future cleanup could delete macros entirely or generate both lists from one
  table; until then the io family's 4 missing macros are the only asymmetry.
