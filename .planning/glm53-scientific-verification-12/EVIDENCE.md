
## E-16h · Executed gate evidence (compiled + run, twice consecutively)

The lane is not merely replayed in Python; it was compiled with `cl.exe` at
`/W4` (EXIT=0, zero warnings) and executed against the live worktree.

```
$ ./.v12-evidence/test_data_parse_integrity_12.exe
Randomness seeded to: 606915354
===============================================================================
All tests passed (6 assertions in 1 test case)
EXIT=0

$ ./.v12-evidence/test_data_parse_integrity_12.exe      # consecutive
Randomness seeded to: 3193077675
===============================================================================
All tests passed (6 assertions in 1 test case)
EXIT=0
```

Verbose (`-s`) values:

```
json files under data/: 609
parsed OK: 586 / 609
tolerated (filed P0): 23
```

`586` rather than `584` is the gate doing its job: this track already repaired
`help/commands.json` (E-15) and `agent/capabilities/preprocess.json`, so those
two now parse. The gate's third assertion fired on the first execution and
refused to pass until they were pruned from the allowlist:

```
stale allowlist entry: agent/capabilities/preprocess.json now parses but is still allowlisted
stale allowlist entry: help/commands.json now parses but is still allowlisted
```

That is the anti-rot property demonstrated **on the real gate, not in a
simulation**: the allowlist cannot outlive the defect. After pruning both
entries and lowering `kMaxTolerated` 25 -> 23, the gate is green — and it is
*tighter* than when it was written.

### Note on the standalone build

The worktree's CMake generate step remains blocked by the sandbox's `reg.exe`
blacklist, so the lane was compiled and linked directly. The recipe (kept in
`.v12-evidence/` as raw evidence, not committed) is:

```
cl /std:c++17 /Zc:__cplusplus /permissive- /utf-8 /bigobj /W4 /MDd /D_DEBUG
   -DCMAKE_SOURCE_DIR=\"<worktree>\"
   -I<worktree>/src
   -external:I<worktree>/build-v12/vcpkg_installed/x64-windows/include
   -external:I<catch2-src>/src -external:I<catch2 generated-includes>
   -external:I<Qt>/include -external:I<Qt>/include/QtCore -external:W0

link /SUBSYSTEM:CONSOLE <obj>
   sicnu_runtime.lib jsoncpp.lib Catch2Maind.lib Catch2d.lib Qt6Cored.lib shell32.lib
```

Three details cost real time and are worth recording: the worktree has no
configured `catch2-src`, so the warm main repo's `_deps/catch2-build` must
supply both the headers and the generated `catch_user_config.hpp`; Qt requires
`/Zc:__cplusplus` **and** `/permissive-`; and `sicnu_runtime` is a SHARED
library, so `sicnu_runtime.dll` must sit beside the executable or it exits 127
with no message.
 across the merge:

```
REGRESSIONS INTRODUCED BY 43dcf19cd: 19   (OK -> FAIL, every one)
```

## E-16c · Family B — 4 files truncated to zero bytes

A second merge, `4713528ef` (`merge: land PR #1021 (spectral-intelligence-11)`),
emptied four sidecars that were non-empty on the master side:

| file | `4713528ef^1` | `4713528ef^2` | `4713528ef` |
| --- | --- | --- | --- |
| `algorithm_meta/rs-temporal-extract-regions.json` | 450 B | absent | **0 B** |
| `algorithm_meta/rs-temporal-harmonic-breaks.json` | 552 B | absent | **0 B** |
| `algorithm_meta/rs-temporal-region-features.json` | 550 B | absent | **0 B** |
| `algorithm_meta/rs-temporal-regularize.json` | 573 B | absent | **0 B** |

The `capability/` siblings of all four survive intact (5964–6609 bytes), which is
the only reason this is recoverable rather than a true data loss.

## E-16d · Live blast radius, and why nothing noticed

The two consumers disagree about how loudly to fail.

| consumer | path | behaviour on parse failure |
| --- | --- | --- |
| `CapabilityCatalog::reload` | reads `algorithm_meta/capability/` | pushes `"<file>: <errors>"` into `mLoadProblems`, then skips the entry |
| `AlgorithmMetaStore::loadFromDirectory` | reads `algorithm_meta/` | **plain `continue`, no record, no counter, no diagnostic** |

