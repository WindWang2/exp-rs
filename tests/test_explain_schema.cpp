/***************************************************************************
 * test_explain_schema.cpp — StepExplanation schema + fact provenance (Slice A)
 *
 * Pins the exp.step_explanation.v1 contract: byte-stable canonical JSON,
 * strict parsing (unknown keys / closed vocabularies) and the provenance
 * trust rules (SystemFact requires machine evidence; inferred requires
 * evidence; authored stays authored).
 ***************************************************************************/
#include <catch2/catch_test_macros.hpp>

#include "explain/explain_provenance.h"
#include "explain/step_explanation.h"

using namespace sicnu::explain;

namespace
{

EvidenceLink makeEvidenceLink( const std::string &kind, const std::string &target )
{
  EvidenceLink l;
  l.kind = kind;
  l.target = target;
  return l;
}

SourceReference reference( const std::string &title, const std::string &kind, const std::string &locator )
{
  SourceReference r;
  r.title = title;
  r.kind = kind;
  r.locator = locator;
  return r;
}

// Fully populated explanation touching every field.
StepExplanation sampleExplanation()
{
  StepExplanation e;
  e.workflowKind = WorkflowKindD17Designer;
  e.workflowId = "wf-ndvi-lab";
  e.stepId = "node_radiometric";
  e.stepTitle = "辐射定标";
  e.stepKind = StepKindOperator;
  e.operatorId = "rs:radiometric_calibration";
  e.operatorDisplayName = "Radiometric Calibration";

  GroundedText purpose;
  purpose.text = "将 DN 转换为 TOA 反射率";
  purpose.provenance = FactProvenance::SystemFact;
  purpose.evidence = { makeEvidenceLink( EvidenceOperatorSchema, "operator:rs:radiometric_calibration" ) };
  e.purpose = { purpose };

  GroundedText prereq;
  prereq.text = "必须先完成辐射定标，否则指数无物理意义";
  prereq.provenance = FactProvenance::AuthoredGuidance;
  e.prerequisites = { prereq };

  GroundedText assumption;
  assumption.text = "假设影像未云污染";
  assumption.provenance = FactProvenance::AuthoredGuidance;
  e.scientificAssumptions = { assumption };

  StateTransition transition;
  transition.aspect = AspectRadiometricState;
  transition.before = "DN";
  transition.after = "TOA";
  transition.explanation = "输出端口声明 TOA";
  transition.provenance = FactProvenance::InferredExplanation;
  transition.evidence = { makeEvidenceLink( EvidenceWorkflowDocument, "workflow:wf-ndvi-lab#node_radiometric" ) };
  e.stateChanges = { transition };

  ParameterRationale rationale;
  rationale.parameter = "scale";
  rationale.chosenValue = Json::Value( 0.0001 );
  rationale.rationale = "Landsat 8 Collection 2 缩放因子";
  rationale.misconfigurationConsequence = "反射率超出 [0,1]，指数失真";
  rationale.provenance = FactProvenance::AuthoredGuidance;
  e.parameterRationale = { rationale };

  SkippedStepConsequence skipped;
  skipped.summary = "跳过后 NDVI 值无物理意义";
  skipped.detail = "DN 直接参与比值计算，结果不可与反射率域阈值比较";
  skipped.downstreamRoles = { "index" };
  skipped.provenance = FactProvenance::AuthoredGuidance;
  e.skipConsequence = skipped;

  ExecutionFacts execution;
  execution.status = "Completed";
  execution.elapsedMs = 1234;
  execution.cacheHit = false;
  execution.artifactDigest = "sha256full:abc";
  execution.startedUtc = "2026-09-21T00:00:00Z";
  execution.endedUtc = "2026-09-21T00:00:01Z";
  execution.evidence = { makeEvidenceLink( EvidenceProvenance, "provenance:run-1#node:node_radiometric" ) };
  e.execution = execution;

  e.evidenceLinks = { makeEvidenceLink( EvidenceDerivation, "derivation:asset-7#node_radiometric" ) };
  e.sourceReferences = { reference( "Lillesand & Kiefer", ReferenceKindTextbook, "ch. 7" ) };
  e.trustNotes = { "assumptions 来自教学编写，非运行时验证" };
  return e;
}

bool parsesTo( const Json::Value &json, StepExplanation &out, std::string &error )
{
  return out.fromJson( json, error );
}

} // namespace

