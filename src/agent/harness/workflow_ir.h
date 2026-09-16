// src/agent/harness/workflow_ir.h
#pragma once

//
// Scientific Workflow Compiler 10.0 (ADR 0149): the typed WorkflowIR.
//
// A versioned, bounded, serializable document (`kind: "workflow_ir"`) whose
// nodes carry operator ids, typed ports, and ARTIFACT FACTS — the physical /
// semantic facts a compiler reasons about before anything executes:
// artifact kind, CRS, grid, band roles, wavelengths, numeric domain, temporal
// domain, modality/sensor, determinism grade, resource/device estimates,
// provenance expectations, and the user-visible semantic output.
//
// Layering (the one non-negotiable): the IR is a harness-side typed layer
// ABOVE AgentPlan v2. It never executes and never opens datasets; facts enter
// through the grounding seam (resolveDatasetRef -> DatasetUnderstanding) and
// the capability knowledge layer. Lowering goes IR -> AgentPlan v2 ->
// compilePlanToWorkflowJson. An IR that cannot be lowered is a compiler bug.
//
// Fact provenance: every artifact fact is staged in a closed vocabulary
//   "observed"   — read from a DatasetUnderstanding document (strongest)
//   "declared"   — written by the agent / recipe in the IR document
//   "derived"    — from the capability knowledge output contract
//   "assumed"    — heuristic (e.g. modality inference)
//   "unknown"    — absent; checks degrade to warnings, never fake pass/fail
// (mirrors the harness 8.0/9.0 fact_status discipline).
//
// Bounds: every limit lives in irLimits() — one table, tests and docs derive
// from it. The reader is fail-closed: an out-of-bounds or malformed document
// is rejected with a typed error, never clipped silently.
//

#include <json/json.h>
#include <string>
#include <vector>

#include "agent_plan.h"
#include "harness_error.h"

