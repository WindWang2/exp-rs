// src/agent/cartography/composition.h
#pragma once

//
// Composition solver — bounded, explainable constraint engine.
//
// Platform 6.0 introduced the bounded relaxation pipeline (issue #805).
// Platform 7.0 makes the outcome *explainable* and the fixpoint choice a
// policy instead of an accident:
//
//   Parse → Normalize (item index, canonical order)
//         → Dependency graph (leader→follower edges, cycle detection)
//         → Canonical constraint ordering (multi-fixpoint policy)
//         → HARD phase: anchors + size clamps + hard constraints, bounded
//           relaxation to a satisfaction fixpoint
//         → SOFT phase: priority-desc greedy application with rank-aware
//           conflict rejection (a soft application that would break a hard
//           constraint or a higher-ranked soft constraint is reverted and
//           reported, never silently kept)
//         → Explanation: for every unsatisfied hard constraint, a bounded
//           unsat-core search reports a minimal conflicting subset
//         → Objective accounting: weighted soft satisfaction
//         → Decisions ledger + convergence / violation report
//
// Supported constraint kinds: align, match_width, match_height, stack,
// distribute, above, below, left_of, right_of, inside, keep_with,
// avoid_overlap, fit_content; plus anchors, min/max size bounds, safe-area
// anchoring and multi-page placement.
//
// v4 constraint surface (MapSpec): constraint items may declare
//   hardness: "hard" (default) | "soft"
//   priority: integer 0..100 (default 50)
//   weight:   number 0..1000 (default 1; soft constraints only)
//
// Determinism contract (Platform 7.0):
//   * items are indexed in canonical collection order; constraints are
//     ordered canonically — (hard before soft, priority desc, weight desc,
//     canonical index asc) — so the chosen fixpoint is independent of
//     declaration order except for constraints tied on the whole ordering
//     key, whose relative order is by design the declaration order.
//     Identical input produces byte-identical output, and distinct-key
//     systems produce identical geometry under declaration permutations;
//   * relaxation is bounded (kMaxRelaxationPasses hard, kMaxSoftPasses soft);
//     exceeding a budget is a reported diagnostic, never a hang;
//   * every movement comes from a declared constraint — nothing is moved
//     "to make things fit" silently; every application, rejection and
//     disablement is recorded in the decisions ledger;
//   * contradictions (cyclic dependencies, unsatisfiable constraints,
//     non-convergence, anchor conflicts) are reported with the constraint
//     identities involved; unsatisfied hard constraints carry a bounded
//     unsat core (minimal conflicting subset found within the declared
//     search radius) — a neighborhood witness, honestly bounded, not a
//     claimed global minimum.
//
// The solver mutates ONLY concrete geometry fields (rect_mm); content is
// never touched. Unsatisfiable outcomes are reported, never "fixed" by
// inventing geometry.
//

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::agent::cartography {

/// Relaxation is bounded: a consistent layout converges in a handful of
/// passes; this budget is the hard stop that turns non-convergence into a
/// diagnostic instead of a hang.
inline constexpr int kMaxRelaxationPasses = 24;

/// Platform 7.0: soft-phase sweeps are bounded separately (soft applications
/// cascade: a later soft may un-block an earlier one's inputs only via
/// fit_content-style materialization, so a handful of passes suffice).
inline constexpr int kMaxSoftPasses = 8;

/// Platform 7.0 unsat-core search: the conflict neighborhood of a failed
/// constraint is capped at this many candidates (canonical order), subsets
/// tested up to this size, with a total simulation budget. Bounds make the
/// search deterministic and cheap at the cost of completeness — a truncated
/// search is reported as such, never presented as a global minimum.
inline constexpr int kMaxCoreCandidates = 8;
inline constexpr int kMaxCoreSubsetSize = 3;
inline constexpr int kMaxCoreSimulations = 32;

/// Platform 7.0: the decisions ledger is bounded (docs are small; a runaway
/// ledger signals a modeling error and is capped with an explicit note).
inline constexpr int kMaxDecisions = 256;

/// Platform 9.0: per-pass convergence trace. Each pass records the applied
/// constraint identities (capped — a pass writing more than this many
/// constraints is reported as truncated); the whole trace is bounded by the
/// existing 24-pass budget.
inline constexpr int kMaxTraceAppliedPerPass = 32;

/// One recorded solver decision (application, rejection, disablement).
struct CompositionDecision
{
    std::string cid;      ///< constraint identity (declared id or kind#index)
    std::string kind;     ///< constraint kind
    std::string outcome;  ///< satisfied|applied|blocked|failed|rejected|
                          ///< anchor_wins|cycle|reverted
    std::string reason;   ///< human-readable, includes conflicting identities
    int order = 0;        ///< canonical application order (0-based)
};

/// One unsatisfied constraint with its reason (structured counterpart of the
/// human-readable `unsatisfied` strings).
struct CompositionViolation
{
    std::string cid;
    std::string kind;
    std::string reason;
};

/// Platform 9.0 oscillation attribution: a constraint still writing at the
/// pass budget is part of a contradictory cycle (its writes flip geometry
/// back and forth). Reported with the identity of the fighters, never
/// silently absorbed — the layout itself is rolled back to the pre-solve
/// snapshot (the #864 contract).
struct CompositionOscillation
{
    std::string cid;
    std::string kind;
};

/// Platform 9.0 convergence trace: one entry per hard-phase pass in which
/// at least one constraint wrote.
struct CompositionTraceEntry
{
    int pass = 0;                        ///< 1-based pass number
    int moves = 0;                       ///< constraints that wrote this pass
    std::vector<std::string> applied;    ///< those constraints' cids
    bool truncated = false;              ///< applied list hit the cap
};

struct CompositionResult
{
    int anchorsResolved = 0;
    int sizesClamped = 0;
    int constraintsSolved = 0;
    /// Declared solver-kind hard constraints, including arity-failed
    /// (unresolvable) entries, which are also reported individually;
    /// excludes non-solver/legacy constraint items. (6.0 semantics: this
    /// counted ALL solver constraints; since 7.0 soft constraints are
    /// reported through softTotal — documents without hardness fields are
    /// all-hard, so the historic meaning is preserved for them.)
    int constraintsTotal = 0;
    /// Relaxation passes actually used by the hard phase (1 = converged in a
    /// single sweep).
    int passes = 0;
    /// False when the hard-phase pass budget was exhausted with constraints
    /// still violated (their ids are listed in unsatisfied/violated).
    bool converged = true;
    /// Deterministic, human-readable reports for constraint/anchor outcomes
    /// the solver could not satisfy (also surfaced by cartography preflight).
    std::vector<std::string> unsatisfied;

    // --- Platform 7.0 explainability surface (additive) ---------------------

    /// Declared soft constraints and how many ended satisfied at the final
    /// geometry (reverted/rejected softs count as unsatisfied).
    int softTotal = 0;
    int softSatisfied = 0;
    /// Weighted objective over soft constraints: Σ weight of satisfied and of
    /// violated/rejected softs (hard constraints contribute to `converged`,
    /// not to the objective).
    double satisfiedWeight = 0.0;
    double violatedWeight = 0.0;
    /// Bounded decisions ledger (applications, rejections with the conflicting
    /// identity, anchor/cycle disablements).
    std::vector<CompositionDecision> decisions;
    /// Structured violations (cid + reason); `unsatisfied` carries the same
    /// information as human-readable strings for compatibility.
    std::vector<CompositionViolation> violated;
    /// For each unsatisfied hard constraint: the bounded unsat-core search
    /// outcome as {constraint: cid, core: [cids], bounded: true|false} —
    /// core lists the minimal conflicting subset found within the search
    /// radius (always includes the constraint itself); `bounded: true` marks
    /// a truncated (non-exhaustive) search.
    Json::Value unsatCores = Json::Value( Json::arrayValue );
    /// The fixpoint policy applied ("canonical: hard, priority desc, weight
    /// desc, index asc") — the answer to "why THIS layout".
    std::string fixpointPolicy;

    // --- Platform 9.0 solver-evidence surface (additive) --------------------

    /// Constraints still writing when the hard-phase pass budget was
    /// exhausted (the contradictory cycle behind a non-converged layout).
    /// Empty when converged. Layout is rolled back regardless (#864).
    std::vector<CompositionOscillation> oscillations;
    /// Per-pass hard-phase trace (moves + which constraints wrote), bounded
    /// by the pass budget and the per-pass cap.
    std::vector<CompositionTraceEntry> trace;

    Json::Value toJson() const;
};

/// Resolves anchors, size bounds, and constraints on `spec` in place through
/// the bounded pipeline described above. `marginDefaultMm` is used when an
/// anchor (or stack gap) does not declare its own margin (callers pass the
/// resolved token set's spacing.margin_mm).
CompositionResult resolveComposition( Json::Value &spec, double marginDefaultMm );

/// Platform 9.0 scoped re-solve: identical pipeline, restricted to the
/// constraints that touch at least one of `focusItemIds` (anchors and size
/// clamps apply to focus items only). Used by bounded repair loops so a
/// later pass cannot pay the full-document solve cost; for a focus set
/// whose constraints reference only focus items, the focus-set geometry
/// equals the full solve's geometry on that set (same canonical ordering,
/// same fixpoint policy).
CompositionResult resolveCompositionScoped( Json::Value &spec, double marginDefaultMm,
                                            const std::vector<std::string> &focusItemIds );

} // namespace sicnu::agent::cartography
