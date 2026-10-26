# R2 Independent Review — agent/r22-agent-20261025T164309Z

- **Reviewer**: R2 (second, independent). R1's report and conclusions were NOT read and are
  not relied on anywhere below; every finding was re-derived from the diff, the surrounding
  code, and (where noted) standalone experiments against this exact tree.
- **Scope**: `git diff origin/master..HEAD -- src/ tests/` (9 commits: #1704 #1690 #1691
  #1692 #1705 #1693 + R1 follow-up round). Baseline `origin/master` = 911e0b17a3.
- **Method**: counterexample construction per attack surface; two standalone reproductions
  were built and run against the tree's own headers (`workspace_containment.h` recursion;
  `istream::getline` boundary matrix on the lane's GCC 16 / libstdc++). Full-tree test suite
  NOT re-run (configure cost); behavior verified by inspection + the standalone runs.
- **Known-environment facts** (vendor link-shim, exprs_plugin_loader KNOWN_BASELINE_FAIL,
  SICNU_EMBED_PYTHON gating, mcp_server ±1 conditional assertion) are excluded as instructed.

---

## Findings

### R2-F1 [P1, real bug] Network-driver table uses a wrong GDAL short name — `OCI:` bypasses the remote default-deny the fix exists to enforce

`src/agent/tool_catalog/workspace_containment.h:224-237` (table), `:401-452` (dispatch).

Counterexample chain (no R1 input; traced from the code):

1. GDAL's Oracle Spatial driver short name is **`OCI`**, and its connection form is
   `OCI:username/password@db` (gdal.org/en/stable/drivers/vector/oci.html — verified
   independently during this review; same for raster `GeoRaster:usr/pwd@db`).
2. The network table contains `"oracle"` — a short name no GDAL build registers — so
   `isNetworkGdalDriver("oci")` is **false** and the string falls to the unknown-driver
   branch.
3. `unknownDriverPayloadIsDangerous("usr/pwd@db")` (`:283-314`): first char `u`; no
   `scheme://`; no embedded drive; kvKeys (`host=`, `server=`, `url=`, `dbname=`,
   `database=`, `user=`, `service=`, `port=`) do not match `usr/pwd@db` (no `=` at all);
   no `..` segment → **benign**.
4. Verdict `LocalPath` → joined onto the workspace root → canonicalizes inside →
   `pathOutsideWorkspace` = **false** → the MCP gate passes the argument
   (`src/agent/mcp_server.cpp:209` scans every non-free-text string argument).
5. The spatial tool then hands the RAW string to GDAL, which opens a **network database
   connection** — exactly the `PG:host=…` scenario #1704 was filed to block, one
   table-entry away. `SICNU_MCP_ALLOW_REMOTE` is never consulted.

Same class, same chain: `EEDA:collection=…` (Earth Engine Data API — network driver,
`collection=` not in kvKeys), `AmigoCloud:project_id=…`, `GeoRaster:usr/pwd@db`.
Exploitability is conditional on the runtime GDAL linking these drivers, but the POLICY
invariant the commit claims ("network drivers follow the remote default-deny") is broken
by construction, and the fix's threat model is precisely a hostile client probing prefixes.

**Fix**: replace `"oracle"` with `"oci"` (keep both if paranoid), add `"georaster"`,
`"eeda"`, `"amigocloud"`; make the table test-driven (assert every network-class short
name against `GDALGetDriverList`-derived expectations, or at least a pinned list of the
documented `X:`-prefixed drivers).

### R2-F2 [P2, real bug, empirically reproduced] Unbounded `classifyReference` recursion — stack-overflow SIGSEGV from a 400 KB tool argument

`src/agent/tool_catalog/workspace_containment.h:407-413` (introduced by the R1 follow-up
commit f8f6c63e97).

The recursion requires every level's prefix to be a *file-backed* driver, so
`"KML:" × N + "x"` (KML is 3 letters + colon = 4 bytes per level) drives depth N with no
guard, and each level also `toLower()`s the entire remaining suffix (O(n²) copying).

Reproduction against THIS tree's header (Qt6/qmake6, default 8 MiB stack —
`ulimit -s 8192`):

```
input=80001 bytes,  depth=20000  → survived (kind=DriverPath)
input=400001 bytes, depth=100000 → exit 139 (SIGSEGV)
```

