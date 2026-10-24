// src/verify_adapters/lab_verify_composer.h — R21-E (#1601): composed
// scientific verification for ONE teaching lab artifact.
//
// The ADR 0172 authority (src/verify, target sicnu_verifier) was wired only
// into test lanes: no production TU linked it, so the "one verification
// truth" never reached a student or teacher report. This composer is the
// production bridge on the adapter side: it builds the default teaching
// spec for an artifact, attaches the fs + GDAL probes and runs the authority
// engine. The consumer (the OutputVerifier teaching grader) folds the
// returned evidence document into LabGradeResult — the verdict lattice stays
// the grader's own; the verifier evidence is ADDITIVE, sealed and
// three-valued, so "could not verify" can never read as a pass.
//
// Qt-free on purpose (the adapters' leaf contract): plain C++20 + jsoncpp +
// the probes this directory already owns.

#pragma once

#include <json/json.h>

#include <string>

namespace sicnu::verify_adapters
{

/// Grid pins re-checked independently by the authority engine. Members are
/// derived from the lab rules' own declared grid contract (the crs_grid
/// assertion); a zero / empty member means "not pinned" and is omitted from
/// the spec — the engine rejects vacuous checks, so unpinned axes are simply
/// not asserted, never asserted vacuously.
struct LabVerifyGridPins
{
    int width = 0;
    int height = 0;
    int bandCount = 0;
    std::string crs; ///< authority form "EPSG:<code>" (the probe's authid)

    bool any() const { return width > 0 || height > 0 || bandCount > 0 || !crs.empty(); }
};

/// One composed verification outcome.
struct LabVerifyOutcome
{
    /// false only when composition was not requested (empty artifact path):
    /// the caller then attaches nothing. A composed run always produces a
    /// document — composition-internal failures surface as status
    /// "indeterminate" with the reason inside, never as a missing evidence
    /// block and never as "pass".
    bool ran = false;
    std::string statusWord; ///< "pass" | "fail" | "indeterminate"
    /// sicnu.verifier.evidence/1 document: {schema, status, spec_digest,
    /// counts, checks[]} — checks[] in spec declaration order, each carrying
    /// the authority's evidence (observed/expected, sampling stamps).
    Json::Value report;
};

/// Verify @p artifactPath (declared @p artifactKind — "raster" or "vector";
/// empty skips the kind check) through the authority engine with the fs
/// artifact probe and the GDAL grid probe attached, re-checking @p pins when
/// the lab rules declared a grid contract.
///
/// Deterministic: identical (path, artifact, kind, pins) inputs compose
/// byte-identical evidence (no clock, no randomness — the engine contract).
/// Scope notes (R2 review):
///   * spec_digest seals the spec INCLUDING the artifact path, so the same
///     bytes submitted from a different directory seal differently — the
///     path is part of what the checks assert, and the grade body already
///     carries the artifact path for the same reason. Byte-identity holds
///     for identical inputs on any machine.
///   * the grid re-check pins width/height/bandCount/crs only — the rules'
///     pixel_size_* constraints stay the grader's own assertion (the
///     authority's GridInfo carries no geotransform). When the rules declare
///     MORE than one grid contract, the FIRST one is pinned.
///   * file-mode labs (artifactKind empty, no pins) compose an
///     artifact.exists-only spec: a "pass" there asserts the file is
///     present, nothing stronger — the evidence document says so by
///     carrying exactly that one check.
LabVerifyOutcome composeLabVerifyEvidence( const std::string &artifactPath,
                                           const std::string &artifactKind,
                                           const LabVerifyGridPins &pins = LabVerifyGridPins() );

} // namespace sicnu::verify_adapters
