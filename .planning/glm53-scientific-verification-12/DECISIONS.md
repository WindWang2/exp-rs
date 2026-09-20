
## D-14 · Never commit on a detached HEAD in an independent worktree (E-17)

**Context.** The first commit on `agent/glm53-scientific-verification-12` was
attempted while the worktree sat on a detached HEAD whose target branch did not
exist. Git silently wrote an **orphan root commit** (`894ebbb4`,
`P=[]`, 13,150 files / 3,477,418 insertions) spanning the *entire* repository,
then failed to write the ref. The result was an unborn branch plus an index in
which every file was staged as an addition — an anomaly that survived repeated
`git reset` because a mixed reset targets an unborn HEAD.

**Options considered.**

| option | outcome |
|---|---|
| (a) `git reset --hard origin/master` | Discards all round work. Rejected. |
| (b) `git checkout -f origin/master` + redo work | Same loss; work is not re-derivable from the ledger alone. Rejected. |
| (c) `git update-ref` + direct loose-ref write + `git read-tree HEAD` | Repairs refs and index only. Working tree untouched. **Chosen.** |
| (d) Salvage the orphan commit's tree | Its tree is the whole repo at an arbitrary moment; it encodes no authored intent and its index was never the intended commit set. Rejected. |

**Decision.** Repair in place (c). The working tree was never at risk — the
defect was entirely in the ref/index layer — so there was no reason to accept
data loss. Verified by re-reading the pre-incident expected set (5 modified +
8 untracked) and by line-counting every deliverable.

**Why `git reset` misled us.** It reported `EXIT=0` and cleared the visible
staged list, yet `git status` kept showing the full tree staged. The exit code
described "the reset command ran", not "the index now matches HEAD". A mixed
reset against an unborn HEAD is a no-op on the index. Reading exit codes as
evidence of *state* — rather than of *invocation* — is precisely the
false-success class this track exists to eliminate; it is fitting that the
track's own tooling tripped it.

**Corollary rule (mandatory in this track and any agent worktree).**
1. Before the first commit: `git symbolic-ref -q HEAD` must succeed and the
   branch must already exist. Otherwise `git checkout -b <branch> <base>` first.
2. After every commit: confirm reachability with `git log --oneline -1`. Do not
   trust `git commit`'s exit status alone.
3. When a working-tree anomaly appears, diagnose at the **object-store layer**
   (`cat-file`, `for-each-ref`, `show-ref`, `read-tree`) before touching the
   working tree. Never reach for `--hard`, `-f`, or `clean` as a first move.
rnative — a red gate on `master` — was rejected because this track cannot
fix the files, and a permanently-red gate is one that gets muted.

**Rule carried forward.** From D-12: *repair when the authored content is
uniquely recoverable from history and the repair is provably a restoration; file
an issue when it is not.* D-13 adds the corollary: **when you find a class rather
than an instance, ship the gate that makes the class visible — do not repair the
instances you happen to own and call the class closed.**
cancel flag (the flag is read before the expensive work). It does **not**
  prove that a flag set *mid-warp* is observed, because the test only exercises
  the pre-set path. Whether `IoWarpOperator` polls during the GDAL warp call
  remains **unverified**.

**Action.** Add a 12.0 lane case that sets the cancel flag *after* the operator
has begun (via the `CancelFn` callback, from the progress callback) and asserts
a typed `Cancelled` refusal with zero partial artifacts. That is the honest
increment: it either passes, closing the gap with evidence, or fails, producing
a genuine reproducible defect that then justifies the minimal fix. **It must
not be assumed either way.**

**Oracle served.** O-4 (failure/cancel lane) — *extended*, not repaired.

---

## D-2 · F5 read-only-directory probe

**Question.** F5 asserts a typed write refusal when the output directory is
read-only. On this host the probe warns `write SUCCEEDED (host does not enforce
directory ACL for owner)`.

**Evidence.** Windows does not deny the owner write access to a directory just
because the read-only *attribute* is set; the attribute is meaningless for
directories on NTFS. The fixture therefore cannot produce its precondition.

