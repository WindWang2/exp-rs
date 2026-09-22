// src/verify/verify_error_codes.h
#pragma once

//
// Unified Scientific Verifier (ADR 0172) — typed failure / cause codes.
//
// Stable wire strings, APPEND-ONLY: never rename a code, never repurpose
// one. "verify:e_*" marks a hard FAIL (the checked contract is violated);
// "verify:i_*" marks INDETERMINATE (the verdict could not be reached —
// capability, provider or data missing). The prefix split is the contract
// that lets an agent distinguish "replan" from "retry/fix-input" without
// understanding individual codes.
//
// Machine-facing suggestion mapping lives in verify_render; the codes
// themselves stay free of policy.
//

#include <string>

namespace sicnu::verify
{

// ---- hard failures -------------------------------------------------------
inline constexpr const char *kCodeInvalidSpec = "verify:e_invalid_spec";
inline constexpr const char *kCodeDuplicateCheckId = "verify:e_duplicate_check_id";
inline constexpr const char *kCodeStateViolated = "verify:e_state_violated";
inline constexpr const char *kCodeArtifactMissing = "verify:e_artifact_missing";
inline constexpr const char *kCodeArtifactTooSmall = "verify:e_artifact_too_small";
inline constexpr const char *kCodeTypeMismatch = "verify:e_type_mismatch";
inline constexpr const char *kCodeGridMismatch = "verify:e_grid_mismatch";
inline constexpr const char *kCodeSchemaMismatch = "verify:e_schema_mismatch";
inline constexpr const char *kCodeMetricOutOfRange = "verify:e_metric_out_of_range";
inline constexpr const char *kCodeRelationViolated = "verify:e_relation_violated";
inline constexpr const char *kCodeProvenanceIncomplete = "verify:e_provenance_incomplete";
inline constexpr const char *kCodeDigestMismatch = "verify:e_digest_mismatch";
inline constexpr const char *kCodeCrossOutputInconsistent = "verify:e_cross_output_inconsistent";

// ---- indeterminate causes -------------------------------------------------
inline constexpr const char *kCodeProviderMissing = "verify:i_provider_missing";
inline constexpr const char *kCodeArtifactUnreadable = "verify:i_artifact_unreadable";
inline constexpr const char *kCodeMetricMissing = "verify:i_metric_missing";
inline constexpr const char *kCodeMetricNotFinite = "verify:i_metric_not_finite";
inline constexpr const char *kCodeProvenanceMissing = "verify:i_provenance_missing";
inline constexpr const char *kCodeDigestUnavailable = "verify:i_digest_unavailable";
inline constexpr const char *kCodeEmptyInput = "verify:i_empty_input";

/// @returns true when @p code belongs to the closed verifier vocabulary.
bool isVerifierCode( const std::string &code );

/// @returns true for the indeterminate ("i_") class — the replan-vs-retry
/// split agents rely on.
bool isIndeterminateCode( const std::string &code );

} // namespace sicnu::verify