TEST_CASE( "fact provenance taxonomy round-trips through strings", "[explain][schema]" )
{
  REQUIRE( std::string( factProvenanceToString( FactProvenance::SystemFact ) ) == "system_fact" );
  REQUIRE( std::string( factProvenanceToString( FactProvenance::AuthoredGuidance ) ) == "authored_guidance" );
  REQUIRE( std::string( factProvenanceToString( FactProvenance::InferredExplanation ) ) == "inferred" );

  FactProvenance parsed = FactProvenance::InferredExplanation;
  REQUIRE( parseFactProvenance( "system_fact", parsed ) );
  REQUIRE( parsed == FactProvenance::SystemFact );
  REQUIRE( parseFactProvenance( "authored_guidance", parsed ) );
  REQUIRE( parsed == FactProvenance::AuthoredGuidance );
  REQUIRE( parseFactProvenance( "inferred", parsed ) );
  REQUIRE( parsed == FactProvenance::InferredExplanation );
  REQUIRE( !parseFactProvenance( "fact", parsed ) );
  REQUIRE( !parseFactProvenance( "", parsed ) );
}

TEST_CASE( "evidence kinds split into machine and authored classes", "[explain][schema]" )
{
  REQUIRE( isKnownEvidenceKind( EvidenceOperatorSchema ) );
  REQUIRE( isKnownEvidenceKind( EvidenceGuidance ) );
  REQUIRE( !isKnownEvidenceKind( "vibes" ) );

  REQUIRE( isMachineEvidenceKind( EvidenceOperatorSchema ) );
  REQUIRE( isMachineEvidenceKind( EvidenceWorkflowDocument ) );
  REQUIRE( isMachineEvidenceKind( EvidenceDerivation ) );
  REQUIRE( isMachineEvidenceKind( EvidenceProvenance ) );
  REQUIRE( isMachineEvidenceKind( EvidenceOperationLog ) );
  REQUIRE( isMachineEvidenceKind( EvidenceContract ) );
  REQUIRE( !isMachineEvidenceKind( EvidenceGuidance ) );
}

TEST_CASE( "evidence link target grammar is enforced per kind", "[explain][schema]" )
{
  REQUIRE( makeEvidenceLink( EvidenceOperatorSchema, "operator:rs:ndvi" ).isValid() );
  REQUIRE( makeEvidenceLink( EvidenceContract, "contract:rs:ndvi" ).isValid() );
  REQUIRE( makeEvidenceLink( EvidenceGuidance, "guidance:rs:ndvi" ).isValid() );
  REQUIRE( makeEvidenceLink( EvidenceOperationLog, "operation_log:rs:ndvi@2026-09-21T00:00:00Z" ).isValid() );
  REQUIRE( makeEvidenceLink( EvidenceWorkflowDocument, "workflow:wf-1#node_a" ).isValid() );
  REQUIRE( makeEvidenceLink( EvidenceDerivation, "derivation:asset-7#node_a" ).isValid() );
  REQUIRE( makeEvidenceLink( EvidenceProvenance, "provenance:run-1#node:node_a" ).isValid() );
  REQUIRE( makeEvidenceLink( EvidenceProvenance, "provenance:run-1#run" ).isValid() );

  // wrong-kind prefixes
  REQUIRE( !makeEvidenceLink( EvidenceOperatorSchema, "contract:rs:ndvi" ).isValid() );
  // missing targets
  REQUIRE( !makeEvidenceLink( EvidenceOperatorSchema, "operator:" ).isValid() );
  REQUIRE( !makeEvidenceLink( EvidenceOperatorSchema, "" ).isValid() );
  // whitespace anywhere in the target
  REQUIRE( !makeEvidenceLink( EvidenceOperatorSchema, "operator:rs:ndvi now" ).isValid() );
  // separator grammar for the '#' kinds
  REQUIRE( !makeEvidenceLink( EvidenceWorkflowDocument, "workflow:wf-1" ).isValid() );
  REQUIRE( !makeEvidenceLink( EvidenceWorkflowDocument, "workflow:wf-1#" ).isValid() );
  REQUIRE( !makeEvidenceLink( EvidenceWorkflowDocument, "workflow:#node_a" ).isValid() );
  REQUIRE( !makeEvidenceLink( EvidenceProvenance, "provenance:run-1#node:" ).isValid() );
  // unknown kind never validates
  REQUIRE( !makeEvidenceLink( "vibes", "vibes:1" ).isValid() );
}

