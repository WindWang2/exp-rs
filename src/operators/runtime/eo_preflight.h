// src/operators/runtime/eo_preflight.h — Platform 10.0 EO domain preflight.
//
// The manifest's `eo` section is the EO model truth layer: the physical facts
// a model requires of its input raster (radiometric calibration state,
// per-band-role wavelength windows, CRS family). This module ENFORCES those
// facts against a concrete input raster through the canonical metadata layer
// (`inspectRaster` — never a guess), before any tile is read:
//
//   - a declared + ENFORCED calibration is verified (missing input fact is a
//     typed refusal — fail-closed; the model must never consume an unverified
//     radiometric domain),
//   - wavelength windows refuse when BOTH sides declare the fact (model
//     window + input band wavelength); absence on either side is recorded as
//     an advisory, never a refusal (absence is meaningful, not a mismatch),
//   - GSD/resolution-range and CRS-family facts follow the same rule
//     (resolution_range stays the historical advisory ranking fact).
//
// No Qt here: path in / path out.
#pragma once

#include "operators/framework/model_catalog.h"

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::operators::runtime {

/// What the preflight verified (or skipped) — recorded into inference result
/// payloads and provenance sidecars so a downstream consumer can audit the
/// EO contract at replay time.
struct EoPreflightReport
{
    bool applicable = false;       ///< manifest declares an `eo` section
    bool calibrationVerified = false; ///< enforced calibration matched
    std::vector<std::string> checks;  ///< human-readable per-fact outcomes
    std::vector<std::string> advisories; ///< non-enforced observations

    Json::Value toJson() const;
};

/// Enforce the model's EO domain contract against @p rasterPath.
/// Throws RSOperatorError(ErrorCode::InvalidInputData) on an enforced
/// mismatch; returns the report otherwise. Never inspects pixel data
/// (metadata only, bounded).
EoPreflightReport enforceEoPreflight( const ModelInfo &model, const std::string &rasterPath );

} // namespace sicnu::operators::runtime
