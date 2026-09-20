# Issue: two merge commits landed 23 unparseable data files on master

**Severity:** P0 (silent data loss: shipped corpora vanish from every consumer)
**Found by:** Track 02 `glm53-scientific-verification-12`, Oracle O-11 (E-16)
**Status on master:** **live defect**, reproducible with a single stock JSON parser
**Ownership:** three corpora outside Track 02's writable set
(`data/help/**`, `data/agent/**`, `data/processing/**`), hence issued out
rather than patched wholesale.

---

## Summary

25 of the 609 tracked `data/**/*.json` files on `origin/master` **do not parse**.
Two independent merge commits introduced them, and both are *evil merges*: the
merge result contains content present in **neither** parent.

| merge commit | PR | effect |
| --- | --- | --- |
| `43dcf19cd` | #1022 terrain-hydrology-11 | 19 files OK → FAIL |
| `4713528ef` | #1021 spectral-intelligence-11 | 4 files non-empty → 0 bytes |

A whole-corpus parse gate now exists to stop this recurring
(`tests/test_data_parse_integrity_12.cpp`), but it deliberately *tolerates* these
23 files: repairing them spans three corpora and two ownership boundaries
outside the verification track. This issue tracks the repair.

## Reproduce

```bash
git archive origin/master data/ | tar -x -C /tmp/m

find /tmp/m/data -name '*.json' -size 0          # the 4 zero-byte files

python - <<'PY'
import json, os
root = "/tmp/m/data"; bad = []
for dp, _, fs in os.walk(root):
    for f in fs:
        if not f.endswith(".json"):
            continue
        p = os.path.join(dp, f)
        try:
            json.loads(open(p, "rb").read().decode("utf-8"))
        except Exception as e:
            bad.append((os.path.relpath(p, root), str(e)))
for p, e in sorted(bad):
    print(f"{p}\n    {e}")
print(len(bad), "unparseable")
PY
```

Expected: `25 unparseable`.

## Family A — duplicated-key prefix block (19 files, merge `43dcf19cd`)

Every key line is emitted twice, once bare and once with a trailing space, so
each object is re-opened on top of itself:

```
   1|{
   2|  "capability" :
   3|  {
   4|    "applicability" :
   5|    {
   6|      "land_cover" :
   7|  "capability" :          <-- the object restarts, cloned from line 2
   8|  {
```

Attribution is exact — **neither parent is broken**:

| revision | `capability/rs-ace.json` |
| --- | --- |
| `43dcf19cd^1` (master side) | OK, 132 lines |
| `43dcf19cd^2` (PR #1022 side) | OK, 132 lines |
| **`43dcf19cd`** | **FAIL, 157 lines** |

A whitespace-only diff of the merge against *each* parent contains **only
insertions** (13 hunks per parent, 0 deleted), i.e. the conflict resolution
authored new content instead of choosing a side.

Files:

```
data/agent/capabilities/preprocess.json          (repaired by Track 02, see below)
data/processing/algorithm_meta/capability/rs-ace.json
data/processing/algorithm_meta/capability/rs-endmember-extraction.json
data/processing/algorithm_meta/capability/rs-gaofen-import.json
data/processing/algorithm_meta/capability/rs-hj-import.json
data/processing/algorithm_meta/capability/rs-library-select.json
data/processing/algorithm_meta/capability/rs-matched-filter.json
data/processing/algorithm_meta/capability/rs-mnf-inverse.json
data/processing/algorithm_meta/capability/rs-mnf.json
data/processing/algorithm_meta/capability/rs-sam-classify.json
data/processing/algorithm_meta/capability/rs-spectral-band-select.json
data/processing/algorithm_meta/capability/rs-spectral-unmixing.json
data/processing/algorithm_meta/capability/rs-temporal-extract-regions.json
data/processing/algorithm_meta/capability/rs-temporal-harmonic-breaks.json
data/processing/algorithm_meta/capability/rs-temporal-monitor.json
data/processing/algorithm_meta/capability/rs-temporal-phenology.json
data/processing/algorithm_meta/capability/rs-temporal-region-features.json
data/processing/algorithm_meta/capability/rs-temporal-regularize.json
data/processing/algorithm_meta/capability/rs-temporal-smooth.json
data/processing/algorithm_meta/capability/rs-terrain-flow.json
```

For each, the authored content is recoverable from the pre-merge revision by
removing the duplicated key lines — `git show 43dcf19cd^1:<path>` parses and
contains the same payload.

## Family B — truncated to zero bytes (4 files, merge `4713528ef`)

| file | `4713528ef^1` | `4713528ef^2` | merge |
| --- | --- | --- | --- |
| `algorithm_meta/rs-temporal-extract-regions.json` | 450 B | absent | **0 B** |
| `algorithm_meta/rs-temporal-harmonic-breaks.json` | 552 B | absent | **0 B** |
| `algorithm_meta/rs-temporal-region-features.json` | 550 B | absent | **0 B** |
| `algorithm_meta/rs-temporal-regularize.json` | 573 B | absent | **0 B** |

Recoverable via `git show 4713528ef^1:<path>`. The `capability/` siblings of all
four survive intact (5964–6609 bytes).

## Why nothing noticed — the actual bug worth fixing

Each affected corpus already had a test, and each test was **true and vacuous**
with respect to this defect:

1. `tests/test_capability_drift.cpp:63` asserts `loadProblems().empty()` — but
   points `CapabilityKnowledge` at `data/agent/capabilities`, a **different
   corpus** from `data/processing/algorithm_meta/capability`. Adding the wrong
   directory to a health check is indistinguishable from a passing one.
2. `AlgorithmMetaStore::loadFromDirectory`
   (`src/processing/framework/algorithm_meta_store.cpp`) reads
   `data/processing/algorithm_meta` — the directory that actually contains the
   failures — and does a bare `continue` on parse error. No record, no counter,
   no diagnostic. 23 sidecars disappear silently:

   | directory | total | parse OK | silently dropped |
   | --- | --- | --- | --- |
   | `data/processing/algorithm_meta/` | 51 | 47 | **4** |
   | `data/processing/algorithm_meta/capability/` | 139 | 120 | **19** |

   (`CapabilityCatalog::reload` *does* record into `mLoadProblems`, unlike
   `AlgorithmMetaStore`. The two loaders disagree about whether this failure is
   worth reporting.)
3. The contract snapshot compares recorded-vs-recorded, which cannot observe
   that the live corpus stopped parsing.

## Requested fix

1. Restore the 23 files from `43dcf19cd^1` / `4713528ef^1` respectively.
2. Make `AlgorithmMetaStore::loadFromDirectory` fail loudly (or at minimum
   expose a `loadProblems()`-style list) — a silent `continue` on a shipped
   corpus is the root cause of the months-long invisibility.
3. Correct `tests/test_capability_drift.cpp` to point at the corpus it intends
   to guard, or add a presence assertion so the wrong-directory case fails.

## Relationship to Track 02

Track 02 (`glm53-scientific-verification-12`) repaired **two** of these files
because their authored content was uniquely recoverable and the repair was
provably a pure restoration:

- `data/help/commands.json` — restored from `559087563`; `0` insertions /
  `155` deletions; `command_help` edges in the live contract graph went `0 → 76`.
- `data/agent/capabilities/preprocess.json` — same class, same method.

The remaining 23 are left to this issue by design. Track 02's contribution is
the **gate**: `tests/test_data_parse_integrity_12.cpp` walks every shipped
`data/**/*.json` and fails on any file outside a shrinking, explicitly-listed
set. It is currently green with 23 tolerated entries and a ceiling of 23, so
repairing any file forces the list to shrink.
