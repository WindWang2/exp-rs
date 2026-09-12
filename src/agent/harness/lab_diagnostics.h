// src/agent/harness/lab_diagnostics.h
#pragma once

//
// D9: the lab diagnostic brain.
//
// Maps MEASURED facts about a student's broken output onto the existing
// diagnostic catalog (data/help/diagnostics.json) — the six canonical lab
// error signatures:
//
//   all_negative_index   → diagnostic.harness.band_role_unresolved
//   all_nodata           → diagnostic.preflight.nodata_declared
//   kappa_near_zero      → diagnostic.harness.training_invalid
//   blank_change_mask    → diagnostic.harness.output_invalid
//   crs_mismatch         → diagnostic.harness.crs_mismatch
//   scale_stripes        → diagnostic.harness.grid_mismatch
//
// Reuse rule: NO new diagnostic codes are invented. The brain consumes
// observations (raster statistics / accuracy facts produced by inspection
// tools or verification), never student prose. Output is diagnosis-first:
// symptom + most likely cause + EXACTLY ONE verification action, wired to a
// key of the closed harness action vocabulary.
//

#include <json/json.h>
#include <string>

namespace sicnu::agent::harness {

/// Measured facts about one output (or one layer pair). Produced by
/// inspection/verification tooling; never parsed from the student message.
struct LabObservation
{
    bool present = false;

    std::string kind;        ///< "raster_stats" | "accuracy" | "mask" | "layer_pair"
    std::string indexName;   ///< index name when known (e.g. "NDVI")

    double min = 0.0;        ///< min over valid cells
    double max = 0.0;        ///< max over valid cells
    double mean = 0.0;
    double nodataFraction = 0.0; ///< 0..1
    double validFraction = 1.0;  ///< 0..1
    double density = 1.0;        ///< nonzero fraction (masks / change binaries)
    double kappa = -2.0;         ///< present when >= -1 (accuracy observations)

    std::string crsA, crsB;          ///< layer pair: CRS identifiers
    double pixelSizeA = 0.0;         ///< layer pair: pixel sizes (map units)
    double pixelSizeB = 0.0;
};

/// The structured diagnosis. `helpId` reuses the catalog id for the mapped
/// origin code (via HelpId::diagnosticId); `actionKey` is always a key of
/// the closed harness action vocabulary (empty when nothing matched).
struct LabDiagnosis
{
    bool matched = false;
    std::string signature;

    std::string symptomZh;   ///< 现象（观测到什么）
    std::string causeZh;     ///< 最可能原因
    std::string verifyZh;    ///< 唯一的验证动作
    std::string actionKey;   ///< harness action key for the verification

    std::string diagnosticFamily; ///< "harness" | "preflight"
    std::string diagnosticCode;   ///< existing origin code, reused verbatim
    std::string helpId;           ///< diagnostic.<family>.<code>

    Json::Value toJson() const;
};

/// Signature detection: deterministic, most-specific first. Unmatched inputs
/// return matched=false with generic first-look advice.
LabDiagnosis diagnoseLabObservation( const LabObservation &observation );

/// Parses the observation document from tool input into measured facts
/// (unknown/absent fields degrade to defaults; nothing throws).
LabObservation parseLabObservation( const Json::Value &doc );

} // namespace sicnu::agent::harness
