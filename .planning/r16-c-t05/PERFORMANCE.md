# r16-c-t05 — performance

Standalone probe (`/tmp/bench_r16c.cpp`, g++ -O2, linked against the exact
static libs built from this branch), two consecutive runs, steady-clock
timings in ms. Machine: shared CI-style box (other agents building
concurrently); both runs within a few percent of each other, so the numbers
are stable enough to serve as a baseline. No wall-clock assertions exist in
any suite (determinism requirement).

| Workload | Size | Metric | Run 1 | Run 2 |
|---|---|---|---|---|
| LabSession canonical serialization (`sessionToCanonicalBytes`) | 50,000 events (2.43 MB doc) | serialize / reload | 183.7 / 164.9 ms | 182.5 / 167.4 ms |
| LabSessionStore save / loadAny (disk) | same session | save / load | 182.4 / 358.4 ms | 181.4 / 367.1 ms |
| LabStepTimeline::fromLabSpec + toJson | 5,000 steps | project / toJson | 37.6 / 55.5 ms | 38.2 / 54.1 ms |
| LabFeedbackProjection::fromReports + fromJson restore | 20,000 checks (20,001 rows) | project / restore | 73.7 / 165.6 ms | 74.9 / 164.3 ms |
| CourseHomeViewModel::fromDocuments | 200 modules × 10 labs | project | 7.7 ms | 7.8 ms |

Reading: everything is linear in input size at µs-per-item scale
(~3.7 µs/event serialization; ~4 µs/check projection). Real student
sessions hold tens-to-hundreds of events and labs hold tens of steps, i.e.
sub-millisecond projections on the interactive path. The identity check
added in WP3 is O(len(session_id)) once per load; the WP2 listing fix adds
one error_code per directory. Neither is measurable at these scales.

Decision: **no optimization made, none warranted** — no perf claims beyond
the table above.