400,001 bytes is 10× under the MCP 4 MiB line cap (`src/agent/mcp_server.cpp:419`
`kMaxMcpLine = 4 * 1024 * 1024`), and the crash input flows through
`collectOutsideWorkspace` (`src/agent/mcp_server.cpp:209`) for ANY string tool argument
(it classifies the RAW value before any workspace join), and through
`src/cli/rs_pipeline_runner.cpp:152`. Same crash available to `GPKG:`/`CSV:` chains.

Impact: the sandbox server process dies (fail-closed — no escape), i.e. a client can
hard-crash the MCP server / pipeline runner; also quadratic CPU for sub-crash inputs.
Not a containment breach, so P2 rather than P1 — but it is newly introduced by the R1
fix round.

**Fix**: strip prefixes iteratively in a bounded loop (max ~4 levels covers every real
nested form), or thread a depth parameter and deny beyond e.g. 8. While there, hoist the
`toLower()` so it is not redone per level.

### R2-F3 [P3, real bug — compound edge] #1705 lets cumulative records and absolute-index deltas mix index spaces in TaskCenter

`src/processing/framework/task_center.cpp:2017-2043`, producer
`src/jobs/job_engine.cpp:1006-1031` (progress callback `copy = it->second;` ships a
CUMULATIVE record with `logLinesOffset == 0` and the *retained* window) and
`src/processing/framework/task_center.cpp:3099` (submit-time catch-up snapshot).

Before #1705, `logLines.size()` of a cumulative record equaled the absolute appended
count, so the cumulative branch's `m_forwardedLogCounts[taskId] = record.logLines.size()`
(`:2043`) wrote the same number the delta branch compares absolute indices against
(`:2027`). After #1705, `size()` is only the **retained** window (`size = absTotal −
droppedLogLines`), so `:2043` writes a retained count into an absolute-indexed slot.
Confirmed TaskCenter is the ONLY consumer (`logLinesOffset` grep: task_center.cpp:2017
alone; `snapshot()`'s other consumer, the TraceEndGuard at job_engine.cpp:904, reads only
state/error).

Concrete loss/dup interleaving (requires an operator that logs from ≥ 2 threads — a
pattern #1693's own comment acknowledges — plus eviction plus notify reordering):
dropped D=5001, absTotal=12002, retained R=7001, TaskCenter forwarded F=5000 with deltas
abs 5000..12001 already emitted but delivered late:

1. stale progress-cumulative delivered first: `R(7001) > F(5000)` → forwards retained
   [5000..7001) = absolute 10001..12001; `forwarded := 7001`;
2. the queued deltas then arrive: absolute 5000..7000 are `< seen` → **skipped** (LOST);
   absolute 7001..12001 are `≥ seen` → forwarded again (**DUPLICATED**).

Pre-#1705 this could neither lose nor duplicate (cumulative size == absolute total, so
step 1 forwarded exactly the missing absolute tail and step 2 deduped the rest). The
single-thread operator case stays safe (deltas and progress copies are produced and
delivered in order), and the submit-time catch-up variant needs >10k lines printed inside
the submit→snapshot window (practically unreachable), hence P3 — but it IS a new
correctness window introduced by this change.

**Fix**: translate cumulative records into the absolute space on the consumer side —
compare and store `droppedLogLines + logLines.size()` in `m_forwardedLogCounts` and slice
the cumulative window from `max(forwarded, dropped) - dropped` onward.

### R2-F4 [P3, real bug — fail-closed, usability] File-backed table entries that cannot ever match

`src/agent/tool_catalog/workspace_containment.h:203-217`:

- `"esri shapefile"`, `"mapinfo file"` contain spaces; `gdalDriverPrefixOf` (`:320-338`)
  rejects any prefix containing a space, so these entries are unreachable dead data
  (harmless — GDAL has no such prefixed form either, but the table lies to the reader).
- `"tiledbspatial"` — the actual raster AND vector driver short name is **`TileDB`**
  (gdal.org/en/stable/drivers/raster/tiledb.html, .../vector/tiledb.html — verified during
  this review). `TileDB:` therefore lands in the unknown branch: `TileDB:/abs` and
  `TileDB:s3://…` are denied (fail-closed, fine), but an **in-workspace absolute**
  reference `TileDB:/ws/root/arr` is now DENIED (`/`-payload ⇒ dangerous) where the same
  path bare is allowed — a false-deny regression class for every missing file-backed
  driver (`GML:`, `DGN:`, `SVG:`, `TileDB:`).

