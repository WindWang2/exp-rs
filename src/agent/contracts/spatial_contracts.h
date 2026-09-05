// src/agent/contracts/spatial_contracts.h
#pragma once

//
// Spatial Scientist 3.0 structured contract documents (ADR 0128).
//
// These are *structured tool outputs* — versioned, validated, bounded JSON
// documents that Pi can parse without free-text reasoning. Nothing here keeps
// conversational state or plans multi-step work; this module only defines the
// document shapes, builders, and validators shared by the agent tools.
//

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::agent::contracts {

/// Schema version stamped into every contract document envelope.
inline constexpr const char *kContractsSchemaVersion = "1.0";

// ---------------------------------------------------------------------------
// Envelope: every contract document travels as
//   { "schema_version": "1.0", "kind": "<document kind>", ...payload fields }
// ---------------------------------------------------------------------------

/// Wraps `payload` fields into a versioned envelope of the given kind.
Json::Value makeEnvelope( const std::string &kind, Json::Value payload );

/// Checks schema_version/kind presence and consistency. `expectedKind` may be
/// empty to accept any kind. Returns the first problem found, or empty.
std::string checkEnvelope( const Json::Value &doc, const std::string &expectedKind );

// ---------------------------------------------------------------------------
// DatasetUnderstanding — per-dataset facts for planning (Phase A/C).
// Constructed from raster/vector inspection output, never from free text.
// ---------------------------------------------------------------------------

/// Adapts a spatial:raster_inspect output object into a DatasetUnderstanding
/// document (envelope included). Extracts the planning-relevant summary:
/// size, pixel size, CRS, band roles, nodata, radiometric state, stats flag.
Json::Value datasetUnderstandingFromRasterInspect( const Json::Value &rasterInspect );

/// Adapts a spatial:vector_inspect output object into a DatasetUnderstanding.
Json::Value datasetUnderstandingFromVectorInspect( const Json::Value &vectorInspect );

/// Validates a DatasetUnderstanding document (envelope + required fields).
/// Returns one human-readable problem per entry; empty means valid.
std::vector<std::string> validateDatasetUnderstanding( const Json::Value &doc );

// ---------------------------------------------------------------------------
// CapabilityCandidate — ranked search hit (Phase D/E).
// ---------------------------------------------------------------------------

/// Builds one candidate document. `kind` is "algorithm" or "model".
/// `estimatedCost` is an object; use makeCostEstimate() for the canonical shape.
Json::Value makeCapabilityCandidate( const std::string &candidateId,
                                     const std::string &kind,
                                     double compatibility,
                                     Json::Value reasons,
                                     Json::Value warnings,
                                     Json::Value estimatedCost );

/// Canonical estimated_cost object; omit unknown optional numbers.
Json::Value makeCostEstimate( const std::string &costClass, Json::Int estimatedRamMb,
                              Json::Int estimatedSeconds, bool gpuAccelerated,
                              bool largeRasterSafe );

/// Validates one CapabilityCandidate object (no envelope).
std::vector<std::string> validateCapabilityCandidate( const Json::Value &candidate );

// ---------------------------------------------------------------------------
// PreflightResult — static feasibility verdict (Phase F/M).
// ---------------------------------------------------------------------------

/// Canonical issue object used by workflow preflight, cartography preflight,
/// and result assessment blockers.
Json::Value makeIssue( const std::string &code, const std::string &severity,
                       const std::string &message, bool repairable,
                       const std::string &itemId, Json::Value suggestedAction );

/// Canonical repair suggestion: machine-actionable `{action, arguments}`.
Json::Value makeRepairSuggestion( const std::string &action, Json::Value arguments );

/// Builds a PreflightResult. `verdict` is "ok" | "fixable" | "blocked".
Json::Value makePreflightResult( const std::string &subject, const std::string &verdict,
                                 Json::Value issues, Json::Value checks );

/// Validates a PreflightResult document (envelope included).
std::vector<std::string> validatePreflightResult( const Json::Value &doc );

/// Derives the verdict from an issue list: any "error" → "blocked"; else any
/// repairable warning → "fixable"; else "ok".
std::string verdictFromIssues( const Json::Value &issues );

// ---------------------------------------------------------------------------
// ExecutionPlan — ordered operator steps with IO wiring (Phase F).
// ---------------------------------------------------------------------------

/// One plan step: `{id, operator_id, params, inputs: [{step, port}], outputs}`.
Json::Value makeExecutionStep( const std::string &stepId, const std::string &operatorId,
                               Json::Value params, Json::Value inputs );

/// Builds an ExecutionPlan envelope with `estimates` (ram/disk/seconds).
Json::Value makeExecutionPlan( const std::string &planId, Json::Value steps,
                               Json::Value estimates );

/// Validates structure and referential integrity (inputs reference existing
/// steps, step ids unique). Envelope included.
std::vector<std::string> validateExecutionPlan( const Json::Value &doc );

// ---------------------------------------------------------------------------
// ResultAssessment — post-execution scientific sanity (Phase G).
// ---------------------------------------------------------------------------

/// One assessment check: `{check, passed, severity, code, details}`.
Json::Value makeAssessmentCheck( const std::string &check, bool passed,
                                 const std::string &severity, const std::string &code,
                                 Json::Value details );

/// Assembles the document; `verdict` "pass" | "warn" | "fail".
Json::Value makeResultAssessment( const std::string &target, const std::string &verdict,
                                  Json::Value checks, Json::Value provenance );

/// Validates a ResultAssessment document (envelope included).
std::vector<std::string> validateResultAssessment( const Json::Value &doc );

// ---------------------------------------------------------------------------
// Bounded-output helpers (agent safety contract).
// ---------------------------------------------------------------------------

/// Slices `items` into `{items, total, offset, next_offset}`; `next_offset` is
/// -1 on the terminal page (matches temporal:* paging contract).
Json::Value paginate( const Json::Value &items, int offset, int limit );

/// Hard cap for any single agent-facing JSON payload produced by this module
/// family (bytes, serialized). Tools may choose smaller caps, never larger.
inline constexpr size_t kMaxToolOutputBytes = 512 * 1024;

/// Serialized size of a JSON document in bytes (compact form).
size_t serializedSize( const Json::Value &doc );

} // namespace sicnu::agent::contracts
