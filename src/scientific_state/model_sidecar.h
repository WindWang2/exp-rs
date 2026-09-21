/***************************************************************************
  scientific_state/model_sidecar.h
  RS14-01 Scientific Data Passport — classifier model sidecar parser.

  Parses the repo's real classifier sidecar ("<model>.meta.json", versions 1
  and 2 — written by RsClassificationPipeline::saveModelSidecarV2, see
  src/analysis/classification/rs_classification_pipeline.h) into plain,
  Qt-free ModelSidecarFacts. The core scientific-state library stays
  jsoncpp-only; this is the only bridge that reads sidecar JSON text.

  Untrusted-input discipline (repo convention): CharReaderBuilder with
  stackLimit 128, jsoncpp exceptions caught and surfaced as typed errors —
  non-JSON ⇒ MalformedJson, JSON missing key fields ⇒ InvalidField.
  The declared method value is projected verbatim: an unknown method is a
  fact about the model, never a parse error.
 ***************************************************************************/

#ifndef SICNU_SCIENTIFIC_STATE_MODEL_SIDECAR_H
#define SICNU_SCIENTIFIC_STATE_MODEL_SIDECAR_H

#include "scientific_state/asset_state_types.h"
#include "scientific_state/state_facts.h"

#include <string>

namespace sicnu::state
{

/// Parses @p text as a classifier sidecar document into @p out.
/// @p sidecarPath is recorded verbatim for provenance. On failure @p error
/// carries the typed code and @p out is left untouched. Sorts and dedups
/// the projected labels; deterministic for identical input.
bool parseClassifierSidecarJson( const std::string &text, const std::string &sidecarPath,
                                 ModelSidecarFacts &out, AssetStateError &error );

} // namespace sicnu::state

#endif // SICNU_SCIENTIFIC_STATE_MODEL_SIDECAR_H
