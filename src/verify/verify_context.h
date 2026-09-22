// src/verify/verify_context.h
#pragma once

//
// Unified Scientific Verifier (ADR 0172) — provider seam.
//
// The engine never touches the filesystem, GDAL or QGIS: every fact it
// judges arrives through one of the small probe interfaces below. This is
// what keeps the verifier core headless-testable (fake providers drive the
// TDD) and keeps real adapters OUTSIDE the leaf (future adapters: harness
// verifyArtifact, exp-rs-prov sidecars, workflow NodeStatusSnapshot,
// ReplayReadiness — see docs/integration.md).
//
// A null provider is a DECLARED capability gap: any check that needs it
// evaluates to Indeterminate (verify:i_provider_missing) — never skipped,
// never guessed.
//

#include <json/json.h>

#include <cstdint>
#include <optional>
#include <string>

namespace sicnu::verify
{

/// Everything the artifact-family checks need about one file.
struct ArtifactInfo
{
    bool exists = false;
    std::string kind;    ///< "raster"|"vector"|"table"|"json"|"sidecar", "" when unknown
    std::uint64_t sizeBytes = 0;
    std::string digest;  ///< lowercase hex sha256 over file bytes, "" when unavailable
};

/// Bounded grid facts (the probe implementation owns the sampling budget;
/// the engine never walks pixels).
struct GridInfo
{
    int width = 0;
    int height = 0;
    int bandCount = 0;
    std::string crs;
    double nodataFraction = 0.0;
    double finiteFraction = 0.0;
};

class IArtifactProbe
{
  public:
    virtual ~IArtifactProbe() = default;
    /// nullopt = the probe cannot answer for this path (unreadable/foreign).
    virtual std::optional<ArtifactInfo> probe( const std::string &path ) = 0;
    /// Parsed JSON document, or nullopt when the path is not readable JSON.
    virtual std::optional<Json::Value> readJson( const std::string &path ) = 0;
};

class IGridProbe
{
  public:
    virtual ~IGridProbe() = default;
    virtual std::optional<GridInfo> grid( const std::string &path ) = 0;
};

/// Execution-state snapshot (key -> JSON value) the task/node recorded.
class IStateView
{
  public:
    virtual ~IStateView() = default;
    virtual std::optional<Json::Value> state( const std::string &key ) = 0;
};

/// Provenance documents by artifact path or run id (the exp-rs-prov/1 and
/// DerivationRecord surfaces are the future real adapters).
class IProvenanceView
{
  public:
    virtual ~IProvenanceView() = default;
    virtual std::optional<Json::Value> provenanceForPath( const std::string &path ) = 0;
    virtual std::optional<Json::Value> provenanceForRun( const std::string &runId ) = 0;
};

class IMetricView
{
  public:
    virtual ~IMetricView() = default;
    /// nullopt = the metric is not recorded (verify:i_metric_missing).
    virtual std::optional<double> metric( const std::string &name ) = 0;
};

struct VerificationContext
{
    IArtifactProbe *artifactProbe = nullptr;
    IGridProbe *gridProbe = nullptr;
    IStateView *stateView = nullptr;
    IProvenanceView *provenanceView = nullptr;
    IMetricView *metricView = nullptr;
};

} // namespace sicnu::verify
