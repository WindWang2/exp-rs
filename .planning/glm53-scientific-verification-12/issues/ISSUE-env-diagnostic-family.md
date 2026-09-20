# Issue: the `env` diagnostic family is authored and emitted but not resolvable

**Severity:** P1 (user-visible help loss across an entire subsystem)
**Found by:** Track 02 `glm53-scientific-verification-12`, Oracle O-11
**Status on master:** **live defect**, present since `503b0d28e`
**Ownership:** `src/help/**` — **outside** Track 02's writable set, hence issued
out rather than patched.

---

## Summary

`data/help/diagnostics.json` ships **12** entries with `"family": "env"`, and
`src/geospatial/doctor/env_doctor.{h,cpp}` documents and emits
`diagnostic.env.*` ids. But the help consumer's closed vocabulary
`DiagnosticFamily` has **no `Env` enumerator**, and
`diagnosticFamilyFromName()` has **no `"env"` branch**. Every `env` page is
therefore dropped at load, and every `env-doctor` finding resolves to a blank
lookup.

## Minimal repro (no build required)

```
test_help_core.exe "[help][content]"
```

Exit code **42**. Verbatim error lines (repeated 12×, once per authored page):

```
:/help/diagnostics.json: unknown diagnostic family 'env'; :
:/help/diagnostics.json: diagnostic descriptor without DiagnosticInfo:
  diagnostic.env.gdal_drivers_empty
:/help/diagnostics.json: unknown diagnostic family 'env'; :
:/help/diagnostics.json: diagnostic descriptor without DiagnosticInfo:
  diagnostic.env.gdal_drivers_empty   (… and 11 more)
```

Assertion that fails: `tests/test_help_core.cpp:258`
`CHECK( result.errors.isEmpty() )`.

## Evidence chain (three-way, all on master)

1. **The producer declares the contract.**
   `src/geospatial/doctor/env_doctor.h:20-23`:
   > findings map to curated prose ids in `data/help/diagnostics.json`
   > (**family "env"**); the id is carried verbatim in the `diagnostic` field
   > **and must exist there** — never invented ad hoc at the call site.

2. **The producer emits the ids.** `env_doctor.cpp` emits **10** distinct
   `diagnostic.env.*` ids:
   `gdal_drivers_empty`, `gdal_driver_missing`, `gdal_data_missing`,
   `proj_db_missing`, `proj_db_unusable`, `data_dir_missing`,
   `data_dir_unresolved`, `temp_unresolved`, `temp_not_writable`,
   `unicode_path_failed`.

3. **The data authors the pages.** `diagnostics.json` contains **12**
   `family: "env"` entries — the 10 above plus `ssl_library_missing` and
   `platform_plugin_missing`, which are authored ahead of their emitters.

4. **The consumer cannot read them.** `src/help/help_id.h:51-59`:
   ```cpp
   enum class DiagnosticFamily
   {
       Harness, Operator, GeoSpatial, Dataset, Preflight, Rs,
   };
   ```
   `src/help/help_id.cpp:49-64` `diagnosticFamilyFromName()` has branches for
   `harness`, `operator`, `geospatial`, `dataset`, `preflight`, `rs` — and
   returns `std::nullopt` otherwise.

5. **A second, quieter bug in the same pair.**
   `src/help/help_id.cpp:30-47` `diagnosticFamilyName()` has no `Env` case
   either, and its fall-through returns `"rs"`:
   ```cpp
   return QStringLiteral( "rs" );
   ```
   So if `Env` is added to the enum but not to this switch, an unknown family
   is **silently relabelled as remote-sensing** rather than reported. Both
   switches must be updated together.

## Impact

Every environment self-check finding (`env-doctor`) — missing `proj.db`,
unusable `proj.db`, missing `GDAL_DATA`, empty driver set, unwritable temp dir,
unresolved temp dir, unicode path failure, missing data dir, missing SSL
library, missing platform plugin — carries a `diagnostic` id whose prose
**cannot be retrieved**. The first-run diagnosis path that `env_doctor.h`
explicitly designs for ("points at the fix, not at a loader dialog") is blank.

## Why it shipped unnoticed

`test_help_core` exercises `loadFromResources()` against `:/help`, which *does*
include `diagnostics.json`. The defect has been detectable since the commit that
introduced it; the suite is simply not part of any enforced gate. The class of
failure is: **the platform faithfully records the mismatch in an error list, and
no gate asserts the list is empty.**

## Suggested fix (for the owning track)

1. Add `Env` to `DiagnosticFamily` (`src/help/help_id.h:51-59`).
2. Add the `"env"` branch to `diagnosticFamilyFromName()`
   (`help_id.cpp:49-64`).
3. Add the `Env` case to `diagnosticFamilyName()` (`help_id.cpp:30-47`) **and
   replace the `"rs"` fall-through with an explicit unknown-value result**, so
   a future unwelded family cannot masquerade as `rs`.
4. Re-run `test_help_core "[help][content]"` → expect exit 0 and
   `result.errors.isEmpty()`.

Do **not** fix this by deleting the 12 data entries: that removes authored
content to silence a gate, and would break `env_doctor.h`'s documented output
contract.

## Related Track 02 artifact

`tests/test_help_integrity_12.cpp` (Oracle O-11) currently pins the gap
deliberately:

```cpp
CHECK( unknown == std::set<QString>{ QStringLiteral( "env" ) } );
```

so it cannot silently grow, and it will fail loudly when this issue is fixed —
prompting whoever fixes it to tighten the pin to `unknown.empty()`.

## Blame

`503b0d28e` — "feat(envcheck): Qt-free environment self-check module +
diagnostic.env.* help entries". That commit added the module, the data and the
doc comment; `src/help/help_id.h` was **never touched** in it (its last change
was PR #901, unrelated).
