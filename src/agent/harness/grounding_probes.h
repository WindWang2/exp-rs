// src/agent/harness/grounding_probes.h
#pragma once

//
// Compiler & Grounding 11.0: bounded factual probes over the EXISTING
// grounding seam.
//
// A probe answers "what are the facts about THIS dataset" for a CLOSED set
// of fact scopes — nothing more. It is the budget-shaped cousin of
// spatial:understand: the probe never opens a dataset itself; it delegates
// to the same spatial:understand tool (one implementation, one cache) and
// then projects the requested scopes. Budgets are DECLARED and CHECKED —
// the probe reports honest elapsed/deadline accounting instead of pretending
// it can interrupt a synchronous GDAL open (a hard interrupt would need an
// async tool; that is a recorded follow-up, not a silent claim).
//
// Anti-hallucination: references resolve through the ONE resolver
// (entity_resolver); unknown scopes are typed errors BEFORE any I/O; a
// scope an understanding document cannot answer stays "unknown" in
// fact_status — never a fabricated value.
//
// Model manifests are probed from the context ledger (the contract the
// harness observed) plus a stat-only check of the declared artifact path —
// file PRESENCE and SIZE, never a content read.
//

#include <json/json.h>
#include <QString>
#include <string>
#include <vector>

namespace sicnu::agent::harness {

/// The one probe budget/bounds table (mirrors IrLimits discipline).
struct ProbeLimits
{
    static constexpr int kDefaultDeadlineMs = 2000;  ///< per-probe wall budget
    static constexpr int kMaxScopes = 16;            ///< max scopes per request
    static constexpr int kMaxScopesInFacts = 32;     ///< max keys a probe projects
    static constexpr long long kLogicalByteBudget = 4 * 1024 * 1024; ///< metadata envelope budget (declared; probes never read pixels)
};

/// Machine-readable mirror of ProbeLimits (drift anchor).
Json::Value probeLimits();

/// Closed fact scopes. Each maps to a bounded projection of a
/// DatasetUnderstanding document (see grounding_probes.cpp for the mapping).
namespace probe_scope {
inline constexpr const char *kIdentity = "identity";         ///< path/driver/source_kind/entity
inline constexpr const char *kGrid = "grid";                 ///< size + pixel size
inline constexpr const char *kCrs = "crs";                   ///< CRS (authid/wkt)
inline constexpr const char *kExtent = "extent";             ///< bounding box
inline constexpr const char *kBands = "bands";               ///< band roles/count/wavelengths
inline constexpr const char *kTemporal = "temporal";         ///< acquisition time(s) + cadence facts
inline constexpr const char *kNodata = "nodata";             ///< per-band sentinels
inline constexpr const char *kQualityMasks = "quality_masks";
inline constexpr const char *kRadiometric = "radiometric";   ///< radiometric state / calibration
inline constexpr const char *kModality = "modality";
inline constexpr const char *kProduct = "product";           ///< product type/level/sensor metadata
bool isKnownProbeScope( const std::string &scope );
/// Every scope in table order (surfaces derive from THIS, never a copy).
std::vector<std::string> allProbeScopes();
} // namespace probe_scope

struct ProbeRequest
{
    std::string ref;              ///< any resolvable dataset reference
    std::vector<std::string> scopes;
    int deadlineMs = 0;           ///< 0 = ProbeLimits::kDefaultDeadlineMs
};

struct ProbeOutcome
{
    bool ok = false;
    std::string code;             ///< typed error code ("" when ok)
    std::string summary;          ///< human-readable one-liner ("" when ok)
    std::string source;           ///< "cache" | "live" ("" on failure)
    long long elapsedMs = 0;      ///< honest wall time of the grounding call
    bool deadlineExceeded = false; ///< the work completed but over budget
    Json::Value facts{Json::objectValue};   ///< scoped facts, per-key fact_status preserved
    Json::Value entity{Json::objectValue};  ///< resolved identity (when resolved)

    Json::Value toJson() const;
};

/// Bounded fact probe for a dataset. Deterministic given (ref, scopes, cache
/// state); the elapsed/deadline accounting is the only varying part and is
/// reported, never hidden.
ProbeOutcome probeDatasetFacts( const ProbeRequest &request );

struct ModelProbeOutcome
{
    bool ok = false;
    std::string code;
    std::string summary;
    Json::Value contract{Json::objectValue};  ///< ledger contract (when observed)
    bool artifactPresent = false;             ///< stat-only check of artifact_path
    long long artifactBytes = -1;             ///< -1 = unknown (no artifact path)
    Json::Value toJson() const;
};

/// Bounded model-manifest probe: the harness-observed contract from the
/// context ledger, plus presence/size of the declared artifact (stat only).
/// Unknown model ids are typed-unknown — never a guessed contract.
ModelProbeOutcome probeModelManifest( const std::string &modelId );

/// Registers the agent-facing probe surfaces (`harness:probe_facts`,
/// `harness:probe_model`). Idempotent.
void registerGroundingProbeTools();

} // namespace sicnu::agent::harness
