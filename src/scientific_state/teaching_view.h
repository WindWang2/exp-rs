/***************************************************************************
  scientific_state/teaching_view.h
  RS14-01 Scientific Data Passport — teaching view-model (Qt-free).

  Renders a passport for students: the SAME evidence the machine-readable
  JSON carries, split into the five buckets of the claim lattice. A GUI
  widget can embed this directly (see docs/integration.md); the plain-text
  renderer doubles as the CLI `passport --teaching` output.
 ***************************************************************************/

#ifndef SICNU_SCIENTIFIC_STATE_TEACHING_VIEW_H
#define SICNU_SCIENTIFIC_STATE_TEACHING_VIEW_H

#include "scientific_state/asset_state_types.h"

#include <string>
#include <vector>

namespace sicnu::state
{

/// Student-facing summary. Every line names its field path so a student can
/// cross-reference the JSON (and an agent can cross-reference the text).
struct TeachingSummary
{
    std::string headline;                 ///< "Asset: <name> (<kind>)"
    std::vector<std::string> known;       ///< declared facts
    std::vector<std::string> inferred;    ///< resolver-derived facts
    std::vector<std::string> assumed;     ///< documented defaults applied
    std::vector<std::string> conflicted;  ///< disagreeing declarations
    std::vector<std::string> missing;     ///< unknown fields (absence is meaningful)
};

/// Renders @p state. Deterministic; never throws.
TeachingSummary renderTeachingSummary( const RemoteSensingAssetState &state );

/// Plain-text rendering with stable section headers (deterministic bytes).
std::string teachingSummaryToPlainText( const TeachingSummary &summary );

} // namespace sicnu::state

#endif // SICNU_SCIENTIFIC_STATE_TEACHING_VIEW_H