**Decision.** Split the assertion, do not weaken it.
1. The **portability defect** is the fixture's. Replace the
   "read-only directory" construction with a hermetic seam that is
   portable by construction: point the output at a **path whose parent is a
   regular file** (e.g. `out.tif` under a file named `blocker`) and/or at a
   **non-existent parent directory inside a path that cannot be created**.
   These produce a deterministic, OS-independent refusal on every platform.
2. The **error-taxonomy question** is separate and real: the operator returned
   `InvalidParameter` for an unwritable destination, but the taxonomy has
   dedicated I/O codes in the 2000–2999 band. Asserting the *taxonomy-correct*
   code (with a documented, evidence-backed accepted set) is tightening, not
   loosening.

**Explicitly rejected.** Widening the accepted set to include
`InvalidParameter`. That would manufacture green over a genuine taxonomy
smell.

**Oracle served.** O-4.

---

## D-3 · Fix the corrupt `preprocess.json`, and add the missing gate

**Question.** `data/agent/capabilities/preprocess.json` is unparseable on
master since `eea3aee65` / PR #1024 (E-5). How to handle?

**Evidence.** Independent parse failure at line 362; the malformed object is
`gdal:orthorectification`, whose trailing `intents`/`resource`/`limitations`
triplet is duplicated after a missing comma. The loader
(`capability_knowledge.cpp:163`) drops the *entire file* on parse failure.
`CapabilityKnowledge::loadProblems()` has no consumers.

**Decision.**
1. **Repair the data** as a conscious, minimal diff: delete the duplicated
   triplet and close the object. Intent reconstruction is unambiguous — the
   first (well-formed) copy of the triplet is the authored content; the second
   is a bad merge/paste artifact. Preserve the authored values exactly.
2. **Add the missing gate.** A new test in `tests/verification12/` must assert
   `CapabilityKnowledge::instance().loadProblems()` is empty after loading the
   repository's real `data/agent/capabilities`, and that the loaded entry count
   matches the number of authored entries. Without this, the same class of
   corruption is invisible again tomorrow.
3. **Do not** fix it by making the loader lenient (e.g. skipping only the bad
   element). Silent partial loads are the defect, not the fix.

**Oracle served.** O-2, O-5, O-9 (new: no load problems).

---

## D-4 · New Oracle O-9: registry load problems must be empty

**Question.** Why is this in scope, given "not a replacement for per-feature
business tests"?

**Evidence.** The platform already *records* load problems in five registries
(capability knowledge, capability catalog, capability relations, and the four
cartography registries). Only cartography surfaces its problems — through
`cartography:preflight`. The capability side records them into a variable that
nothing reads.

**Decision.** In scope. This is not a business test; it is a **platform
integrity gate**, exactly the kind of thing the 12.0 mandate calls for. It is
cheap (one test), it is source-grounded (reads the real data directory), and it
is falsifiable (it fails today, on master, which is the proof).

**Oracle served.** O-9.

---

## D-5 · Snapshot regeneration policy (E-2 census, E-5b graph)

**Question.** Both snapshots are stale. Regenerate blindly?

**Evidence.** Committed census has 166 entries, fresh has 190. Committed graph
has 161 operator nodes and misses 20 contract-bearing ids. Both drifted because
24 operator ids were registered after the last regeneration
(`d2fcfff38`, `407ffacd2`) and 14 fail-closed PRs landed without re-running
these gates.

**Decision.** Regenerate, but as a **reviewed conscious diff**, never
mechanically:
1. Regenerate with the documented tool
   (`contract_inventory --source-root . --census-out …` and the graph
   equivalent) in the worktree.
2. **Inspect the diff line by line.** Every added id must be attributable to a
   commit that registered it. Any id that cannot be attributed is **unknown
   drift** and must fail loudly rather than be blessed — per the brief's
   non-goal.
3. **Never widen** `expectedMissing` in
   `test_contract_cross_surface_11.cpp`, and never bump the `>= 109` /
   `>= 132` / `>= 100` floors to make an assertion pass. Floors may only move
   *upward* when new surface is genuinely added by this track.
