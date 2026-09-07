// src/agent/cartography/composition.h
#pragma once

//
// Composition solver (Design System 4.0, Milestones D/E / ADR 0131).
//
// Deterministic pre-compile pass over a MapSpec v2 document. Everything an
// agent should not have to hand-compute resolves here:
//
//   1. anchors       — item.anchor {edge, margin_mm} → concrete rect_mm
//                      origin (item size comes from rect_mm or min_size_mm).
//   2. size clamps   — min_size_mm / max_size_mm enforced on rect_mm.
//   3. constraints   — align / match_width / match_height / stack /
//                      distribute, solved once each in declared order
//                      (bounded: no iteration to fixpoint, no backtracking).
//
// The solver mutates ONLY concrete geometry fields (rect_mm); content is
// never touched. Resolution is a pure function of the spec + the token
// margin default, so identical inputs produce byte-identical documents.
// Unsatisfiable outcomes (e.g. an anchor edge that would push the item off
// the page even before clamping) are reported, never silently "fixed" by
// moving other items.
//

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::agent::cartography {

struct CompositionResult
{
    int anchorsResolved = 0;
    int sizesClamped = 0;
    int constraintsSolved = 0;
    /// Deterministic, human-readable reports for constraint/anchor outcomes
    /// the solver could not satisfy (also surfaced by cartography preflight).
    std::vector<std::string> unsatisfied;

    Json::Value toJson() const;
};

/// Resolves anchors, size bounds, and constraints on `spec` in place.
/// `marginDefaultMm` is used when an anchor (or stack gap) does not declare
/// its own margin (callers pass the resolved token set's spacing.margin_mm).
CompositionResult resolveComposition( Json::Value &spec, double marginDefaultMm );

} // namespace sicnu::agent::cartography
