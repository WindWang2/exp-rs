# Boundary Fuzz Corpus — Track `ds41-fuzz-boundaries`

Every entry below was FOUND by the lanes in `tests/test_contract_fuzz_frame.cpp`,
`tests/test_contract_fuzz_payload.cpp` and `tests/test_contract_fuzz_paths.cpp`
(deterministic seeds + hard caps, see `tests/support/fuzz_corpus.h`), reproduced,
root-caused, and either fixed in-tree or recorded as an observation. Nothing here
is a speculative finding: each row has the failing assertion, the minimized
input class and the production site.

Conventions
- "Fixture" = the smallest input class that still triggers the behaviour
  (for throwing-parser defects the delta-debugging result is 1-minimal, see the
  `corpus minimization` lane; for stack-overflow defects the depth-bomb is
  minimal by construction — removing a level removes the overflow).
- Severity uses the repo's P0–P2 scale. All fixed items carry a regression in
  the corresponding lane, so the defect cannot come back silently.

---

## D1 — IPC frame/envelope decode is not total on deeply nested JSON (P0)

**Symptom (pre-fix, Debug build):**

```
ipc envelope fuzz: depth and size bombs are typed refusals
tests/test_contract_fuzz_frame.cpp(448): FAILED:
  REQUIRE_NOTHROW( decoded = Ipc::decodeEnvelopePayload( bomb, envelope, error ) )
due to a fatal error condition:
  SIGSEGV - Stack overflow
```

Bisected with a standalone probe (`depthprobe`): the process died on
`{"a":[[[…1…]]]}` at **depth 866** (≈1.7 KB payload, far below the 32 MiB frame
cap) and at depth ≈900 in an `-O2` build. Depth 865 parsed and was refused typed.

**Root cause**: `exprs::Ipc::decodeEnvelopePayload`
(`src/sdk/exprs/ipc_envelope.cpp`) built a `Json::CharReaderBuilder` with the
library defaults, which do not bound the reader's recursion usefully in this
build; the same shape was used by `parseFrame`
(`src/runtime/worker/worker_protocol.h`), which crashes at depth ≈890 with the
same payload family. An untrusted peer frame (or a corrupted stream) killed the
process instead of producing E6002.

**Fix**: explicit `stackLimit` (64 — the documented protocol is shallow; large
artifacts travel as file references, never raw JSON) **plus** a try/catch, because
jsoncpp THROWS `Exceeded stackLimit in readValue()` rather than returning false —
the naive "set stackLimit" hardening alone moves the failure from SIGSEGV to an
escaping exception.

**Regression**: `tests/test_contract_fuzz_frame.cpp` —
"ipc envelope fuzz: depth and size bombs are typed refusals" pins depths
16/64/256/866/4096/65536/200000 as typed refusals; the pinned 866 is the
pre-fix crash boundary.

**Also fixed (same class, file-based surface)**: `PluginManifest::loadManifestFromFile`
(`src/sdk/exprs/plugin_manifest.cpp`), `loadIndex` (`src/sdk/exprs/plugin_discovery.cpp`)
and `PluginRegistry::loadUserIndex` (`src/sdk/exprs/plugin_registry.cpp`) used the
legacy `Json::Reader`, which cannot bound its recursion at all — a ~40 KB
`plugin.json` nested 20 000 levels deep stack-overflow the caller (probe:
`loadManifestFromFile` survived depth 900 and died at 20 000). All three now parse
through the bounded, guarded reader with identical diagnostics shape
("invalid JSON: …").

## D2 — Worker UI schema with a wrong-typed `commandId` throws out of the validator (P1)

**Symptom (pre-fix):**

```
tests/test_contract_fuzz_payload.cpp(267): FAILED:
  REQUIRE_NOTHROW( result = exprs::validatePluginUiSchema( mutated ) )
due to unexpected exception with message:
  Type is not convertible to string
```

**Minimized fixture** (1-minimal by delta debugging, see the lane):

```json
{"version":1,
 "commands":[{"id":"cmd.refresh","title":"Refresh"}],
 "menuItems":[{"id":"menu.refresh","title":"Refresh now","commandId":42}]}
```

Also fires through `contextActions` (same code path).

**Root cause**: `validateEntries` (`src/sdk/exprs/plugin_ui_schema.cpp`) built its
failure message with `commandId.asString()` outside the branch that had proved the
value is a string. jsoncpp's `asString()` throws `Json::LogicError` for a
number/object/array, so the validator itself — the component whose documented
contract is "must fail VALIDATION, never throw through the worker" — threw. The
three call sites (`plugin_host_worker_main.cpp`, `plugin_host_process_runtime.cpp`,
`plugin_ui_schema_host.cpp`) all catch it, so there is no process-level crash, but
every new call site would inherit an exception boundary.

