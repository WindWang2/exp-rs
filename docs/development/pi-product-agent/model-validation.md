# Model validation evidence

The normal application reuses the existing model settings and secret store.
Real-provider validation runs through its packaged GUI and native tool host.
Loopback SSE fixtures validate deterministic contracts and are reported
separately from StepFun calls. A candidate PR-head application is labelled as a
candidate; the final Windows application must come from synchronized master.

Enable the existing trace bootstrap with `SICNU_TRACE=1` and a fresh
`SICNU_TRACE_DIR`. Each accepted, traced conversation round produces a bounded
`agent.run.metrics` observation in the shared trace sink. Extract only that event
into validation evidence: the global trace can contain unrelated diagnostics and
must not be copied wholesale into a report or artifact.

Metrics contain backend, terminal classification, provider invocation attempts,
usage report counts and coverage, numeric tokens, monotonic elapsed time and
time to first content/reasoning delta. They contain no profile IDs, endpoints,
headers, prompts, model text, tool payloads, raw provider errors or credentials.
They do not schedule, approve, cancel, retry or publish work.

`elapsed_us` includes model and tool/approval waiting through conversation
settlement. It excludes initial installation verification and does not prove
native task or asset completion. `first_model_delta_us` is the first observed
content/reasoning delta, not an exact provider first-token timestamp. Attempts
include compaction and failed invocations; they are not confirmed billable HTTP
requests. Explicit cancellation remains cancelled; destruction before any
settlement is incomplete.

Native tracing requests standard OpenAI streaming usage and preserves final
empty-choice usage frames. Other clients and untraced Native runs keep their
previous request body. Pi SDK usage and provider SSE usage have different source
labels. SDK defaults may contain zero when the provider omits usage. Missing or
partial coverage remains explicit; observed token sums are not invented totals.
Cached input is a subset of prompt tokens and reasoning is not double-counted.

`cost_status` is `unpriced` and `cost` is null. Currency cost needs a separately
verified provider bill or dated price source; SDK default zero proves neither
free requests nor savings. Report every attempted scenario and its denominator,
with error, cancellation and incomplete outcomes distinguished. A successful
single scenario is not a stable error-rate or performance estimate.

Record host and owned Node process working-set/private-byte peaks and CPU deltas
externally, without command-line or environment dumps. Keep package source SHA,
full artifact inventory, sample interval and measurement gaps with those results.
The scientific oracle remains the native task, validated GeoTIFF pixels/CRS,
asset ID and input lineage; assistant prose and runtime metrics cannot replace it.
