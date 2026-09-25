// src/verify_adapters/provenance_sidecar_view.h — real IProvenanceView over
// the two provenance records this platform actually writes (ADR 0172
// provider seam, verify_context.h):
//
//   per product:  <path>.prov.json          — "exp-rs-prov/1" sidecar
//                 (src/operators/runtime/provenance_verify consumer family)
//   per run:      provenance_<runId>.json   — "d17_provenance" lineage graph
//                 (src/workflow/workflow_provenance writer)
//
// The view PROJECTS records; it never judges and never repairs. Missing /
// unreadable / foreign-envelope records are classified by the typed readers
// below, and the view feeds the engine only a document it fully trusts the
// envelope of: provenanceForPath returns nullopt for a sidecar whose schema
// marker is not exp-rs-prov/1, so a version mismatch reads as "no usable
// provenance" (Fail) at the engine — never as a false completion Pass. The
// typed readers are the surfaces that need to NAME the difference.
#pragma once

#include "verify/verify_context.h"

#include <json/json.h>

#include <optional>
#include <string>
#include <vector>

namespace sicnu::verify_adapters
{

/// Wire schema this view trusts on <path>.prov.json.
inline constexpr const char *kProvSidecarSchema = "exp-rs-prov/1";
/// Envelope of provenance_<runId>.json (workflow_provenance writer).
inline constexpr const char *kRunProvenanceKind = "d17_provenance";
inline constexpr const char *kRunProvenanceVersion = "1.0";

/// The sidecar path convention: <productPath> + ".prov.json".
std::string provenanceSidecarPathFor( const std::string &productPath );

enum class ProvenanceReadStatus
{
    Ok,             ///< record present, parsed, envelope matches
    Missing,        ///< no record at the expected location
    Unreadable,     ///< record present but not parseable JSON (tamper bait)
    ForeignEnvelope ///< parseable but not the schema/version this view trusts
};

struct ProvenanceReadResult
{
    ProvenanceReadStatus status = ProvenanceReadStatus::Missing;
    Json::Value document;   ///< parsed record when status == Ok
    std::string detail;     ///< human-readable evidence for non-Ok statuses
};

/// Typed read of one product sidecar. Never throws.
ProvenanceReadResult readProvenanceSidecar( const std::string &productPath );

/// Typed read of one run lineage graph. @p runDirectories are searched in
/// order for provenance_<runId>.json (the run directory convention).
ProvenanceReadResult readRunProvenance( const std::string &runId,
                                        const std::vector<std::string> &runDirectories );

class SidecarProvenanceView final : public sicnu::verify::IProvenanceView
{
  public:
    /// Directories searched for provenance_<runId>.json, in order.
    explicit SidecarProvenanceView( std::vector<std::string> runDirectories = { "." } );

    std::optional<Json::Value> provenanceForPath( const std::string &path ) override;
    std::optional<Json::Value> provenanceForRun( const std::string &runId ) override;

  private:
    std::vector<std::string> mRunDirectories;
};

} // namespace sicnu::verify_adapters
