// src/agent/lab_grader_kernels.h — grader 2.0 assertion kernels (ADR 0150).
//
// The kernels here extend the statistical grader with the assertion families
// the labspec labs (temporal / SAR / hyperspectral / cartographic) need, and
// close the known ADR-0150 limit that grading is "statistics, not position":
//
//   zone_stats          per-zone(/band) statistics joined by an aux zone raster
//                       — mean/median/ENL bounds, Fisher separability, cross-
//                       zone deltas, ENL ratio vs a reference raster;
//   band_layout         band count, per-band valid-pixel fractions, exact
//                       per-band valid counts (stack integrity, NoData holes);
//   spatial_agreement   POSITION-SENSITIVE agreement of the artifact with a
//                       truth raster: per-zone label accuracy, binary hit /
//                       false-alarm rates, or continuous |diff| tolerance;
//   series_separation   temporal/ordered series: per-zone slope over a
//                       declared x axis plus cross-zone separation;
//   spectral_signature  SAM spectral angle of pixel spectra against declared
//                       reference spectra, optionally zone-anchored;
//   file_check          non-raster artifacts (rules artifact.kind == "file"):
//                       existence, byte bounds, PNG page geometry, MapSpec
//                       validation via the platform validator.
//
// Contracts inherited from the D4 grader (ADR 0150):
//   * every pass streams in bounded windows under the caller's byte budget —
//     no kernel materializes the whole raster;
//   * a graded failure is EVIDENCE ({observed, expected, delta}), never a
//     bare boolean; the caller rounds and embeds it in the transcript;
//   * results are pure functions of (artifact, rules, truths) — no wall
//     clock, no RNG — so transcripts stay byte-identical for equal inputs;
//   * grid-incompatible truth rasters are GRADED failures, not crashes
//     (same policy as classification_kappa's truth walk).
#pragma once

#include <json/json.h>

#include <QString>

#include <map>
#include <vector>

namespace sicnu::geo
{
class RasterReader;
}

namespace sicnu::agent {

/// One assertion scheduled into a kernel walk (id + params are all a kernel
/// needs; severity/weight stay with the caller's rules copy).
struct LabKernelSpec
{
    QString id;
    QString kind;
    Json::Value params;
};

/// Graded outcome of one kernel assertion — same shape as the D4 grader's
/// FinalOutcome (passed / observed / expected / delta / message).
struct LabKernelOutcome
{
    bool passed = false;
    bool hasDelta = false;
    double delta = 0.0;
    QString message;
    Json::Value observed;
    Json::Value expected;
};

/// Raster-mode kernels owned by this module (artifact must open as a raster).
bool isLabRasterKernelKind( const QString &kind );

/// File-mode kernels owned by this module (rules artifact.kind == "file").
bool isLabFileKernelKind( const QString &kind );

/// True when the rules' artifact kind mixes raster and file kernels — always
/// a usage error (one artifact, one mode).
bool labKernelKindsMatchArtifactMode( const QString &artifactKind, const QString &kind );

/// Per-kind params validation (usage errors; accumulates the FIRST error).
bool validateLabKernelParams( const QString &kind, const Json::Value &params, QString *error );

/// Runs every raster-mode spec against @p reader and fills @p outcomes by
/// assertion id. @p rulesDir resolves truth/reference paths relative to the
/// rules file (same policy as the classification truth walk). Returns false
/// for artifact-level I/O failures (typed via @p error) or usage-class
/// failures (invalid params/paths — @p usageClass then distinguishes the
/// exit class); graded failures land in outcomes as passed == false.
bool runLabKernelWalks( const sicnu::geo::RasterReader &reader,
                        const std::vector<LabKernelSpec> &specs, const QString &rulesDir,
                        std::size_t maxBytes, std::map<QString, LabKernelOutcome> &outcomes,
                        bool *usageClass, QString *error );

/// Runs file-mode specs against @p artifactPath (the submitted file; sibling
/// files are addressed by paths relative to its directory). Same contract as
/// runLabKernelWalks; @p usageError is set for invalid params/paths.
bool runLabFileChecks( const QString &artifactPath, const std::vector<LabKernelSpec> &specs,
                       const QString &rulesDir, std::map<QString, LabKernelOutcome> &outcomes,
                       QString *usageError );

} // namespace sicnu::agent
