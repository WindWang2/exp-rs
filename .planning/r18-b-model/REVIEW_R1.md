# R18-B Review Round 1 (independent reviewer — authored nothing in this branch)

Reviewer verdict: **PASS-WITH-NITS** (0 × P0, 0 × P1, 0 × P2, 8 × P3).
Full adversarial trace of every fix (slide direction/scope, deferred-walk
ordering vs ceilings, preflight repositioning effects on stats/sidecars, fsync
failure semantics, lock orders, sink-guard false positives, restoration byte
fidelity, test vacuousness) — all mechanically correct; the restoration diff
against f326d8ca9 verified EMPTY for all 35 restored paths and master
interaction empty.

Nits (all P3): (1) doc verification-command wording — fixed in 498012d25;
(2) flushThrough finalize unclamped vs slide clamp — FIXED structurally in
498012d25 (std::min with m_top + m_span); (3) comment tying finish() to the
deferred walk — added in 498012d25; (4) M5 span claim 24→16 — fixed;
(5) W1 pin vacuous on OpenCV-5 hosts (disclosed by design, r14-B precedent);
(6) W2 pin ~8 s wall (bounded, acceptable); (7) memcpy vs memmove (safe as
written since cleared ≥ 1); (8) pre-existing stage-litter asymmetry on
rename failure — recorded in KNOWN_FINDINGS.md.

Reviewer's follow-up ask (not this PR): the structural clamp is now in, so
the class is closed in-tree.
