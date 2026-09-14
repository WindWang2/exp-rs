# DECISIONS — hyperspectral-spectral-intelligence-10

Autonomy defaults applied (goal brief §五/§二; unattended mode). Every entry:
options considered → decision → reason.

## D-1 Structured artifact transport: file path, not grammar change

* Options: (a) extend placeholder grammar to splice JSON arrays into params —
  deep change to `resolvePlaceholderPort` + TaskCenter + WorkflowSession +
  resume path (#727 froze their contract), high blast radius; (b) in-memory
  session artifact store for arrays — not serializable, no digest, breaks
  crash-resume; (c) typed serialized artifact file + path reference +
  typed loader at consumers.
* Decision: (c). `rs:endmember_extraction` writes `endmembersOut` and returns
  `endmembersArtifact` (path string) in the payload; consumers gain `*Ref`
  params. Placeholder grammar, TaskCenter, WorkflowSession untouched.

## D-2 Artifact format: one `exp-rs:spectral-table` JSON, digest-bounded

* Options: (a) reuse spectral-library format for artifacts; (b) new table
  format; (c) raw matrix binary.
* Decision: (b), JSON with sha256 digest over the canonical spectra block.
  (a) conflates curated-library validation rules (license/citation mandatory)
  with machine artifacts; (c) is opaque to Agent/CLI/UI and violates
  "Agent/CLI/UI 都能引用". Size bound: 4 Mi cells/table (typed refusal above),
  ~64 MiB JSON worst case — endmember sets are k≤O(100) spectra, bounds are
  anti-abuse not typical-case.

## D-3 Reference input precedence: inline XOR reference, typed ambiguity error

Both `refs` and `refsRef` supplied → `InvalidParameter` naming both keys.
Backward compatibility: inline-only callers unchanged; schema marks the new
params optional; required-list unchanged.

## D-4 MNF: new kernel beside the old; operator switches; old kernel untouched

`ImageEnhancement::mnf/processMnfFile` stay (tests pin RNG/estimator
semantics). New `mnf_transform` reuses the same noise estimator (horizontal
shift differences) and the same eigensolver utility, adds streaming
statistics accumulation + exposed transform + inverse. `rs:mnf` switches to
the new kernel; outputs verified bit-exact-graded against the new serial path
(streaming == serial), not against the old kernel (eigenvector sign/rotation
may differ; invariants not signs are pinned).

## D-5 FCLS: Lawson–Hanson NNLS + sum-to-one augmentation; OLS default

Default `ols` keeps every existing caller/result stable. `fcls` is opt-in.
Collinear endmembers: refuse (typed) when the endmember Gram matrix has
condition/reciprocal-pivot rank deficiency above a stated tolerance; OLS
keeps current behavior (documented).

## D-6 Preprocessing gaps: band-select operator + shared wavelength helper

Not a kitchen-sink `rs:spectral_preprocess`: each stage is already an
operator (continuum/derivative/resample); the missing piece is band
selection/exclusion + unit normalization, so: one small operator + one
helper. Chained composition stays the platform idiom (ADR 0083 DAG).

## D-7 Library evolution scope: one `rs:library_select` operator

Import/export/provenance fields already exist (v2 + validateLibrary +
LICENSES.md). The operator gap is *selection/projection as pipeline input*.
Near-duplicate detection reports (QA), does not mutate the library. Sparse
unmixing, SID-SAM hybrid, local-RX: recorded as non-goals (YAGNI, no current
caller; AGENTS.md §2).

## D-8 No GUI dialog work

SchemaFormBuilder renders operator schemas; new params surface in GUI
automatically. Dedicated hyperspectral dialog is out of scope.

## D-9 Build/test discipline on the shared host

build-dev preset in the worktree; `-j2` default, `-j1` if RSS > 70% or load >
1.5×16; targeted `ctest -R test_spectral*|test_mnf*|test_endmember*` first;
`QT_QPA_PLATFORM=offscreen`; never `-j$(nproc)`; watch `/tmp` tmpfs pressure
(observed transient ENOSPC at baseline).

## D-10 New operator naming/ids

`rs:mnf_inverse`, `rs:spectral_band_select`, `rs:library_select` — colon
namespace, snake_case, aligned with the existing vocabulary. Sidecar files in
`data/processing/algorithm_meta/` named after the operator id.

## D-11 (added) New operators omit `task` metadata; sidecars untouched

The algorithm_meta drift gate (`tests/test_algorithm_meta_drift.cpp`) pins
the task-declaring descriptor set to 29 with byte-exact sidecars (#707/#729).
`rs:mnf_inverse`, `rs:spectral_band_select`, `rs:library_select` do not
declare task families, so the shipped sidecar set is unchanged and the gate
stays green without regenerating generated artifacts. Promoting them into
task families later goes through `--export-catalog` in the same PR as the
metadata change.

## D-12 (added) WorkflowRuntime payload-port recording is generic

Both step-execution paths record every non-empty STRING result-payload value
as `<stepId>.<port>` artifacts, so `$step.port` resolves identically on the
session path and the TaskCenter/resume path (#727 policy). Qualified keys
are new namespace — no collision with existing artifact names; payload keys
are few and bounded.