**Fix**: type-check before conversion; a non-string `commandId` is now the typed
error "commandId must be a bounded string".

**Regression**: `tests/test_contract_fuzz_payload.cpp` —
"plugin ui schema fuzz: worker-supplied commandId of the wrong type fails typed
instead of throwing" (all five wrong types × menuItems and contextActions) and
the delta-debugging lane that asserts the reduced fixture is 1-minimal.

## D3 — `ManifestPort::fromJson` throws on a non-boolean `required` (P2)

**Symptom (pre-fix):**

```
tests/test_contract_fuzz_payload.cpp(533): FAILED:
  REQUIRE_NOTHROW( ok = ManifestPort::fromJson( mutated, parsed, error ) )
due to unexpected exception with message:
  Value is not convertible to bool.
```

**Minimized fixture**: `{"name":"input","required":"yes"}` (and `1`, `[]`, `{}`).

**Root cause**: `out.required = json.get("required", false).asBool();`
(`src/sdk/exprs/plugin_manifest.cpp`) — `get()` returns the PRESENT value, and
`asBool()` throws for a string/array/object. #1038 hardened the sibling helpers
(`requireString`, …) but not this field.

**Fix**: the field is type-checked before the cast; a non-boolean `required` is a
typed field error ("'required' must be a boolean").

**Regression**: `tests/test_contract_fuzz_payload.cpp` — "manifest port fuzz"
mutated-totality leg plus the four directed wrong-typed cases.

## D4 — `PluginDiagnostic::fromJson` throws on hostile records (P2)

**Symptom (pre-fix):**

```
tests/test_contract_fuzz_payload.cpp(613): FAILED:
  REQUIRE_NOTHROW( parsed = PluginDiagnostic::fromJson( mutated ) )
due to unexpected exception with message:
  in Json::Value::find(begin, end): requires objectValue or nullValue
```

**Minimized fixture**: any non-object value (`[1,2]`, `"text"`, `42`) or
`{"code":42,"message":7}` (wrong-typed members).

