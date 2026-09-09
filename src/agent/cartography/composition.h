// src/agent/cartography/composition.h
#pragma once

//
// Composition solver — Knowledge Platform 6.0 (bounded constraint engine).
//
// Deterministic pre-compile pass over a MapSpec v2/v3 document. Everything an
// agent should not have to hand-compute resolves here through an explicit,
// bounded pipeline (Platform 6.0, Milestone B — replaces the single
// declared-order pass whose outcome depended on constraint declaration
// order, issue #805):
//
//   Parse → Normalize (item index, canonical order)
//         → Dependency graph (leader→follower edges, cycle detection)
//         → Intrinsic size estimation (rect_mm | min_size_mm | content_mm)
//         → Constraint propagation (equality/relational application)
//         → Iterative relaxation (bounded passes to a satisfaction fixpoint)
//         → Collision handling (declared avoid_overlap pairs)
//         → Quality scoring (satisfied/total, anchors, clamps, passes)
//         → Convergence / unsatisfied report
//
// Supported constraint kinds: align, match_width, match_height, stack,
// distribute, above, below, left_of, right_of, inside, keep_with,
// avoid_overlap, fit_content; plus anchors, min/max size bounds, safe-area
// anchoring and multi-page placement.
//
// Determinism contract:
//   * items are indexed in canonical collection order, constraints iterate in
//     declared order — identical input produces byte-identical output;
//   * relaxation is bounded (kMaxRelaxationPasses); for consistent systems the
//     satisfaction fixpoint is order-independent, so permuting the declared
//     constraint order converges to the same geometry;
//   * every movement comes from a declared constraint — nothing is moved
//     "to make things fit" silently;
//   * contradictions (cyclic leader/follower dependencies, unsatisfiable
//     constraints, non-convergence) are reported with the constraint ids
//     involved, never silently absorbed.
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

struct CompositionResult
{
    int anchorsResolved = 0;
    int sizesClamped = 0;
    int constraintsSolved = 0;
    /// Total declared solver constraints (excludes malformed/unresolvable
    /// entries, which are reported individually).
    int constraintsTotal = 0;
    /// Relaxation passes actually used (1 = converged in a single sweep).
    int passes = 0;
    /// False when the pass budget was exhausted with constraints still
    /// violated (their ids are listed in unsatisfied).
    bool converged = true;
    /// Deterministic, human-readable reports for constraint/anchor outcomes
    /// the solver could not satisfy (also surfaced by cartography preflight).
    std::vector<std::string> unsatisfied;

    Json::Value toJson() const;
};

/// Resolves anchors, size bounds, and constraints on `spec` in place through
/// the bounded pipeline described above. `marginDefaultMm` is used when an
/// anchor (or stack gap) does not declare its own margin (callers pass the
/// resolved token set's spacing.margin_mm).
CompositionResult resolveComposition( Json::Value &spec, double marginDefaultMm );

} // namespace sicnu::agent::cartography
