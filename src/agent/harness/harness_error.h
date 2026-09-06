// src/agent/harness/harness_error.h
#pragma once

//
// Harness 4.0 stable error taxonomy (mission Phase 12).
//
// One vocabulary for every agent-facing failure in the harness: preflight
// blockers, entity resolution misses, execution failures, verification
// failures, and map confirmation failures. Pi reads codes — it never parses
// arbitrary log text to guess what went wrong.
//
// The codes required by the Harness 4.0 mission are spelled exactly as
// specified; harness-internal codes extend the same style. Every code maps to
// a category ("validation" | "io" | "runtime" | "environment") and a retry
// policy ("none" | "manual" | "transient"). Only "transient" codes may be
// retried automatically, and even then bounded (PlanRunner policy, Phase 13).
//

#include <json/json.h>
#include <string>

namespace sicnu::agent::harness {

/// All stable error codes. Values are the wire strings; never rename.
namespace error_codes {
inline constexpr const char *kDatasetNotFound = "DATASET_NOT_FOUND";
inline constexpr const char *kBandRoleUnresolved = "BAND_ROLE_UNRESOLVED";
inline constexpr const char *kCrsMismatch = "CRS_MISMATCH";
inline constexpr const char *kGridMismatch = "GRID_MISMATCH";
inline constexpr const char *kInvalidRadiometry = "INVALID_RADIOMETRY";
inline constexpr const char *kInsufficientMemory = "INSUFFICIENT_MEMORY";
inline constexpr const char *kModelIncompatible = "MODEL_INCOMPATIBLE";
inline constexpr const char *kModelNotReady = "MODEL_NOT_READY";
inline constexpr const char *kExecutionFailed = "EXECUTION_FAILED";
inline constexpr const char *kCancelled = "CANCELLED";
inline constexpr const char *kOutputInvalid = "OUTPUT_INVALID";
inline constexpr const char *kMapPreflightFailed = "MAP_PREFLIGHT_FAILED";
inline constexpr const char *kPreflightBlocked = "PREFLIGHT_BLOCKED";
inline constexpr const char *kEntityAmbiguous = "ENTITY_AMBIGUOUS";
inline constexpr const char *kInvalidPlan = "INVALID_PLAN";
inline constexpr const char *kInvalidParameter = "INVALID_PARAMETER";
inline constexpr const char *kTransientFailure = "TRANSIENT_FAILURE";
inline constexpr const char *kPathOutsideWorkspace = "PATH_OUTSIDE_WORKSPACE";
inline constexpr const char *kWorkflowNotFound = "WORKFLOW_NOT_FOUND";
inline constexpr const char *kToolNotFound = "TOOL_NOT_FOUND";
inline constexpr const char *kNotSupported = "NOT_SUPPORTED";
} // namespace error_codes

/// Retry policy class for an error code.
enum class RetryClass {
  None,      ///< Never retry (deterministic failures: validation, preflight).
  Manual,    ///< Only an explicit agent/user decision may retry.
  Transient, ///< Safe to auto-retry, bounded by the caller's policy.
};

/// Error family, aligned with the SpatialToolResult categories.
/// "validation" | "io" | "runtime" | "environment".
std::string errorCategoryForCode( const std::string &code );

RetryClass retryClassForCode( const std::string &code );

const char *retryClassToString( RetryClass retryClass );

/// True when `code` is part of the stable taxonomy (guards against typos in
/// producers; unknown codes degrade to category "runtime", retry Manual).
bool isKnownErrorCode( const std::string &code );

/// Structured harness error: one code, one summary, structured details,
/// recoverability, and machine-actionable suggested actions.
struct HarnessError {
  std::string code;
  std::string summary;
  Json::Value details{Json::objectValue};
  bool recoverable = false; ///< The agent can fix this by changing inputs/plans.
  Json::Value suggestedActions{Json::arrayValue}; ///< [{action, arguments}]

  /// Wire shape: {code, summary, details, recoverable, suggested_actions,
  /// category, retry_class}.
  Json::Value toJson() const;

  static HarnessError make( const std::string &code, const std::string &summary );
  static HarnessError make( const std::string &code, const std::string &summary,
                            Json::Value details );
  static HarnessError make( const std::string &code, const std::string &summary,
                            Json::Value details, bool recoverable,
                            Json::Value suggestedActions );
  static HarnessError makeWithAction( const std::string &code, const std::string &summary,
                                      const std::string &action, Json::Value arguments );
};

/// Canonical suggested action: {action, arguments} (contract makeRepairSuggestion shape).
Json::Value suggestedAction( const std::string &action, Json::Value arguments );

/// Wraps a HarnessError into a failed SpatialToolResult-shaped JSON envelope:
/// {success:false, error:{message, code, category, retryable, details,
/// recoverable, suggested_actions}}.
Json::Value errorEnvelope( const HarnessError &error );

/// Parses a legacy "error code" string (e.g. a SpatialToolResult errorCode or
/// an McpToolError code) into the stable taxonomy. Known codes pass through;
/// a small alias table maps the historical codes; everything else becomes
/// EXECUTION_FAILED (recoverable=false) so Pi never sees an unmapped code on
/// the harness surface.
HarnessError normalizeLegacyError( const std::string &legacyCode, const std::string &message );

} // namespace sicnu::agent::harness
