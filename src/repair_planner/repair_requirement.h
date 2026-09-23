// src/repair_planner/repair_requirement.h
#pragma once

//
// RS14-03 completion slice B: requirement synthesis.
//
// A CLOSED mapping turns preflight finding codes into typed repair
// requirements. The mapping table below is the planner's only finding
// vocabulary; the codes are the ones the harness preflight and the compiler
// already emit (scientific_preflight.cpp rule packs, harness_error.h
// error_codes::*). This module invents no new finding code.
//
// Fail-closed discipline:
//   - a finding code outside the table becomes a requirement of kind
//     `unsupported` with a typed reason — it is never dropped and never
//     guessed into a known kind;
//   - malformed finding documents (non-object, missing/empty code, unknown
//     severity) are typed invalid_input errors;
//   - requirement order is deterministic: severity rank desc (blocking
//     first), then finding code asc, then subject asc — the presentation
//     order discipline of the preflight report (findingLess).
//
// PLANNING-ONLY: requirements describe what a repair would have to achieve;
// nothing here touches data, registries, or execution.

#include <json/json.h>
#include <string>
#include <vector>

#include "repair_schema.h"

namespace sicnu::repair {

/// Closed requirement kinds — the planner-side target vocabulary.
namespace requirement_kind {
inline constexpr const char *kCrsAlign = "crs_align";
inline constexpr const char *kGridAlign = "grid_align";
inline constexpr const char *kRadiometricState = "radiometric_state";
inline constexpr const char *kCalibrationDomain = "calibration_domain";
inline constexpr const char *kPolarizationSelect = "polarization_select";
inline constexpr const char *kModalityCheck = "modality_check";
inline constexpr const char *kBandRole = "band_role";
inline constexpr const char *kTemporalAlign = "temporal_align";
inline constexpr const char *kModelContract = "model_contract";
inline constexpr const char *kTrainingData = "training_data";
inline constexpr const char *kDatasetSubstitution = "dataset_substitution";
inline constexpr const char *kCategoricalCheck = "categorical_check";
inline constexpr const char *kQualityMask = "quality_mask";
/// A finding code outside the closed table. Carries unsupportedReason;
/// candidates for it are refusals, never offers.
inline constexpr const char *kUnsupported = "unsupported";
} // namespace requirement_kind

/// One synthesized requirement.
struct RepairRequirement
{
    std::string requirementId;     ///< "req-<n>", positional and deterministic
    std::string findingCode;       ///< the input finding code, verbatim
    std::string kind;              ///< requirement_kind::* (kUnsupported possible)
    std::string severity;          ///< unified: "error" | "warning" | "advice" | "info"
    bool blocking = false;         ///< true for error-severity findings
    std::string subject;           ///< slot or asset the finding is about
    Json::Value evidence{Json::objectValue};   ///< finding evidence, verbatim
    std::string unsupportedReason; ///< non-empty iff kind == unsupported
};

bool isKnownRequirementKind( const std::string &kind );

/// The closed finding-code -> requirement kind mapping. Empty string for
/// codes outside the table (the typed-unsupported path).
std::string requirementKindForFindingCode( const std::string &findingCode );

/// Unifies harness/preflight severity words onto the closed ladder. Empty for
/// unknown words (invalid input).
std::string normalizeFindingSeverity( const std::string &severity );

/// Synthesizes requirements from finding documents. Deterministic order;
/// requirement ids are assigned AFTER sorting, so the same findings always
/// produce the same ids. Unknown codes yield typed unsupported requirements.
/// Malformed documents return false with error.code == "invalid_input".
bool synthesizeRequirements( const std::vector<Json::Value> &findings,
                             std::vector<RepairRequirement> &out, RepairError &error );

Json::Value requirementToJson( const RepairRequirement &requirement );

} // namespace sicnu::repair