`AlgorithmMetaStore` is the one wired into the MCP server
(`src/agent/mcp_server.cpp:336` → `loadDefaults()`). Its loader is:

```cpp
const QJsonDocument doc = QJsonDocument::fromJson( handle.readAll() );
if ( !doc.isObject() )
  continue;                       // <-- 24 files disappear here, silently
```

Live arithmetic at `origin/master`:

| directory | total | parse OK | **silently dropped** |
| --- | --- | --- | --- |
| `data/processing/algorithm_meta/` | 51 | 47 | **4** |
| `data/processing/algorithm_meta/capability/` | 139 | 120 | **19** |

And the gate that looks like it covers this does not, because **it points at a
different directory**:

```cpp
// tests/test_capability_drift.cpp:63
CapabilityKnowledge &knowledge = CapabilityKnowledge::instance();
knowledge.setDirectory( std::string( CMAKE_SOURCE_DIR ) + "/data/agent/capabilities" );
REQUIRE( knowledge.loadProblems().empty() );   // green — wrong corpus
```

`data/agent/capabilities/` is a *third* corpus. Every assertion in that suite is
true and vacuously reassuring with respect to the 23 dropped sidecars. This is
the same shape as E-15: **a real, recorded, unasserted failure signal.**

## E-16e · Relationship to E-15