TEST_CASE( "closed vocabularies pin workflow/step/aspect/reference kinds", "[explain][schema]" )
{
  REQUIRE( isKnownWorkflowKind( WorkflowKindD17Designer ) );
  REQUIRE( isKnownWorkflowKind( WorkflowKindGuidedPipeline ) );
  REQUIRE( isKnownWorkflowKind( WorkflowKindLab ) );
  REQUIRE( isKnownWorkflowKind( WorkflowKindAgentPlan ) );
  REQUIRE( !isKnownWorkflowKind( "mission" ) );

  REQUIRE( isKnownStepKind( StepKindOperator ) );
  REQUIRE( isKnownStepKind( StepKindInteractive ) );
  REQUIRE( isKnownStepKind( StepKindReview ) );
  REQUIRE( isKnownStepKind( StepKindComposite ) );
  REQUIRE( !isKnownStepKind( "task" ) );

  REQUIRE( isKnownStateAspect( AspectRadiometricState ) );
  REQUIRE( isKnownStateAspect( AspectCrs ) );
  REQUIRE( isKnownStateAspect( AspectResolution ) );
  REQUIRE( isKnownStateAspect( AspectBandCount ) );
  REQUIRE( !isKnownStateAspect( "mood" ) );

  REQUIRE( isKnownReferenceKind( ReferenceKindTextbook ) );
  REQUIRE( isKnownReferenceKind( ReferenceKindPaper ) );
  REQUIRE( isKnownReferenceKind( ReferenceKindStandard ) );
  REQUIRE( isKnownReferenceKind( ReferenceKindDoc ) );
  REQUIRE( !isKnownReferenceKind( "blog" ) );
}

TEST_CASE( "canonical serialization is byte-stable across rebuilds", "[explain][schema]" )
{
  const StepExplanation sample = sampleExplanation();
  const std::string first = stepExplanationToCanonicalJson( sample );
  const std::string second = stepExplanationToCanonicalJson( sample );
  REQUIRE( !first.empty() );
  REQUIRE( first == second );
  // Compact canonical form has no newlines.
  REQUIRE( first.find( '\n' ) == std::string::npos );
}

TEST_CASE( "round-trip preserves every field of a populated explanation", "[explain][schema]" )
{
  const StepExplanation sample = sampleExplanation();
  const Json::Value json = sample.toJson();

  StepExplanation parsed;
  std::string error;
  REQUIRE( parsesTo( json, parsed, error ) );
  REQUIRE( error.empty() );

  REQUIRE( parsed.schemaVersion == StepExplanationSchemaV1 );
  REQUIRE( parsed.workflowKind == WorkflowKindD17Designer );
  REQUIRE( parsed.workflowId == "wf-ndvi-lab" );
  REQUIRE( parsed.stepId == "node_radiometric" );
  REQUIRE( parsed.stepTitle == "辐射定标" );
  REQUIRE( parsed.stepKind == StepKindOperator );
  REQUIRE( parsed.operatorId == "rs:radiometric_calibration" );
  REQUIRE( parsed.operatorDisplayName == "Radiometric Calibration" );

  REQUIRE( parsed.purpose.size() == 1 );
  REQUIRE( parsed.purpose[0].text == "将 DN 转换为 TOA 反射率" );
  REQUIRE( parsed.purpose[0].provenance == FactProvenance::SystemFact );
  REQUIRE( parsed.purpose[0].evidence.size() == 1 );

  REQUIRE( parsed.prerequisites.size() == 1 );
  REQUIRE( parsed.prerequisites[0].provenance == FactProvenance::AuthoredGuidance );

  REQUIRE( parsed.scientificAssumptions.size() == 1 );

  REQUIRE( parsed.stateChanges.size() == 1 );
  REQUIRE( parsed.stateChanges[0].aspect == AspectRadiometricState );
  REQUIRE( parsed.stateChanges[0].before == "DN" );
  REQUIRE( parsed.stateChanges[0].after == "TOA" );
  REQUIRE( parsed.stateChanges[0].provenance == FactProvenance::InferredExplanation );

  REQUIRE( parsed.parameterRationale.size() == 1 );
  REQUIRE( parsed.parameterRationale[0].parameter == "scale" );
  REQUIRE( parsed.parameterRationale[0].chosenValue.isNumeric() );
  REQUIRE( parsed.parameterRationale[0].provenance == FactProvenance::AuthoredGuidance );

  REQUIRE( parsed.skipConsequence.has_value() );
  REQUIRE( parsed.skipConsequence->summary == "跳过后 NDVI 值无物理意义" );
  REQUIRE( parsed.skipConsequence->downstreamRoles == std::vector<std::string>{ "index" } );

  REQUIRE( parsed.execution.has_value() );
  REQUIRE( parsed.execution->status == "Completed" );
  REQUIRE( parsed.execution->elapsedMs.value_or( -1 ) == 1234 );
  REQUIRE( parsed.execution->cacheHit.value_or( true ) == false );
  REQUIRE( parsed.execution->artifactDigest == "sha256full:abc" );

  REQUIRE( parsed.evidenceLinks.size() == 1 );
  REQUIRE( parsed.sourceReferences.size() == 1 );
  REQUIRE( parsed.sourceReferences[0].title == "Lillesand & Kiefer" );
  REQUIRE( parsed.trustNotes.size() == 1 );

  // And the parsed copy serializes to the same bytes.
  REQUIRE( stepExplanationToCanonicalJson( parsed ) == stepExplanationToCanonicalJson( sample ) );
}