namespace sicnu::agent::harness {

inline constexpr const char *kWorkflowIrSchemaVersion = "1.0";
inline constexpr const char *kWorkflowIrKind = "workflow_ir";

/// Closed artifact-fact vocabulary. Values are the wire strings; never rename.
namespace artifact_facts {
/// Artifact kind.
inline constexpr const char *kKindRaster = "raster";
inline constexpr const char *kKindVector = "vector";
inline constexpr const char *kKindTable = "table";
inline constexpr const char *kKindModel = "model";
inline constexpr const char *kKindStructured = "structured";

/// Numeric domain of a raster's pixel values (radiometric/scaling semantics).
inline constexpr const char *kDomainSurfaceReflectance = "surface_reflectance";
inline constexpr const char *kDomainToa = "toa";
inline constexpr const char *kDomainDn = "dn";
inline constexpr const char *kDomainDb = "db";               ///< decibels (log scale)
inline constexpr const char *kDomainLinearPower = "linear_power";
inline constexpr const char *kDomainIndex = "index";          ///< spectral index output
inline constexpr const char *kDomainCategorical = "categorical";
inline constexpr const char *kDomainMasked = "masked";        ///< mask: 0 = clear, else value
inline constexpr const char *kDomainUnknown = "unknown";

/// Modality.
inline constexpr const char *kModalityOptical = "optical";
inline constexpr const char *kModalitySar = "sar";
inline constexpr const char *kModalityDem = "dem";
inline constexpr const char *kModalityUnknown = "unknown";

/// Determinism expectation for a node (mirrors the ADR 0124 grades).
inline constexpr const char *kDeterminismBitExact = "bit_exact";
inline constexpr const char *kDeterminismTolerance = "tolerance";
inline constexpr const char *kDeterminismStochastic = "stochastic";
} // namespace artifact_facts

/// True when `domain` is part of the closed numeric-domain vocabulary.
bool isKnownNumericDomain( const std::string &domain );
/// True when `kind` is part of the closed artifact-kind vocabulary.
bool isKnownArtifactKind( const std::string &kind );
/// True for domains on a linear radiometric scale (no log compression).
bool isLinearReflectiveDomain( const std::string &domain );

/// The one bounds table. Enforced by readWorkflowIr; mirrored in docs/tests.
/// Note: `params`/`expectations` JSON is carried as parsed by the caller —
/// the reader adds no second depth/size bound beyond the incoming document
/// it already holds (documented; review B-9).
struct IrLimits
{
    static constexpr int kMaxNodes = 64;
    static constexpr int kMaxInputsPerNode = 8;
    static constexpr int kMaxOutputsPerNode = 8;
    static constexpr int kMaxDeclaredOutputs = 32;
    static constexpr int kMaxDocumentInputs = 16;
    static constexpr size_t kMaxIdChars = 64;
    static constexpr size_t kMaxTextChars = 512;
    static constexpr size_t kMaxCrsChars = 256;
};

/// Machine-readable mirror of IrLimits (drift anchor for docs/tests).
Json::Value irLimits();

/// One typed port of a node: a name plus the artifact facts the node declares
/// for what flows through it.
struct IrPort
{
    std::string name;                 ///< port name, e.g. "output"
    Json::Value artifact{Json::objectValue}; ///< closed-key artifact facts
};

/// One wiring edge: either from an upstream node port ({node, output}) or
/// from a document input slot ({input}). Exactly one form per edge.
struct IrNodeInput
{
    std::string node;    ///< upstream node id (node form)
    std::string output;  ///< upstream port name, default "output"
    std::string input;   ///< document input slot name (slot form)
    std::string as;      ///< local port this edge feeds, default "input"
};

struct IrNode
{
    std::string id;
    std::string operatorId;
    Json::Value params{Json::objectValue};
    std::vector<IrNodeInput> inputs;
    std::vector<IrPort> outputs;
    std::string verification;      ///< "" | "raster" | "vector" | "skip"
    long long resourceEstimateMb = 0;
    std::string device;            ///< "" | "cpu" | "gpu"
    std::string determinism;       ///< "" | bit_exact | tolerance | stochastic
    std::string semanticOutput;    ///< user-visible meaning of the node's product
    std::string source;            ///< "agent" | "recipe:<id>" | "compose_chain" | "repair:<rule>"
};

/// One document-level dataset input slot: a named reference resolved at the
/// grounding stage (never a path baked into analysis).
struct IrInputSlot
{
    std::string name;
    std::string reference;         ///< asset-N id / uuid / path / display name
    Json::Value artifact{Json::objectValue}; ///< optional declared facts
};

struct IrOutputDecl
{
    std::string name;
    std::string node;              ///< producing node id
    std::string port;              ///< producing port, default "output"
    std::string kind;              ///< artifact kind for display ("" = unset)
    std::string path;              ///< optional declared output path
    std::string semantic;          ///< user-visible meaning
};

/// The compiled-form IR. `raw` keeps the original document; `toJson()` emits
/// the canonical (normalized) document.
struct WorkflowIr
{
    std::string irId;              ///< "wir-<hex>"; generated deterministically when absent
    std::string schemaVersion = kWorkflowIrSchemaVersion;
    std::string goal;
    std::string intent;            ///< closed vocabulary or "" (custom)
    std::vector<IrInputSlot> inputs;
    std::vector<IrNode> nodes;
    std::vector<IrOutputDecl> outputs;
    Json::Value expectations{Json::objectValue}; ///< {max_ram_mb, deterministic, device, output_dir}
    Json::Value raw{Json::objectValue};
};

/// Evidence of one auto-inserted repair (written by workflow_repair).
struct IrRepairRecord
{
    std::string ruleId;
    std::string issueCode;
    std::string insertedNode;
    std::string risk;              ///< "shape_preserving" | "radiometric"
    Json::Value factsUsed{Json::objectValue};
    Json::Value params{Json::objectValue}; ///< parameters the inserted node carries (11.0)
    Json::Value toJson() const;
};

/// Evidence of one refused repair — a science-changing transform the facts
/// cannot justify. `decisionRequired` is always true: refusing is not a no.
struct IrRefusal
{
    std::string ruleId;
    std::string issueCode;
    std::string why;
    Json::Value missingFacts{Json::arrayValue};
    bool decisionRequired = true;
    Json::Value toJson() const;
};

/// Reads + validates an IR document (fail-closed; typed errors; never throws).
/// Envelope: {kind: "workflow_ir", schema_version: "1.0", ir_id?, goal?,
/// intent?, inputs[], nodes[], outputs[], expectations?, artifacts?}.
bool readWorkflowIr( const Json::Value &doc, WorkflowIr &ir, HarnessError &error );

/// Structural self-consistency of an ALREADY-READ IR: unique node ids, wiring
/// references resolve (upstream node + port / declared slot), declared outputs
/// reference existing nodes/ports, no cycles. Issues carry typed codes.
std::vector<AgentPlanIssue> validateIrStructure( const WorkflowIr &ir );

/// Deterministic normalize (in place): nodes in stable topological order
/// (independents keep document order), edge lists sorted, params kept as-is
/// (jsoncpp object members already iterate sorted). Idempotent.
void normalizeWorkflowIr( WorkflowIr &ir );

/// Canonical compact serialization of the normalized IR's scientific content
/// (excludes ir_id/expectations bookkeeping), SHA-256 first 16 hex — the same
/// discipline as planFingerprint. Identical science -> identical bytes.
std::string workflowIrFingerprint( const WorkflowIr &ir );

/// Emits the canonical IR document (round-trips through readWorkflowIr).
Json::Value workflowIrToJson( const WorkflowIr &ir );

/// Validates one closed-key artifact-facts object. Returns one problem string
/// per finding; empty = valid. Static so tests can validate facts directly.
std::vector<std::string> validateArtifactFacts( const Json::Value &facts );

/// Merges declared artifact facts with observed (DatasetUnderstanding-derived)
/// facts into a merged facts object plus a parallel fact_status object.
/// Conflict policy: observed wins for the same key, and the conflict is
/// returned as a typed FACT_CONFLICT detail (never a silent overwrite).
/// `conflicts` (optional) collects {key, declared, observed} entries.
Json::Value mergeArtifactFacts( const Json::Value &declared, const Json::Value &observed,
                                Json::Value &factStatus, Json::Value *conflicts = nullptr );

/// Derives the fact_status object for a merged facts object: every present
/// key is "observed" when it came from `observed`, else "declared".
Json::Value factStatusFor( const Json::Value &declared, const Json::Value &observed );

/// Deterministic ir_id for an IR whose document omitted one:
/// "wir-" + workflowIrFingerprint(ir) — content-addressed, stable.
std::string deriveIrId( const WorkflowIr &ir );

/// Normalized CRS authid from a facts document: reads `crs` (string or
/// {authid,wkt} object) AND `crs_authid`, case-folded to one spelling — the
/// ONE normalizer every CRS comparison uses (analysis + repair), so shape or
/// case differences can never fake a conflict or hide one.
std::string normalizedCrsAuthid( const Json::Value &facts );

/// The derived output path for a node (see the lowering contract):
/// <output_dir>/<ir_id>_<node_id><ext>, or the declared path when the node's
/// params/declared outputs already carry one. `outputDir` empty = no
/// derivation (callers decide what that means for their surface).
std::string derivedOutputPath( const WorkflowIr &ir, const IrNode &node,
                               const std::string &outputDir );

} // namespace sicnu::agent::harness