**Fix**: rename to `"tiledb"`; either drop the space-containing entries or make
`gdalDriverPrefixOf` admit them (do not admit — spaces would misparse real paths);
optionally add `gml`, `dgn`, `svg`.

### R2-F5 [P3, hardening] `unknownDriverPayloadIsDangerous` kvKeys miss `=`-parameter forms that carry paths

`src/agent/tool_catalog/workspace_containment.h:305-311`. An unknown driver whose
connection string carries a filesystem path in a parameter not on the list — e.g. the
GDAL 3.11 ADBC driver's `ADBC:driver=adbc_driver_sqlite,path=/abs/x.db` (`path=`,
`driver=` absent from kvKeys; separator is `,` so the `' ' + key` clause never fires) —
is classified benign-relative and passes, and the runtime driver opens `/abs/x.db`.
Conditional on such a driver being compiled in; ADBC is the concrete one on the horizon.

**Fix (opinion)**: for UNKNOWN drivers, deny any payload containing `=` at all — no
legitimate relative workspace filename needs `key=value` connection syntax.

### R2-F6 [P3, minor] Engine-internal appends after eviction ship cumulative notifies that can no longer deliver their line

`src/jobs/job_engine.cpp:154` ("Cancelled due to shutdown") and `:341` ("Cancelled while
queued") `appendLog` + `notify(rec)` with `logLinesOffset == 0`. For the shutdown case on
a chatty RUNNING job, retained size < absolute forwarded count ⇒ TaskCenter's cumulative
guard (`size > forwarded`) is false ⇒ the terminal "Cancelled due to shutdown" line is
never displayed. One line, extreme edge; queued jobs never have 10k lines.

### R2-F7 [P3, platform caveat] StdinReader dead-branch rationale verified on libstdc++, unverified on MSVC

The #1693 comment's claims were **empirically confirmed** on this lane (GCC 16,
libstdc++): a line of exactly `kMaxMcpLine` chars + `'\n'` is delivered by the non-fail
path (getline consumes the delimiter without failbit), and exactly-`kMaxMcpLine`+EOF sets
eofbit only and is delivered by the eof branch; fail()+gcount==kMax therefore always means
genuinely overlong. Caveat: MSVC's `getline` sets failbit whenever the buffer limit is
reached without a delimiter (per its documented contract), so a *final* frame of exactly
4 MiB with no trailing newline would draw a spurious `__MCP_LINE_TOO_LONG__` sentinel
there. 4 MiB-exact frame + no newline + Windows is vanishingly rare — note only.

### R2-F8 [P3, test quality] Minor test/registration nits

- `tests/test_r22_agent_env_keys.cpp:108-109` — `INFO("key: …")` placed AFTER the
  `REQUIRE_FALSE` it was meant to explain: on failure the context is not attached
  (Catch2 INFO only annotates subsequent assertions). Move it before.
- `tests/CMakeLists.txt` R1-F6 comment claims BOTH ghost/ipc binaries "spawn the REAL
  worker daemon" — `test_r22_agent_ipc` does not (it drives a fake `QLocalSocket` peer;
  no Python involved). Harmless doc drift, but it misleads lane triage.
- The env-key control test (`/bin/sh`, `printf`) is POSIX-only; the *refusal* tests
  still pass on Windows because E5007 fires before any spawn (verified order at
  `external_process.cpp:637-647`), so only the control would red there.
- No table-driven test pins network-class driver short names — that is exactly the gap
  R2-F1 slipped through (a test listing OCI/EEDA/GeoRaster would have caught it). The
  recursion bound (R2-F2) is likewise untested (a depth-cap fix should add one).

---

## Independently verified safe (attacks that failed)

1. **IPC salvage regex vs daemon serialization** (`python_ipc_server.cpp:177-225`): all
   26 `sendall` sites in `worker_daemon.py` inspected. Every RESPONSE dict is
   `jsonrpc, id, …` (incl. error paths at :410/:443…:798/:814 and the #1692 replacement at
   :795-809); every daemon→host REQUEST is `jsonrpc, method, params, id` — so the
   response-shape anchor really does prevent a giant request from retiring an innocent
   pending id (the in-code claim holds). Python `json.dumps` default separators
   (`", "`, `": "`) are covered by the regex's `\s*` — and the new test uses compact
   JSON, exercising the other spacing. `id: null`/string ids simply don't match → old
   silent-drop, which is correct (no callback exists for them).