TEST_CASE( "minimal explanation round-trips with empty collections omitted", "[explain][schema]" )
{
  StepExplanation minimal;
  minimal.workflowKind = WorkflowKindAgentPlan;
  minimal.workflowId = "wf";
  minimal.stepId = "s1";
  minimal.stepKind = StepKindOperator;
  minimal.operatorId = "rs:ndvi";

  const Json::Value json = minimal.toJson();
  REQUIRE( !json.isMember( "purpose" ) );
  REQUIRE( !json.isMember( "skipConsequence" ) );
  REQUIRE( !json.isMember( "execution" ) );

  StepExplanation parsed;
  std::string error;
  REQUIRE( parsed.fromJson( json, error ) );
  REQUIRE( parsed.purpose.empty() );
  REQUIRE( !parsed.skipConsequence.has_value() );
  REQUIRE( stepExplanationToCanonicalJson( parsed ) == stepExplanationToCanonicalJson( minimal ) );
}

TEST_CASE( "strict parsing rejects unknown keys", "[explain][schema][hallucination]" )
{
  Json::Value json = sampleExplanation().toJson();
  json["definitely_not_in_schema"] = true;
  StepExplanation parsed;
  std::string error;
  REQUIRE( !parsed.fromJson( json, error ) );
  REQUIRE( error.find( "definitely_not_in_schema" ) != std::string::npos );
}

TEST_CASE( "strict parsing rejects unknown nested keys", "[explain][schema][hallucination]" )
{
  Json::Value json = sampleExplanation().toJson();
  json["purpose"][0]["invented"] = "x";
  StepExplanation parsed;
  std::string error;
  REQUIRE( !parsed.fromJson( json, error ) );
  REQUIRE( error.find( "invented" ) != std::string::npos );
}

TEST_CASE( "strict parsing rejects unsupported schema versions", "[explain][schema]" )
{
  Json::Value json = sampleExplanation().toJson();
  json["schemaVersion"] = "exp.step_explanation.v99";
  StepExplanation parsed;
  std::string error;
  REQUIRE( !parsed.fromJson( json, error ) );
  REQUIRE( error.find( "exp.step_explanation.v99" ) != std::string::npos );

  json["schemaVersion"] = 1; // wrong type
  REQUIRE( !parsed.fromJson( json, error ) );
}

TEST_CASE( "strict parsing rejects unknown closed-vocabulary values", "[explain][schema]" )
{
  StepExplanation parsed;
  std::string error;

  Json::Value json = sampleExplanation().toJson();
  json["workflowKind"] = "mission";
  REQUIRE( !parsed.fromJson( json, error ) );
  REQUIRE( error.find( "workflowKind" ) != std::string::npos );

  json = sampleExplanation().toJson();
  json["stepKind"] = "task";
  REQUIRE( !parsed.fromJson( json, error ) );

  json = sampleExplanation().toJson();
  json["stateChanges"][0]["aspect"] = "mood";
  REQUIRE( !parsed.fromJson( json, error ) );

  json = sampleExplanation().toJson();
  json["sourceReferences"][0]["kind"] = "blog";
  REQUIRE( !parsed.fromJson( json, error ) );

  json = sampleExplanation().toJson();
  json["purpose"][0]["provenance"] = "fact";
  REQUIRE( !parsed.fromJson( json, error ) );

  json = sampleExplanation().toJson();
  json["purpose"][0]["evidence"][0]["kind"] = "vibes";
  REQUIRE( !parsed.fromJson( json, error ) );
}