E-15 (`commands.json`) is Family A applied to the help corpus, introduced by a
different merge (`24ea7ab9e`, PR #1028). `preprocess.json` and `diagnostics.json`
are the same family again. So the track's root-cause statement is now:

> Merges in this repository are landing **data files that do not parse**, and no
> gate in the suite is shaped to notice — because the only assertions on the
> affected corpora either (a) hash a snapshot against itself, (b) inspect the
> wrong directory, or (c) read a failure list that nothing requires to be empty.

Three independent instances (E-15, Family A's 19, Family B's 4) is no longer a
coincidence to be patched file by file. It is the class this track exists to
close, and it argues for a **whole-corpus parse gate** rather than 25 repairs.

## E-16f · Disposition

| item | action | rationale |
| --- | --- | --- |
| Whole-corpus parse gate | **IN SCOPE — this track** | it is a verification-platform capability, which is exactly the track contract |
| `AlgorithmMetaStore` silent `continue` | **issue, not a fix here** | `src/processing/**` is read-only to this track; the fix belongs to the owning track, and the minimal repro is this section |
| The 25 damaged files | **issue (P0), with attribution** | repairing 25 files across three corpora by hand outruns the track's ownership boundary and would hide the systemic cause |
| `test_capability_drift.cpp` pointing at the wrong directory | **issue** | changing another track's assertion target silently is not this track's call; the new gate covers the uncovered corpus instead |

The rule from D-12 still governs: **repair when the authored content is uniquely
recoverable from history and the repair is provably a restoration; file an issue
when it is not.** Here it is not — there are 25 files, two distinct mechanisms,
and a live producer that will re-break them.
ps the page** on any other value.
2. **Genuine duplicate ids.** `runtime_provider_failed` at lines 884/2169 and
   `policy_refused` at 2097/2188. `help_registry.cpp:34` **rejects**
   duplicates, so the later copies never register but still emit errors.

   **Verified by execution + byte comparison (R1.1).** master = **132**
   entries; repaired = **130**; **2 removed, 0 added, 0 ids lost**.
   - `runtime_provider_failed`: line 884 `retry='manual'` (**valid**,
     authentic) kept; line 2169 `retry='retryable'` (**invalid**, appended
     tail) removed. Surviving object is **byte-identical** to the line-884
     copy.
   - `policy_refused`: line-2097 copy kept; its `related` is
     `["command.workbench.operatorCatalog"]` — an id that resolves. (The
     `workbench.plugin_manager` value belongs only to the *discarded tail
     copy*; corrected here.)
   - No other id's content changed (`changed == []`).
3. **THE BIG ONE — the `env` family does not exist.**
   `help_id.h:51-59` declares the closed vocabulary
   `harness | operator | geospatial | dataset | preflight | rs`. There is **no
   `Env` enumerator**, and `diagnosticFamilyFromName()` (`help_id.cpp:49-64`)
   has no `env` branch. `diagnosticFamilyName()` (`help_id.cpp:30-47`)
   likewise has no `Env` case — and its fall-through returns `"rs"`, silently
   mislabelling an unknown family as remote-sensing. Yet
   `data/help/diagnostics.json` authors **12** `diagnostic.env.*` pages.

### E-8b · The contract was declared on both sides and never welded

- `src/geospatial/doctor/env_doctor.h:20-23` states it explicitly:
  > "findings map to curated prose ids in `data/help/diagnostics.json`
  > (**family "env"**); the id is carried verbatim in the `diagnostic` field
  > **and must exist there**"
- `env_doctor.cpp` emits **10** `diagnostic.env.*` ids: `gdal_drivers_empty`,
  `gdal_driver_missing`, `gdal_data_missing`, `proj_db_missing`,
  `proj_db_unusable`, `data_dir_missing`, `data_dir_unresolved`,
  `temp_unresolved`, `temp_not_writable`, `unicode_path_failed`.
- `diagnostics.json` authors **12** pages — those 10 **plus**
  `ssl_library_missing` and `platform_plugin_missing`.
- The consumer simply never learned `env`.

**CORRECTION.** An earlier version of this entry said `env_doctor.cpp` emits
"exactly 11" ids. The count is **10**, and the authoritative number is **12
authored pages**. The extra two have no emitter in `env_doctor.cpp`. The
executed gate settles it: 12 `unknown diagnostic family 'env'` errors, one per
authored page. Lesson: count from the **executed** error list, not from a
hand-scan of one producer's call sites.

**Blame:** `503b0d28e` added the module, the data and the doc comment;
`src/help/help_id.h` was **never touched** in it (last changed by PR #901,
unrelated).

**Consumer-visible impact.** Every `env-doctor` finding carries a `diagnostic`
id that resolves to **no help page**. Diagnosis for missing `proj.db`,
unreadable temp dir, unicode path failure, missing SSL library or missing GDAL
driver is blank. Detectable since that commit; simply not in anyone's gate.

### E-8c · Disposition (ownership)

The correct fix is to **add `Env` to `DiagnosticFamily`** — implementing the
contract two other files already declare — *not* to delete the 12 data entries
(that would be deleting data to force green, and would break `env_doctor`'s
documented output contract).

`src/help/**` is **outside this track's ownership**, so per the brief:
**minimal repro + out-of-scope issue**, with the data repair and the gate done
in scope where they properly belong.

Minimal repro (one command, no build):
```
test_help_core.exe "[help][content]"     # exit 42
```

---

# Part 4 — The headline structural defect

## E-13 · The graph's harness vocabulary is a strict subset of the live taxonomy

**How it surfaced.** Generating the *live* graph from the worktree with the
already-built tool (no new compilation):

```
contract_inventory --source-root . --out live_graph.json
nodes: 1147 edges: 426 findings: 5
note: unreadable help file: ./data/help/commands.json
finding: dangling_ref diagnostic_for:harness/ACQUISITION_DATES_MISSING
finding: dangling_ref diagnostic_for:harness/COMPLEX_BANDS_REQUIRED
finding: dangling_ref diagnostic_for:harness/DATES_NOT_ASCENDING
finding: dangling_ref diagnostic_for:harness/IO_ERROR
finding: dangling_ref diagnostic_for:harness/UNWRAP_PROVIDER_UNAVAILABLE
```

**Root cause, proven by reading the assembler.**

`graph_assembly.cpp` builds the harness `error_code` node set from **one file
only** — the `kXxx` constants in `harness_error.h`:

```cpp
errScanner.scanHarnessCodes(
    readFile( joinPath( sourceRoot, "src/agent/harness/harness_error.h" ) ),
    errReport );
...
for ( const auto &[var, code] : errReport.harnessCodes )   // kXxx constants ONLY
    g.addNode( { "harness/" + code, ... } );
```

then creates a `diagnostic_for` edge for **every** `diagnostics.json` entry with
`family == "harness"` — **without checking that the target node exists**:

```cpp
if ( family == "harness" || family == "operator" )
    g.addEdge( { "diagnostic_for", id, family + "/" + code, "diagnostics.json" } );
```

The authoritative harness vocabulary is **split across two files**:

| source | codes |
|---|---|
| `harness_error.h` — `kXxx` constants (what the graph scans) | **40** |
| `harness_error.cpp` — `errorCategoryForCode` table | **66** |

**26 codes exist in the `.cpp` table with no `kXxx` constant in the `.h`**,
including all five named above. They are live, classified, retry-policied codes,
used in `capability_catalog.cpp:41-42`,
`spatial_tools/result_assessment_tool.cpp:171,276` and
`processing/algorithms/sar/sar_unwrap_provider.cpp:86`. The graph's harness
vocabulary is therefore a **strict subset** of reality, and every diagnostic
documenting a `.cpp`-only code dangles.

**Why the committed snapshot did not catch this — the sharper point.**
The committed `contract_graph.snap.json` reports **0** dangling targets. It also
has **no node** for any of the 5 codes, and only **52** `diagnostic_for` edges
versus a larger live set. The snapshot is simply **from an earlier tree**: the 5
diagnostics were added by a later fail-closed PR and the snapshot was never
regenerated.

So the failure ran in the **worse** direction: the byte-gate went red for *stale
byte content*, which **masked** the fact that a fresh generation reports
**structural findings**. A maintainer regenerating to "make the gate green"
would have blessed 5 dangling edges into the new baseline.

**This is the strongest justification for 12.0's drift work:** a byte comparison
is the wrong *shape* of gate. It cannot distinguish "content moved" from "the
graph no longer holds together". A regenerated snapshot must be gated on
`computeFindings() == empty`.

**In scope?** The **detector** is (`src/contracts/**` is owned;
`computeFindings()` already exists and the gate ignores it). The **26-code
split** is a `src/agent/harness/**` concern — out of ownership → minimal repro +
issue. I do **not** patch `harness_error.h`, and I do **not** delete the 5
diagnostics to make findings vanish.

## E-13b · A second, quieter note in the same output

```
note: unreadable help file: ./data/help/commands.json
```

`graph_assembly.cpp:151-157` reads every `data/help/*.json` and requires it to
be a JSON **array**; anything unreadable or non-array is recorded as a note and
skipped. `commands.json` triggers it. This is the unresolved R1.7 question, now
localized to one file. Investigating whether `commands.json` is an object (a
legitimate shape the scanner does not accept) or genuinely malformed is R1.2
work, because it decides whether the regenerated snapshot would silently drop
command help nodes.

**RESOLVED — see E-15.** The file is a genuine JSON array that is genuinely
malformed (not an object-shaped corpus the scanner rejects). It is repairable to
its authored content, which is what I did. The question this section posed is
answered: the regenerated snapshot *was* silently dropping command help nodes,
by exactly the amount E-15 measures.

---

# Part 5 — Process corrections (my own errors, kept on the record)

## E-9 · Retracted: there is NO harness-code drift

I extracted the declared vocabulary with an arbitrary line window
(`sed -n '27,66p' harness_error.h`). That window truncated the namespace and
omitted 5 codes, so comparing the graph against it produced a plausible-looking
"5 codes missing from the taxonomy" defect. It was an artifact of my method.

All uppercase string literals in the file: **40**; all 5 codes **are** declared.
Every graph `harness/*` code resolves. **No drift.** Also, the committed graph
is internally sound: **0 dangling edges**.

The drift detector must **parse the identifier list**
(`inline constexpr const char *k[A-Za-z0-9_]+ = "…"`), never slice by line
number. E-13 is the real version of this finding, obtained the correct way.

## E-12 · Note on method

Two independent times on this track an over-eager slice produced a defect that
did not exist (E-0, E-9). Both were caught by *executing* something rather than
reading it. That is the operating rule for the remainder of the track: prefer an
executed observation over a code reading, and when a reading is unavoidable,
state the exact command that would falsify it.

---

# Part 6 — Verified repairs

## E-11 · R1.1 repairs, verified shape (executed, not asserted)

Both committed-data repairs are established by **byte-level comparison of master
against the repaired tree**, plus an independent parser, plus the executed gate.

### E-11a · `data/agent/capabilities/preprocess.json`

Diff is exactly three removed lines:

```
-    "intents": ["preprocess"],
-    "resource": { "cost_class": "heavy", "large_raster_safe": true },
-    "limitations": ["Requires a DEM and RPC/TIMING geometry; wrong DEM datum shifts the product"]
```

The well-formed triplet immediately above is untouched, so authored values are
preserved verbatim. Independent parse across the family:

```
files=15  total_entries=247  parse_failures=0
preprocess.json entries = 21
per-file: change 11, classify 14, filter 17, fusion 7, geometric 4,
          inference 8, io 19, otb 5, preprocess 21, sar 24, spectral 15,
          spectral_transform 15, temporal 21, terrain 4, tools 62
duplicate ids across family = 0
gdal:orthorectification occurrences = 1
  keys = {extends, family, id, intents, limitations, resource}
```

### E-11b · `data/help/diagnostics.json`

132 → 130 entries, 2 removed, 0 added, 0 ids lost; surviving objects
byte-identical to master's authentic first copies (see E-8a(2)). Re-parse:

```
entries=130  unique=130  dups=0  invalid_retry=0
families: dataset 21, env 12, geospatial 21, harness 40, operator 26,
          preflight 8, rs 2
```

### E-11c · What the executed gate reports afterwards

Three error classes down to one:

```
test_help_core.exe "[help][content]"   ->  exit 42
errors: 12 x unknown diagnostic family 'env'
        12 x diagnostic descriptor without DiagnosticInfo: diagnostic.env.<id>
```

The `retry 'retryable'` and `duplicate help id` errors are **gone**. The
residual is the `src/help/**` consumer defect — out of ownership, pinned by
O-11 and issued out (E-8c), **not** claimed fixed.

---

# Part 7 — Independent reference implementation

`tools/verification12/snapshot_diff_ref.py` implements the drift rules from
`src/contracts/snapshot_diff.h` independently of the C++ control flow, so that
agreement between the two is evidence rather than tautology.

Both snapshots self-diff to **zero differences** (reflexivity holds for both
schemas). Synthetic mutation results:

| mutation | expected | Reference output |
|---|---|---|
| drop a node | 1 removed | `- capability_entry benchmark:compare` ✓ |
| add a node | 1 added | `+ operator zz:new` ✓ |
| change node `origin` | 1 changed | `~ … origin: '…tools.json' -> 'moved.json'` ✓ |
| drop an edge | 1 removed | `- edge capability_for gdal:clip -> gdal:clip` ✓ |
| add an edge | 1 added | `+ edge k a -> b` ✓ |
| change edge `origin` | 1 changed | `~ edge … origin: '…io.json' -> 'moved.json'` ✓ |
| drop a census entry | 1 removed | `- entry cartography:compose` ✓ |
| add a census entry | 1 added | `+ entry zz:new` ✓ |
| graph vs census | schema mismatch | `!! SCHEMA MISMATCH` ✓ |
| unknown schema | error | `unrecognized snapshot schema: exp.unknown.v9` ✓ |
| missing schema | error | `recorded document carries no schema member` ✓ |

The **payload rule** is the part that matters: moving an element's `origin`
yields one `~changed` line, **not** an add+remove pair. That is what makes the
report readable instead of noisy.

One defect this exposed and fixed in both implementations: the readable id
printed redundantly as `capability_entry capability_entry/benchmark:compare`.
Now the id column carries the bare id, since `kind` has its own column.

---

# Part 8 — E-15 · The command surface was silently deleted from the graph

This is the resolution of E-13b, and it is the clearest instance yet of the
track's thesis. It is recorded here in full because the causal chain is
mechanically proven, not inferred.

## E-15a · The defect

`data/help/commands.json` — the **sole** source of `command.*` help topics and
of every `command_help` edge — does not parse. Consequence, read from the
assembler itself (`graph_assembly.cpp:151-158`): the file is skipped, the note
`unreadable help file` is recorded, and the entire command help surface
disappears from the contract graph.

Note that `command` nodes are **not** affected: they come from
`command_ref_scanner` over `src/app/workbench/command_defs.cpp` and
`src/app/main_window_workbench.cpp` (`graph_assembly.cpp:96-114`). Only the
*help* side is sourced from `commands.json`. So the defect is precisely a
**producer/consumer break across the graph's two halves**: 69 `command` nodes
remain, and their `command_help` edges have nowhere to land.

## E-15b · Evidence chain (every step executed)

**1. It is pre-existing on master, not mine.**

```
$ git diff --stat data/help/commands.json      # before my repair, after my 3-line fix
 data/help/commands.json | 3 +++
```
My only edit was 3 insertions (`]`, `},`, `{`). The parse failure and the
duplicate blocks are present at `HEAD`.

**2. The committed snapshot proves the file used to be 59 entries.**

```
$ python -c "… contract_graph.snap.json …"
help_topic origins: {… 'data/help/commands.json': 59, …}
```
The snapshot records **59** help topics from a file that today contains
**83** id lines. The snapshot therefore predates the corruption, and its byte
gate passes regardless — the gate compares snapshot bytes to snapshot bytes and
**never re-parses the source data it is supposed to be defending**.

**3. The causal chain, from git history.**

```
$ git log --oneline -5 -- data/help/commands.json
24ea7ab9e merge: land PR #1028 (linked-visual-analytics-11)
559087563 fix(help): append the 10 help entries master's registry was missing
                               (cartography.*/workbench.* pre-existing drift) …
```
and the parse state of each revision:

| revision | state |
|---|---|
| `24ea7ab9e^` | **69 entries, valid, 0 duplicates** |
| `24ea7ab9e` | **PARSE FAIL: Expecting ',' delimiter: line 986** |
| `559087563` | **76 entries, valid, 0 duplicates** |
| `HEAD` | **PARSE FAIL**, 83 id lines / 76 unique, 7 ids duplicated |

`24ea7ab9e` is the merge that broke it, adding **263 insertions** to a file it
should have merged cleanly. `559087563` is an *ancestor* of `HEAD`
(`git merge-base --is-ancestor` → true) and is the authored fix for exactly the
10 commands the snapshot shows as help-less.

**4. The damage is pure insertion — no authored content was ever lost.**

```
$ python difflib.SequenceMatcher(559087563, HEAD)
lines inserted: 158   lines deleted/replaced: 0
lines matched equal: 1152 of 1152
non-equal hunks: 5
```
Every line of `559087563` survives, in order and verbatim. The merge spliced in
**5 fragments**: one 119-line block that re-applies an already-present region
(`workbench.cartography`, `visualAnalytics`, `operatorCatalog`, the four
`cartography.*`), and four fragments that are the *bodies* of
`classifyStudio` / `georefDual` / `ir2Pipeline` — each **missing its `{` opener
and its `"id":` line**, which is what makes the array unparseable.

**5. The snapshot's 10 help-less commands are exactly the appended 10.**

```
command nodes: 69        commands with a help edge: 59
commands WITHOUT a help edge:
  cartography.compose, cartography.export, cartography.preflight,
  cartography.repair, workbench.cartography, workbench.classifyStudio,
  workbench.georefDual, workbench.ir2Pipeline,
  workbench.operatorCatalog, workbench.visualAnalytics
```
These are precisely the ids `559087563` was authored to add. The snapshot is a
faithful record of the 59-entry past.

## E-15c · The repair, and why it is provably the authored content

I restored the file to `559087563`. This is not a guess and not my own
authorship: it is the exact revision whose commit message is *"append the 10
help entries master's registry was missing"*, it is an ancestor of `HEAD`, and
it is a superset of the pre-merge 69 that is duplicate-free and valid.

```
$ git diff --numstat data/help/commands.json
0       155     data/help/commands.json        # pure deletion: 0 added, 155 removed
```

**Zero insertions.** I removed only the merge's improperly-spliced residue. No
authored line was invented, rewritten, or dropped.

## E-15d · Verification of the repair (parse level, independent of C++)

An independent Python pass over all 11 `data/help/**/*.json`, replaying the
assembler's help pass:

| metric | committed baseline | after repair |
|---|---|---|
| unreadable help files | **1** (`commands.json`) | **0** |
| `command_help` edges | 59 (in the *stale snapshot*) | **76** |
| `help_topic` nodes | 292 (in the *stale snapshot*) | **335** |
| registry commands with a help page | 59 / 69 (snapshot) | **69 / 69** |

## E-15d′ · Verification of the repair (the REAL assembler, executed)

The parse-level pass above is necessary but not sufficient. I then ran the
actual `contract_inventory` binary against both trees. This **corrects the
"59 → 76" framing above**: 59 was the *stale snapshot's* recorded number. The
computed live graph before the repair had **zero** `command_help` edges.

```
# BEFORE repair (commands.json as committed on master)
$ contract_inventory --source-root . --out .v12-evidence/live_graph.json
nodes: 1147 edges: 426 findings: 5
stderr: note: unreadable help file: ./data/help/commands.json

# AFTER repair
$ contract_inventory --source-root . --out .v12-evidence/live_graph_repaired.json
nodes: 1223 edges: 502 findings: 5
stderr: (no `unreadable help file` note)
```

Per-kind delta, which is the attributable part:

| kind | before | after | delta |
|---|---|---|---|
| `help_topic` (node) | 259 | 335 | **+76** |
| `command_help` (edge) | **0** | **76** | **+76** |
| `command` (node) | 76 | 76 | +0 |
| `capability_entry` / `diagnostic` / `error_code` / `operator` / `scientific_contract` / `preflight_action` | — | — | **+0 each** |
| every other edge kind | — | — | **+0 each** |

**`command_help: 0 → 76`.** The whole command documentation surface was absent
from the live graph and is now fully present. Every other element kind is
untouched, which is the proof that the repair is exactly scoped to the defect
and does not perturb anything else.

The 5 findings are unchanged before and after: they are the E-13 harness
dangling refs, which are out of ownership. **This repair neither masks them nor
adds any.** R1.2 therefore remains blocked, exactly as the ledger says.

## E-15d″ · Non-vacuity of the `command` node set

Note that `command` stayed at **76** across both runs. That is the corroboration
that `command` nodes come from `command_defs.cpp` and are *unaffected* by the
help-corpus failure — so before the repair the graph held 76 command nodes with
**0** of their help edges. That is the defect in one line.

`command`-node coverage is now **complete (69/69, no missing)**. The remaining
asymmetry is the reverse direction: 7 `view.link*` help pages have no registry
`command` node in the committed snapshot — because the snapshot predates the
`view.*` family (`a5da8899a feat(shell): mount linked-visual controllers and
view.* command family`). That is the stale-snapshot finding (E-2 / E-5b), not a
help-corpus defect, and it is why R1.2 must regenerate rather than hand-edit.

## E-15e · The gate that did not exist, and proof that it works

The reason E-15 went unnoticed is that **no test read the help corpus's raw
JSON**. `HelpContentStore` is deliberately tolerant, and the graph assembler
records a *note* rather than failing. So I added a case to
`tests/test_help_integrity_12.cpp` tagged `[e15]`:

> every shipped help file parses as a JSON **array** of uniquely-identified
> **objects**, with no missing ids; total entries ≥ 300, `command.*` entries ≥ 70.

Potency was proven **before compiling**, by replaying the gate's exact algorithm
in Python over two trees — the pre-repair tree (reconstructed from the backup I
saved at `.v12-evidence/commands.json.broken.bak`) and the repaired tree:

| tree | problems | `totalEntries` | `commandEntries` | verdict |
|---|---|---|---|---|
| **broken** (the E-15 defect) | 1 — `commands.json does not parse: Expecting ',' delimiter: line 1168 column 5` | 259 | **0** | **FAIL** ✓ |
| **repaired** (current) | 0 | 335 | 76 | **PASS** ✓ |

The `commandEntries = 0` on the broken tree is the defect stated numerically:
not "some commands lost their docs" but **the entire command help surface is
absent**. The gate fails on the defect and passes on the repair, and the
non-vacuity floors are what make the "absent" case loud.

## E-15f · Why this matters more than the specific file
Three independent defects on this track (E-5 `preprocess.json`, E-8a
`diagnostics.json`, E-15 `commands.json`) are the *same* failure mode:

> The platform faithfully records a producer/consumer mismatch into **an error
> list that nothing asserts is empty**, so the mismatch is observable and
> harmless at the same time.

And E-15 adds the sharper lesson: the byte gate was **the wrong shape of gate**.
A snapshot that is diffed only against itself cannot notice that the corpus it
describes has stopped parsing. It stays green through a total loss of the
command help surface. This is exactly what `test_snapshot_drift_12` (O-6) and
`src/contracts/snapshot_diff.{h,cpp}` exist to fix: make the live-vs-recorded
relation a first-class, readable, **asserted** condition rather than a byte
comparison.