4. Record the attribution table in the PR body.

**Oracle served.** O-2, O-6.

---

## D-6 · New Oracle O-10: environment preconditions are asserted, not assumed

**Question.** E-0 cost a full diagnostic cycle and nearly produced a wrong
report to the user. How is that prevented structurally?

**Evidence.** Running the inherited suites without `PROJ_DATA` yields
`InvalidParameter: input raster carries no CRS` / `warpRaster: option
construction failed` — i.e. **product-defect-shaped** failures caused by test
provisioning. A Unix-style `PROJ_DATA` fails identically, which is worse.

**Decision.** Add a precondition gate the whole verification platform runs
first:
- assert GDAL/PROJ data resolves (`proj.db` reachable through the configured
  path), and
- emit a single, unambiguous message naming the missing variable and the
  required Windows-style value, then fail fast.

The gate must distinguish "environment not provisioned" from "product is
wrong". This is deliberately *not* a skip: a skipped precondition is a false
green. It is a hard failure with a legible cause.

**Oracle served.** O-10, and it protects O-2/O-3/O-4 from false red.

---

## D-7 · Detached HEAD now, branch name at push time

**Question.** The brief mandates branch `agent/glm53-scientific-verification-12`
in worktree `../exp-rs-worktrees/<track-id>`.

**Evidence.** `.git/refs/heads/agent/` was being **actively deleted** by
concurrent agent sessions on the shared clone (observed: refs vanishing within
seconds, new `ds41-*` refs appearing, orphaned reflogs). `git branch <name>`
returned exit 0 while producing no resolvable ref. Creating the branch eagerly
was itself a source of corruption risk.

**Decision.** Work at a **detached HEAD on `adf8f9895`** (clean tree verified,
`git status --short` empty), and create the branch ref only at push time with
an atomic `git push origin HEAD:refs/heads/agent/glm53-scientific-verification-12`.
Rationale: the deliverable is a **PR from a commit range**, and the commit
ids are the real identity — the branch name is just a pointer that any
concurrent session can clobber. This satisfies the brief's intent (independent
worktree, independent branch, independent PR) while removing a live corruption
vector. If the name collides at push time, the brief's auto-rename rule
applies.

**Oracle served.** Process integrity; no Oracle is graded on branch names.

---

## D-8 · Ledger location

**Question.** `.goal-loop-ledger.md` already exists in the repository root and
is 25 KB of *other* tracks' history.

**Evidence.** The file is committed and shared; concurrent sibling tracks are
writing to it.

