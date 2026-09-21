/***************************************************************************
 * explain_provenance.h — fact provenance taxonomy for the explain layer
 *
 * Every explanation field carries one of three provenance classes:
 *
 *   system_fact      — machine-verifiable, produced only by a source
 *                      adapter / the builder. Must cite at least one
 *                      machine-kind evidence link.
 *   authored_guidance— human-written curriculum content loaded from the
 *                      guidance store. Never presented as a runtime fact.
 *   inferred         — composed narrative derived from cited facts; must
 *                      cite the facts it derives from.
 *
 * The taxonomy is enforced at three gates: builder construction (by
 * design), strict JSON parsing (against hand-forged documents) and the
 * explanation validator (hallucination guard).
 ***************************************************************************/
#pragma once

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::explain
{

enum class FactProvenance
{
  SystemFact,
  AuthoredGuidance,
  InferredExplanation,
};

const char *factProvenanceToString( FactProvenance provenance );
bool parseFactProvenance( const std::string &text, FactProvenance &out );

// Closed machine-evidence kinds. Only these count as grounding for
// SystemFact content. "guidance" is an authored self-reference and never
// machine evidence.
constexpr const char *EvidenceOperatorSchema = "operator_schema";
constexpr const char *EvidenceWorkflowDocument = "workflow_document";
constexpr const char *EvidenceDerivation = "derivation";
constexpr const char *EvidenceProvenance = "provenance";
constexpr const char *EvidenceOperationLog = "operation_log";
constexpr const char *EvidenceContract = "contract";
constexpr const char *EvidenceGuidance = "guidance";

bool isKnownEvidenceKind( const std::string &kind );
bool isMachineEvidenceKind( const std::string &kind );

// Grammar per kind (no whitespace anywhere):
//   operator_schema   -> "operator:<id>"
//   contract          -> "contract:<id>"
//   guidance          -> "guidance:<id>"
//   operation_log     -> "operation_log:<id>"
//   workflow_document -> "workflow:<workflowId>#<nodeOrStepId>"
//   derivation        -> "derivation:<assetId>#<stepId>"
//   provenance        -> "provenance:<runId>#node:<nodeId>" | "provenance:<runId>#run"
bool isValidEvidenceTarget( const std::string &kind, const std::string &target );

struct EvidenceLink
{
  std::string kind;
  std::string target;
  std::string note;

  Json::Value toJson() const;
  // Returns false and fills @p error on unknown kind / malformed target.
  bool fromJson( const Json::Value &value, std::string &error );
  bool isValid() const;
};

// Closed workflow kinds (v1). Growth requires a schema version bump.
constexpr const char *WorkflowKindD17Designer = "d17_designer";
constexpr const char *WorkflowKindGuidedPipeline = "guided_pipeline";
constexpr const char *WorkflowKindLab = "lab";
constexpr const char *WorkflowKindAgentPlan = "agent_plan";
bool isKnownWorkflowKind( const std::string &kind );

// Closed step kinds, mirroring Workflow Engine 2.0 StepDef kinds.
constexpr const char *StepKindOperator = "operator";
constexpr const char *StepKindInteractive = "interactive";
constexpr const char *StepKindReview = "review";
constexpr const char *StepKindComposite = "composite";
bool isKnownStepKind( const std::string &kind );

// Closed state-change aspects (v1). These are the aspects the builder can
// synthesize from projected port facts; everything else stays out of the
// schema rather than being invented.
constexpr const char *AspectRadiometricState = "radiometric_state";
constexpr const char *AspectCrs = "crs";
constexpr const char *AspectResolution = "resolution";
constexpr const char *AspectBandCount = "band_count";
bool isKnownStateAspect( const std::string &aspect );

// Closed source-reference kinds (citations never fetched at runtime).
constexpr const char *ReferenceKindTextbook = "textbook";
constexpr const char *ReferenceKindPaper = "paper";
constexpr const char *ReferenceKindStandard = "standard";
constexpr const char *ReferenceKindDoc = "doc";
bool isKnownReferenceKind( const std::string &kind );

bool containsWhitespace( const std::string &text );

} // namespace sicnu::explain