2. **256-byte prefix truncation collision**: mid-digit truncation requires an id with
   ≥ ~230 digits (prefix `{"jsonrpc": "2.0", "id": ` ≈ 25 bytes). Host ids are
   sequential `int`s from 1 (`python_ipc_server.h:145`) and daemon request ids are the
   fixed literals 8001-8005/9999 (request-shaped, unmatched). A wrong-but-live id is not
   constructible; if ids ever become UUID strings the salvage degrades to the old
   silent-drop, never to a wrong failure.
3. **1 MiB boundary consistency**: daemon replaces when `len(payload) > 1048576`
   (payload incl. `\n`) ⇔ host salvages when `newlineIdx > 1048576` (chars excl. `\n`) —
   there is no length window the new daemon lets through that the host drops.
4. **StdinReader matrix** (see R2-F7): exact+\n, exact+EOF, overlong+\n, overlong+EOF,
   empty, badbit — no infinite loop, no lost frame on libstdc++.
5. **envp splice closure**: both run() sites reject empty / `'='` / NUL keys BEFORE the
   deny-list; POSIX `childEnvironment` (:965) and Windows `environmentBlock` (:478)
   assemblers can therefore no longer splice. Manifest gate `[A-Za-z_][A-Za-z0-9_]*` is
   strictly tighter; existing fixtures compliant (`FULL_MODE` in test_plugin_manifest.cpp:179;
   example manifests carry only `inherit_environment`). Residual asymmetry — the run
   gate still admits space/control-char/non-ASCII keys for DIRECT SDK callers — cannot
   splice (no `=`/NUL) and is hardening-only (opinion).
6. **Bridge/adapter lifetime (#1691)**: `~AppInterfaceBridge` (:288-302) revoke is
   IPC-free, exception-swallowed, idempotent; the bridge is uniquely owned
   (`std::make_unique`, no QObject parent → no double delete). Failure path
   (`python_plugin_adapter.cpp:174-185`) revoke+reset double-revokes as a no-op; unload()
   (:265-271) symmetric; registry executors capture `QPointer` and fail with
   "IPC Server not available" rather than UAF once revoked. Residual TOCTOU between the
   `!weakBridge` check and `ipc->…` use (`app_interface_bridge.cpp:402-406`) under a
   concurrent unload is pre-existing and unchanged.
7. **#1705 delta algebra**: cursor/droppedLogLines/localStart math is exactly-once under
   10'000→5'000 block eviction (single-thread case), pinned by test 2's
   contiguity + exact `droppedLogLines == 5001` assertions (deterministic at 12'002
   appends). Terminal fields-only notify (`offset == 0`, empty lines) is a harmless no-op
   for TaskCenter. UTF-8-aware 4 KiB line truncation backs up correctly over continuation
   bytes.
8. **ALLOW_REMOTE parity after recursion**: `GeoJSON:http://x` and `CSV:/vsicurl/http://x`
   with the flag set resolve to `RemoteAllowed` exactly like the bare reference;
   `Zarr:s3://…`, `CSV:GPKG:/abs`, quoted `GPKG:"GPKG:…"` all terminate fail-closed;
   `GPKG::memory:`, `CSV:` (empty payload) and unclosed quotes are Denied.
9. **#1693 late-log guard**: terminal check happens under `m_mutex` before `appendLog`,
   so it cannot race the finish path's own append; it also prevents the cursor
   re-insertion leak. Legitimate post-completion logging is intentionally dropped.

---

## Verdict

**REJECT** — the direction of all six fixes is sound and most attack surfaces held up
under independent counterexample construction, but the containment change ships two
actionable defects of its own:

1. **R2-F1 (P1)**: the network-driver allow/deny table keys off driver short names that
   do not exist in GDAL; `OCI:` (verified short name) walks through the benign-unknown
   fallback and nullifies the `SICNU_MCP_ALLOW_REMOTE` default-deny that is the entire
   point of #1704.
2. **R2-F2 (P2, reproduced)**: the R1-round nested-driver recursion has no depth bound;
   a 400 KB tool argument (10× under the frame cap) SIGSEGVs the sandbox server through
   `mcp_server.cpp:209`.

Both are small, targeted fixes (correct/extend two tables + bound the recursion at ~8
levels); everything else can stand as-is. Re-review after those two, ideally with the
table-driven driver-name test suggested in R2-F8.