**Root cause**: `PluginDiagnostic::fromJson` (`src/sdk/exprs/plugin_diagnostics.cpp`)
called `json.get(key, default).asString()` without checking that the value is an
object (jsoncpp's `find()` throws for non-object roots) or that the member is a
string. This is the public deserializer for diagnostic records that travel over
IPC and files.

**Fix**: an `isObject()` guard plus a type-checked per-field reader; unknown or
wrong-typed members fall back to the documented defaults.

**Regression**: `tests/test_contract_fuzz_payload.cpp` — "diagnostic records
fuzz" asserts totality on mutated/random records and keeps the message verbatim
when it is a string.

## D5 — `PathPolicy` throws for UTF-8 text the ANSI code page cannot express (P2)

**Symptom (pre-fix):**

```
tests/test_contract_fuzz_paths.cpp(105): FAILED:
  REQUIRE_NOTHROW( rejection = PathPolicy::checkRelativeLexically( candidate ) )
due to unexpected exception with message:
  在多字节的目标代码页中，没有此 Unicode 字符可以映射到的字符。
```
(std::system_error from the MSVC code-page conversion)

**Minimized fixture**: `unicode-é中.txt` (valid UTF-8) and invalid UTF-8
(`"\xFF\xFE"`, `"a\x80b"`). The SAME input is accepted on a US-English Windows
host and throws on a zh-CN host — a locale-dependent totality violation in the
containment policy that guards plugin payloads and workspace effects.

**Root cause**: `std::filesystem`'s `std::string` constructor decodes with the
process' ANSI code page on MSVC and throws — constructionally for some inputs and
LAZILY (during component iteration / comparison) for others.

**Fix**: paths are built through the UTF-8 decoding constructor
(`fs::path(std::u8string)`) and every public policy function converts any
platform-conversion surprise into a typed rejection (`NotCanonical` for the
lexical/containment checks, empty/false for `canonical`/`isAbsolute`).

**Regression**: `tests/test_contract_fuzz_paths.cpp` — the corpus + mutation
legs assert totality, and the independent resolution oracle (the test's own
`std::filesystem` walk, now UTF-8 guarded) must agree with the policy verdict on
every accepted path.

## Observations (documented, not defects)

- **O1 — empty dependency range means "any version".** `versionSatisfiesRange(v, "")`
  is satisfied by every version (documented bare-id semantics). Asserted in the
  lane instead of being treated as a failure.
- **O2 — trailing newline in a version range.** `">=1.2.3\n"` parses as
  `">=1.2.3"` because the last `std::getline` uses `'\n'` as its default
  delimiter. Harmless leniency, recorded; asserted as a tolerance.
- **O3 — pre-existing, out of scope.** `isCredentialQueryKey("api-key")` is false
  (the denylist spells `apikey`/`api_key` only) — already recorded by the io lane's
  fault matrix, owned by the geospatial track; left untouched here.
- **O4 — the same unbounded-reader class exists at ~20 other `CharReaderBuilder`
  sites** (agent, cli, workflow, geospatial, help, app). Those files belong to
  other tracks; the two IPC entry points fixed here are the plugin-boundary
  surfaces this track owns. Evidence: `grep -rn CharReaderBuilder src/`.

## How to reproduce any row

```bash
# from the worktree build dir, with the lane binaries built
ctest -R "ipc envelope fuzz: depth" --output-on-failure        # D1
ctest -R "commandId of the wrong type" --output-on-failure     # D2
ctest -R "manifest port fuzz" --output-on-failure              # D3
ctest -R "diagnostic records fuzz" --output-on-failure         # D4
ctest -R "path policy fuzz" --output-on-failure                # D5
```

To see a defect re-appear, revert only the corresponding production hunk and
re-run the lane — every row above is a red/green test, not a narrative claim.

---

## Sanitizer lane (ASan)

The three lanes were compiled with the repository's own sanitizer option
(`ENABLE_SANITIZERS=ON`, i.e. the `sanitizer-debug` preset's cache variables)
into a separate build directory and run with `-j1` builds:

```
cmake --preset dev-default -B build-san -DENABLE_SANITIZERS=ON ...
ninja -C build-san -j1 test_contract_fuzz_frame test_contract_fuzz_payload \
                     test_contract_fuzz_paths
ASAN_WIN_CONTINUE_ON_INTERCEPTION_FAILURE=1 ASAN_OPTIONS=detect_leaks=0 \
  ./test_contract_fuzz_<lane>.exe
```

Result: all three lanes pass with **zero AddressSanitizer reports**
(no heap overflow, no use-after-free, no UB report; leak detection is off per
the repository's documented policy — QGIS/Qt singleton "leaks" are intentional,
see CMakeLists.txt around ENABLE_SANITIZERS).

`ASAN_WIN_CONTINUE_ON_INTERCEPTION_FAILURE=1` is required only because the MSVC
ASan runtime cannot intercept the non-instrumented jsoncpp/Git-Bash host calls;
it does not disable any check.

## Review disposition (independent reviewer, read-only, `origin/master...HEAD`)

Verdict was SHIP-WITH-FIXES; every finding is dispositioned below.

| Finding | Sev | Disposition |
|---|---|---|
| F1 `parseFrame` had the depth bound but no try/catch (jsoncpp throws) | P1 | **FIXED** — same guard as the envelope decoder, plus a new lane case that feeds parseFrame depth bombs and wrong-shaped frames. |
| F2 `stackLimit=64` could reject legal ui-event values (byte cap allows ~2000 levels) | P1 | **FIXED** — new documented `PluginUiSchemaLimits::maxEventValueDepth = 32` enforced by an iterative walk; transport bound raised to 128 (legal event + wrapper fits with headroom); lane pins a 20-deep event payload decoding. |
| F3 manifest depth-bomb leg never wrote its fixture | P2 | **FIXED** — both shapes (unknown field, versioned manifest) are now written and loaded. |
| F4 dead helpers + a size leg that did not exist | P2 | **FIXED** — `jsonAtPath`, `FaultRecorder`, `sizeBomb` removed; case renamed to what it asserts; the real over-cap-payload leg (no partial bytes on the stream) added. |
| F5 unreachable branch in the truncation oracle | P2 | **FIXED** — removed. |
| F6 D5's pin was code-page dependent | P2 | **FIXED** — invalid-UTF-8 entries added to the path corpus, so the totality pin is red on every host. |
| F7 DEDUP claimed a `validateUiEvent` defect that master already guards | P2 | **FIXED** — DEDUP corrected; the test leg is now explicitly a totality assertion. |
| F8 `NotCanonical` semantics widened undocumented | P2 | **FIXED** — enum/function docs state the un-representable case. |
| F9 `resolvedPath` encoding undocumented | P2 | **FIXED** — documented as the platform narrow encoding (the form callers feed back into std::filesystem). |
| F10 legacy `Json::Reader` grammar narrowed (comments) | P2 | **FIXED** — `allowComments` preserved on all three migrated readers. |
| F11–F14 (unused constant, missing `<fstream>`, OWNERSHIP wording, PR_BODY.md ignore) | NIT | **FIXED** — plus PR_BODY.md stays out of the repository entirely (the PR is created directly with `gh pr create`). |
