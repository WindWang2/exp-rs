// src/agent/harness/provenance_projection.h
#pragma once

//
// Compiler & Grounding 11.0: execution provenance projection.
//
// The compiler's decisions (fingerprint, facts, repairs, refusals, checks)
// become STABLE METADATA for the execution plane: one canonical block that
// rides the workflow document's root `metadata` key (the seam
// compilePlanToWorkflowJson already owns — engine parsers ignore unknown
// root keys, verified in ADR 0149) plus an atomic `<output>.compile.json`
// sidecar for run artifacts.
//
// Layering: the projection is a pure function of (IR, analysis, repairs,
// refusals). It never touches the execution plane's files (src/workflow/**
// stays untouched), never mutates engine-owned metadata keys, and its own
// writes are QSaveFile-atomic beside the artifact with a typed failure that
// never corrupts the artifact.
//
// Determinism: same compile inputs -> byte-identical projection (jsoncpp's
// sorted member iteration; canonical compact serialization for the digest).
//

#include <json/json.h>
#include <string>
#include <vector>

#include "workflow_analysis.h"
#include "workflow_ir.h"

namespace QSaveFileHelper {
}

namespace sicnu::agent::harness::projection {

inline constexpr const char *kProjectionSchemaVersion = "1.0";
/// The workflow metadata key the projection rides (additive, engine-ignored).
inline constexpr const char *kCompilerMetadataKey = "compiler";

/// Projection bounds (mirrors IrLimits discipline; drift-pinned by tests).
struct ProjectionLimits
{
    static constexpr int kMaxTextChars = 512;   ///< per carried string (clamped, counted)
    static constexpr int kMaxChecksEchoed = 64; ///< checks ledger rows echoed
    static constexpr int kMaxRepairs = 64;
    static constexpr int kMaxRefusals = 64;
};

/// Machine-readable mirror of ProjectionLimits.
Json::Value projectionLimits();

/// Builds the canonical compiler projection block. All inputs are already
/// validated types (IR reader / analysis / repair outcome); malformed JSON
/// inside them degrades to clamped empties, never a throw.
Json::Value compilerProjection( const WorkflowIr &ir, const IrAnalysis &analysis,
                                const std::vector<IrRepairRecord> &repairs,
                                const std::vector<IrRefusal> &refusals );

/// SHA-256 (first 16 hex) over the canonical compact serialization — the
/// stable identity a run record cites.
std::string projectionDigest( const Json::Value &projection );

/// Attaches the projection to a workflow definition document under
/// `metadata.compiler` WITHOUT overwriting engine-owned keys: an existing
/// `metadata.compiler` from a different digest is preserved under
/// `metadata.compiler_superseded` (bounded to one level). Returns the updated
/// document (jsoncpp copy).
Json::Value attachToWorkflowJson( const Json::Value &workflowDef,
                                  const Json::Value &projection );

/// Writes `<output>.compile.json` beside an artifact (QSaveFile atomic).
/// A failed write never touches the artifact and is reported honestly.
struct SidecarResult
{
    std::string path;
    bool written = false;
    std::string error;
};

SidecarResult writeCompileSidecar( const std::string &outputPath,
                                   const Json::Value &projection );

} // namespace sicnu::agent::harness::projection
