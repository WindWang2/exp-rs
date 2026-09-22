// src/science_context/observed_state.h
#pragma once

//
// ObservedState — capability-graph-facing projection of passport facts.
// Deliberately a Json document shaped like DatasetUnderstanding body fields
// the harness feasibility checks read (modality, band roles, radiometry, CRS).
//

#include "scientific_state/asset_state_types.h"

#include <json/json.h>

namespace sicnu::science_context {

/// Projects a RemoteSensingAssetState into an ObservedState / understanding
/// body. Conflicted claims are surfaced as `conflicted` flags — never
/// auto-picked. Unknown fields stay absent (never defaulted to a fake value).
Json::Value observedStateFromPassport( const sicnu::state::RemoteSensingAssetState &state );

/// Envelope kind "DatasetUnderstanding" for planner inputFacts slots.
Json::Value understandingEnvelopeFromPassport( const sicnu::state::RemoteSensingAssetState &state );

} // namespace sicnu::science_context
