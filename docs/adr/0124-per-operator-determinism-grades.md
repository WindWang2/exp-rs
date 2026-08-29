# ADR 0124: Per-Operator Determinism Grades for Parallel Execution

## Status

Accepted (implemented by the determinism-grade schema ticket)

## Context

The performance effort (workflow parallel throughput, large-raster memory,
single-algorithm speed) introduces parallel and blocked execution into operator
kernels: OpenMP/SIMD loops, horizontal-strip chunking, block-streamed two-pass
statistics. Standard parallel-reduction techniques **reorder floating-point
accumulation**, so continuous statistics (regional means, GLCM probability
sums, covariance entries) differ from the serial result by ~1e-7 relative —
bit-level identity is lost even though the algorithm is unchanged.

The repo's standing rule is "strict numeric semantics, no behavior change for
test-passing convenience" (audit-era constraint). A blanket bit-exact rule,
however, forbids the parallel reductions that deliver most of the C-dimension
(speed) gains; a blanket tolerance rule hides the few places where exact
reproducibility is a real product promise.

Existing structure that constrains the choice:

- Discrete-verdict outputs (class maps, change labels, majority/recode
  categories) are already stable under any scheduling — only continuous
  statistics are sensitive.
- Serial single-threaded execution is the regression baseline used by the
  existing Catch2 suites.
- Operator schemas are the uniform contract consumed by GUI, CLI, MCP, and
  agents (ADR 0120/0122), so any per-operator declaration has a natural home.

## Decision

Introduce a **Determinism Grade** as a first-class, per-operator schema
declaration with two values:

1. **`bit-exact`** — results are identical regardless of execution scheduling
   and reduction order. The operator forbids parallel accumulation reordering;
   parallelism is restricted to independent output regions (per-block writes,
   per-block SIMD) that cannot interact.
2. **`tolerance`** — results stay within a documented relative tolerance
   (default 1e-6) of the serial result under parallel/blocked execution. The
   operator may use parallel reductions.

Rules:

- The grade is declared in the operator schema (JSON I/O contract), visible to
  GUI, CLI, MCP tools, and agents; changing an operator's grade is a
  schema-visible, reviewable event.
- Discrete-verdict operators are expected to be stable under either grade; the
  grade governs continuous statistics only.
- **Serial single-threaded paths keep bit-exact output unconditionally** — they
  remain the regression anchor for all tests.
- Tolerance-grade tests assert within-tolerance equivalence against the serial
  result; bit-exact tests assert exact equality.

## Consequences

- Parallel and blocked implementations may reorder accumulation only within
  the declared grade; reviewers check the grade against the implementation.
- Users and agents can read the grade from the schema and know whether two
  runs must match bit-for-bit.
- The serial path stays the single source of truth for regression tests, so
  the existing suites keep their semantics (no weakened assertions).
- Operators that later gain parallelism must declare (and justify) a grade
  change explicitly rather than silently changing numeric behavior.
