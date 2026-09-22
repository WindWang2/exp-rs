// grader_error.h — typed errors for the process-aware experiment grader.
//
// Part of the grader leaf library (src/grader): pure C++20 + jsoncpp, zero
// Qt/GDAL/sicnu dependencies. See docs/adr/0174-process-aware-experiment-grader.md.
//
// Two closed vocabularies, deliberately distinct (ADR 0174 §error-model):
//   * Document-level refusals use GraderErrorCode with machine-readable codes
//     spelled `grader:e-*`. A refused rubric/evidence document produces NO
//     report — the grader fails closed instead of silently grading garbage.
//   * Per-criterion outcome reasons use slugs spelled `grader:<slug>` and live
//     in the report (see grader_types.h). They explain lost/indeterminate
//     points; they are never document refusals.
//
// Reason codes and error codes are append-only: consumers may match on the
// spelled strings, so existing codes never change meaning.
#pragma once

#include <string>

namespace sicnu::grader {

enum class GraderErrorCode
{
    None = 0,
    /// Document carries a foreign or missing `schema` version.
    SchemaVersionUnsupported,
    /// Document is structurally invalid (missing/unknown members, bad types,
    /// duplicate ids, weight-sum mismatch, dangling references, bad budgets).
    SchemaShapeInvalid,
    /// Input is not parseable JSON at all.
    InvalidJson,
    /// Internal invariant violation — a bug in the grader itself, never an
    /// input problem. Callers should treat this as a defect report.
    Internal,
};

struct GraderError
{
    GraderErrorCode code = GraderErrorCode::None;
    std::string message;
    /// Dotted path to the offending member (e.g. "dimensions[2].criteria[0].maxPoints").
    std::string path;

    bool ok() const { return code == GraderErrorCode::None; }
};

/// Machine-readable code string for a GraderErrorCode ("grader:e-*" family).
/// Returns "grader:e-internal" for None-carrying default errors other than the
/// explicit ok() case; callers must check ok() first.
std::string toCodeString( GraderErrorCode code );

inline GraderError makeError( GraderErrorCode code, std::string message, std::string path = {} )
{
    GraderError e;
    e.code = code;
    e.message = std::move( message );
    e.path = std::move( path );
    return e;
}

} // namespace sicnu::grader