**Decision.** Keep this track's ledger **worktree-local** at
`GOAL-LOOP-LEDGER-12.md`, as the brief permits ("keep `.goal-loop-ledger.md`
worktree-local by default unless repo convention requires committing it"). A
shared mutable ledger across concurrent agents is a merge-conflict generator
and would let another track's entries be mistaken for mine. **Do not** commit
`GOAL-LOOP-LEDGER-12.md`.

---

## D-9 · What is NOT being done, and why

- **No full-repo byte-identical cross-OS determinism test.** Explicit non-goal.
- **No rebuilding QGIS/OTB/ITK.** Resource rule; use the warm `build-dev`
  artifacts for baseline measurement and the worktree `build-v12` only for
  targets this track changes.
- **No cherry-picking of stale `agent/*` / `fix/*` branches.** Census in
  DEDUP.md classes them as superseded or different-track.
- **No relaxing of any assertion, floor, tolerance, precision or accepted-code
  set** to reach green. Where an assertion was wrong (D-2) it is made
  *portable and taxonomy-correct*, not weaker.
- **No enabling of online services or online CI** as a completion condition.

---

## D-10 · The `env` diagnostic family is a real producer↔consumer break (E-9)

**Question.** `DiagnosticFamily` has no `Env` enumerator and
`diagnosticFamilyFromName("env")` returns `nullopt`, yet the repository authors
12 entries with `"family": "env"` in `data/help/diagnostics.json`. Is this a
defect, and is it in scope?

**Evidence (three-way, all on master).**
1. **Producer, declared.** `src/geospatial/doctor/env_doctor.h:20-23` states the
   contract explicitly: findings map to curated prose ids in
   `data/help/diagnostics.json` **(family "env")**; "the id is carried verbatim
   in the `diagnostic` field **and must exist there**".
2. **Producer, emitted.** `env_doctor.cpp` emits 10 distinct
   `diagnostic.env.*` ids (`data_dir_missing`, `data_dir_unresolved`,
   `gdal_data_missing`, `gdal_driver_missing`, `gdal_drivers_empty`,
   `proj_db_missing`, `proj_db_unusable`, `temp_not_writable`,
   `temp_unresolved`, `unicode_path_failed`).
3. **Authored.** `data/help/diagnostics.json` contains 12 `family: "env"`
   entries — so the ids *do* exist, satisfying the header's requirement.
4. **Consumer, broken.** `src/help/help_id.h:51-59` declares
   `DiagnosticFamily{Harness,Operator,GeoSpatial,Dataset,Preflight,Rs}` — no
   `Env`. `help_id.cpp:49-64` `diagnosticFamilyFromName` has no `"env"` branch,
   and `help_id.cpp:30-47` `diagnosticFamilyName` has no `Env` case (its
   fall-through returns `"rs"`, so an unknown family is silently mislabelled as
   remote-sensing).

**Consequence.** The 12 authored env pages are unreachable by family lookup.
`test_help_core` fails today on master (`exit 42`) reporting
`unknown diagnostic family 'env'` for exactly these entries. This is the
**same failure shape as E-5/E-8a/E-8b/E-8c**: a producer's contract recorded in
a place no one asserts is exhaustive.

**Decision.**
1. **Do not fix it in this track.** `src/help/**` is outside the 12.0 ownership
   list. The correct disposition per the brief is: establish a **minimal
   reproducible defect**, record evidence, and **open an issue**.
2. **Do assert it.** `tests/test_help_integrity_12.cpp` (Oracle O-11) asserts
   the *current, documented* state exactly: the set of unknown families is
   `{"env"}`. This pins the known gap so it cannot silently grow, and it will
   fail loudly the moment either the gap is closed (gate must be updated
   deliberately) or a *new* family is authored without a consumer. A
   pinned-known-gap assertion is **not** a loosened assertion — it is a change
   detector with the current value written down.
3. **The minimal repro** to attach to the issue is one command:
   `test_help_core.exe "[help][content]"` (exit 42, message names
   `unknown diagnostic family 'env'`), supported by the four-way evidence above.

**Oracle served.** O-11; contributes to O-3 (uncovered set = 0 with reasoned
exemption — here the exemption is "authored but unconsumed, pinned and issued").

---

## D-11 · ERROR: I reported a non-existent harness-code drift (E-12)

**Question.** Does the contract graph contain harness error codes absent from
`harness_error.h`?

**What I did wrong.** I extracted the declared vocabulary with
`sed -n '27,66p' src/agent/harness/harness_error.h`, which is an **arbitrary
line window**, not a parse. That window truncated the namespace and omitted 5
codes: `CATEGORICAL_MISMATCH`, `FACT_CONFLICT`, `NONDETERMINISTIC_CHAIN`,
`OUTPUT_PATH_COLLISION`, `RESOURCE_OVER_BUDGET`. Comparing the graph (36
`harness/*` `error_code` nodes) against that truncated set produced a
plausible-looking "5 codes missing from the taxonomy" defect. It was an
artifact of my method.

**Corrected evidence.** All uppercase string literals in the file:
40 total; all 5 codes **are** declared. Every one of the 36 graph
`harness/*` codes resolves to a declared literal, and all 31 declared codes
appear as nodes. **There is no harness-code drift.** The graph is also
internally sound: 673 nodes, 448 edges, **0 dangling edges**.

**The one real signal, honestly scoped.** 5 declared codes
(`CATEGORICAL_MISMATCH`, `FACT_CONFLICT`, `NONDETERMINISTIC_CHAIN`,
`OUTPUT_PATH_COLLISION`, `RESOURCE_OVER_BUDGET`) have **no
`diagnostic.harness.*` help page**. That is a documentation *coverage gap*, not
a correctness defect: the codes are usable, typed, and in the taxonomy; they
simply have no curated prose. Not in scope for repair here.

**Decision.** Record the error, retract the drift claim, and keep the positive
finding (0 dangling edges; graph internally consistent). The drift detector I
intend to add must **parse the identifier list**
(`inline constexpr const char *k[A-Za-z0-9_]+ = "…"`), never slice by line
number. This is exactly the "one real gap, one attributable change, and do not
judge by appearances" discipline — and it is the second time on this track that
an over-eager slice produced a defect that did not exist (see D-0/E-0).

**Oracle served.** None directly; this protects O-2/O-3 from a fabricated
finding and is recorded as a process correction.

---

## D-12 · Repair `commands.json` by restoring the authored revision, not by re-authoring (E-15)

**Question.** `data/help/commands.json` does not parse, so the graph's entire
command-help surface vanishes (E-15). Options were: (a) hand-repair the broken
brace structure to something I judged correct, (b) delete the duplicated
blocks only, leaving the missing `"id"` lines to be invented, (c) restore the
file to the authored revision that the history says it should be, (d) leave it
broken and file an issue.

**Decision: (c).**

**Reasoning.**

1. (a) and (b) would make me the author of command ids and prose. Two fragments
   are *bodies without ids* (`classifyStudio`, `georefDual`/`georefI2M`,
   `ir2Pipeline`); inventing an id would be guessing at authored content. That
   is exactly the "manufacture green" failure mode the brief forbids.
2. (d) is untenable because this is squarely in my ownership
   (`data/contracts/**`, `data/help/**` help-corpus integrity) and it is the
   single largest contributor to the snapshot drift I am chartered to make
   legible.
3. Evidence uniquely identifies the target content. `559087563` is an *ancestor
   of `HEAD`*, its message is literally *"append the 10 help entries master's
   registry was missing"*, it parses with 76 entries and 0 duplicates, and it
   contains all 10 ids the committed snapshot records as help-less. The
   pre-merge state (`24ea7ab9e^`) had 69 valid entries, so `559087563` is a
   strict superset of a known-good state.
4. `SequenceMatcher` proves the divergence is **pure insertion**: 158 lines
   added, **0 deleted**, `1152 of 1152` original lines matched in order. So
   restoring is a **pure deletion** — `git diff --numstat` reports
   `0 155`. Zero authored lines are introduced.

**Consequence.** The repair is mechanically attributable: every surviving byte
comes from `559087563`; the only change is removal of 5 merge residue fragments.
Verified **by executing the real assembler**: live `command_help` edges
**0 → 76**, `help_topic` nodes 259 → 335, unreadable help files 1 → 0, and
**every other node/edge kind unchanged (+0)**. Registry command coverage
76/76 in the live graph. The 5 remaining findings are E-13 and are unchanged.
(An earlier draft of this line quoted "59 → 76", conflating the *stale
snapshot's* recorded 59 with the live graph's 0. Corrected 2026-09-20.)

**What this does NOT do.** It does not touch `graph_assembly.cpp`, does not
regenerate any snapshot, and does not address E-13 (the harness-vocabulary
split, still blocked on the `src/agent/harness/**` issue). R1.2 remains blocked:
regeneration must wait until the findings gate is meaningful.

**Oracle served.** O-2 (snapshot matches live registry), O-11 (help corpus loads
with zero errors), and it is the precondition that makes O-6's drift report
interpretable — 59 of the 124 removals in the drift report had this single root
cause.

**Process note.** This is the third committed-data corruption on this track,
and the third time the "report, do not repair" rule needed a judgement call. The
rule I am applying: *repair when the authored content is uniquely recoverable
from history; file an issue when it is not.* `preprocess.json` (E-5) and
`diagnostics.json` (E-8a) met that bar; the `env` family (D-10) and the harness
vocabulary split (E-13) did not.