TEST_CASE( "system facts without machine evidence are refused", "[explain][schema][hallucination]" )
{
  // purpose claims system_fact but carries only the authored kind.
  Json::Value json = sampleExplanation().toJson();
  json["purpose"][0]["evidence"][0]["kind"] = EvidenceGuidance;
  StepExplanation parsed;
  std::string error;
  REQUIRE( !parsed.fromJson( json, error ) );
  REQUIRE( error.find( "purpose" ) != std::string::npos );

  // ... and refuses an empty evidence list as well.
  json = sampleExplanation().toJson();
  json["purpose"][0].removeMember( "evidence" );
  REQUIRE( !parsed.fromJson( json, error ) );

  // authored content with no evidence is fine.
  json = sampleExplanation().toJson();
  json["prerequisites"][0].removeMember( "evidence" );
  REQUIRE( parsed.fromJson( json, error ) );
}

TEST_CASE( "inferred content without evidence is refused", "[explain][schema][hallucination]" )
{
  Json::Value json = sampleExplanation().toJson();
  json["stateChanges"][0].removeMember( "evidence" );
  StepExplanation parsed;
  std::string error;
  REQUIRE( !parsed.fromJson( json, error ) );
  REQUIRE( error.find( "stateChanges" ) != std::string::npos );

  // An inferred entry citing only the authored kind is also refused: an
  // inference must cite machine-checkable grounding.
  json = sampleExplanation().toJson();
  json["stateChanges"][0]["evidence"][0]["kind"] = EvidenceGuidance;
  REQUIRE( !parsed.fromJson( json, error ) );
}

TEST_CASE( "execution facts require machine evidence", "[explain][schema][hallucination]" )
{
  Json::Value json = sampleExplanation().toJson();
  json["execution"].removeMember( "evidence" ); // jsoncpp removeMember is void here
  StepExplanation parsed;
  std::string error;
  REQUIRE( !parsed.fromJson( json, error ) );

  json = sampleExplanation().toJson();
  json["execution"]["evidence"] = Json::Value( Json::arrayValue ); // empty array
  REQUIRE( !parsed.fromJson( json, error ) );
}

TEST_CASE( "hostile integer magnitudes are typed refusals", "[explain][schema][hostile]" )
{
  // 2^63 passes isIntegral() but makes asInt64() throw — the refusal must be
  // typed instead of an escaping Json::LogicError.
  Json::Value json = sampleExplanation().toJson();
  json["execution"]["elapsedMs"] = Json::Value( Json::UInt64( 1ULL ) << 63 );
  StepExplanation parsed;
  std::string error;
  REQUIRE( !parsed.fromJson( json, error ) );
  REQUIRE( error.find( "elapsedMs" ) != std::string::npos );
}

TEST_CASE( "malformed evidence links inside explanations are refused", "[explain][schema]" )
{
  Json::Value json = sampleExplanation().toJson();
  json["evidenceLinks"][0]["target"] = "not-a-grammar";
  StepExplanation parsed;
  std::string error;
  REQUIRE( !parsed.fromJson( json, error ) );
  REQUIRE( error.find( "evidenceLinks" ) != std::string::npos );
}

TEST_CASE( "non-object input and wrong-typed fields are refused", "[explain][schema]" )
{
  StepExplanation parsed;
  std::string error;
  REQUIRE( !parsed.fromJson( Json::Value( Json::arrayValue ), error ) );
  REQUIRE( !parsed.fromJson( Json::Value( Json::stringValue ), error ) );

  Json::Value json = sampleExplanation().toJson();
  json["purpose"] = "prose"; // must be an array
  REQUIRE( !parsed.fromJson( json, error ) );

  json = sampleExplanation().toJson();
  json["execution"]["elapsedMs"] = "soon"; // must be numeric
  REQUIRE( !parsed.fromJson( json, error ) );

  json = sampleExplanation().toJson();
  json["trustNotes"] = Json::Value( Json::arrayValue );
  json["trustNotes"][0] = 42; // must be a string
  REQUIRE( !parsed.fromJson( json, error ) );
}
