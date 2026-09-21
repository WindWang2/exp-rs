#include "explain/step_explanation.h"

#include <algorithm>

namespace sicnu::explain
{
namespace
{

std::string provenanceToString( FactProvenance provenance )
{
  return factProvenanceToString( provenance );
}

void appendEvidence( Json::Value &value, const std::vector<EvidenceLink> &evidence )
{
  if ( evidence.empty() )
    return;
  Json::Value array( Json::arrayValue );
  for ( const EvidenceLink &link : evidence )
    array.append( link.toJson() );
  value["evidence"] = array;
}

// Strict object key check: every member must be in the allow-list. The
// offending key is named in the error so authors can fix the document.
bool checkAllowedKeys( const Json::Value &value, const std::vector<const char *> &allowed,
                       const std::string &context, std::string &error )
{
  for ( const std::string &member : value.getMemberNames() )
  {
    const auto it = std::find_if( allowed.begin(), allowed.end(),
                                  [ &member ]( const char *k ) { return member == k; } );
    if ( it == allowed.end() )
    {
      error = context + ": unknown key '" + member + "'";
      return false;
    }
  }
  return true;
}

bool readString( const Json::Value &value, const char *key, std::string &out,
                 const std::string &context, std::string &error, bool required = true )
{
  if ( !value.isMember( key ) )
  {
    if ( required )
    {
      error = context + ": missing string field '" + key + "'";
      return false;
    }
    return true;
  }
  if ( !value[key].isString() )
  {
    error = context + ": field '" + key + "' must be a string";
    return false;
  }
  out = value[key].asString();
  return true;
}

bool readNonEmptyString( const Json::Value &value, const char *key, std::string &out,
                         const std::string &context, std::string &error, bool required = true )
{
  const bool present = value.isMember( key );
  if ( !present && !required )
    return true;
  if ( !readString( value, key, out, context, error, required ) )
    return false;
  if ( out.empty() )
  {
    error = context + ": field '" + key + "' must not be empty";
    return false;
  }
  return true;
}

bool readProvenance( const Json::Value &value, FactProvenance &out,
                     const std::string &context, std::string &error )
{
  std::string text;
  if ( !readNonEmptyString( value, "provenance", text, context, error ) )
    return false;
  if ( !parseFactProvenance( text, out ) )
  {
    error = context + ": unknown provenance '" + text + "'";
    return false;
  }
  return true;
}

bool readEvidence( const Json::Value &value, std::vector<EvidenceLink> &out,
                   const std::string &context, std::string &error, bool required )
{
  if ( !value.isMember( "evidence" ) )
  {
    if ( required )
    {
      error = context + ": missing evidence array";
      return false;
    }
    return true;
  }
  if ( !value["evidence"].isArray() )
  {
    error = context + ": evidence must be an array";
    return false;
  }
  for ( const Json::Value &entry : value["evidence"] )
  {
    EvidenceLink link;
    if ( !link.fromJson( entry, error ) )
    {
      error = context + ": " + error;
      return false;
    }
    out.push_back( link );
  }
  return true;
}

// Trust gate shared by every provenance-bearing entry:
//   system_fact / inferred -> at least one machine-kind link required
//   authored_guidance      -> links optional (but must be well-formed)
bool checkProvenanceEvidence( FactProvenance provenance, const std::vector<EvidenceLink> &evidence,
                              const std::string &context, std::string &error )
{
  if ( provenance == FactProvenance::AuthoredGuidance )
    return true;
  const bool hasMachine = std::any_of( evidence.begin(), evidence.end(),
                                       []( const EvidenceLink &l ) { return isMachineEvidenceKind( l.kind ); } );
  if ( !hasMachine )
  {
    error = context + ": " + provenanceToString( provenance ) +
            " content requires machine-kind evidence";
    return false;
  }
  return true;
}

bool parseGroundedText( const Json::Value &value, GroundedText &out,
                        const std::string &context, std::string &error )
{
  if ( !value.isObject() )
  {
    error = context + ": must be an object";
    return false;
  }
  if ( !checkAllowedKeys( value, { "text", "provenance", "evidence" }, context, error ) )
    return false;
  if ( !readNonEmptyString( value, "text", out.text, context, error ) )
    return false;
  if ( !readProvenance( value, out.provenance, context, error ) )
    return false;
  if ( !readEvidence( value, out.evidence, context, error, false ) )
    return false;
  return checkProvenanceEvidence( out.provenance, out.evidence, context, error );
}

} // namespace

Json::Value SourceReference::toJson() const
{
  Json::Value value( Json::objectValue );
  value["title"] = title;
  value["kind"] = kind;
  value["locator"] = locator;
  if ( !note.empty() )
    value["note"] = note;
  return value;
}

bool SourceReference::fromJson( const Json::Value &value, std::string &error )
{
  if ( !value.isObject() )
  {
    error = "sourceReferences entry: must be an object";
    return false;
  }
  if ( !checkAllowedKeys( value, { "title", "kind", "locator", "note" }, "sourceReferences entry", error ) )
    return false;
  if ( !readNonEmptyString( value, "title", title, "sourceReferences entry", error ) )
    return false;
  std::string kindText;
  if ( !readNonEmptyString( value, "kind", kindText, "sourceReferences entry", error ) )
    return false;
  if ( !isKnownReferenceKind( kindText ) )
  {
    error = "sourceReferences entry: unknown reference kind '" + kindText + "'";
    return false;
  }
  kind = kindText;
  if ( !readNonEmptyString( value, "locator", locator, "sourceReferences entry", error ) )
    return false;
  return readString( value, "note", note, "sourceReferences entry", error, false );
}

Json::Value GroundedText::toJson() const
{
  Json::Value value( Json::objectValue );
  value["text"] = text;
  value["provenance"] = provenanceToString( provenance );
  appendEvidence( value, evidence );
  return value;
}

bool GroundedText::fromJson( const Json::Value &value, std::string &error )
{
  return parseGroundedText( value, *this, "purpose/prerequisites/assumptions entry", error );
}

Json::Value ParameterRationale::toJson() const
{
  Json::Value value( Json::objectValue );
  value["parameter"] = parameter;
  if ( !chosenValue.isNull() )
    value["chosenValue"] = chosenValue;
  value["rationale"] = rationale;
  if ( !misconfigurationConsequence.empty() )
    value["misconfigurationConsequence"] = misconfigurationConsequence;
  value["provenance"] = provenanceToString( provenance );
  appendEvidence( value, evidence );
  return value;
}

bool ParameterRationale::fromJson( const Json::Value &value, std::string &error )
{
  static const std::string context = "parameterRationale entry";
  if ( !value.isObject() )
  {
    error = context + ": must be an object";
    return false;
  }
  if ( !checkAllowedKeys( value,
                          { "parameter", "chosenValue", "rationale", "misconfigurationConsequence",
                            "provenance", "evidence" },
                          context, error ) )
    return false;
  if ( !readNonEmptyString( value, "parameter", parameter, context, error ) )
    return false;
  if ( value.isMember( "chosenValue" ) )
    chosenValue = value["chosenValue"];
  if ( !readNonEmptyString( value, "rationale", rationale, context, error ) )
    return false;
  if ( !readString( value, "misconfigurationConsequence", misconfigurationConsequence, context, error, false ) )
    return false;
  if ( !readProvenance( value, provenance, context, error ) )
    return false;
  if ( !readEvidence( value, evidence, context, error, false ) )
    return false;
  return checkProvenanceEvidence( provenance, evidence, context, error );
}

Json::Value StateTransition::toJson() const
{
  Json::Value value( Json::objectValue );
  value["aspect"] = aspect;
  value["before"] = before;
  value["after"] = after;
  value["explanation"] = explanation;
  value["provenance"] = provenanceToString( provenance );
  appendEvidence( value, evidence );
  return value;
}

bool StateTransition::fromJson( const Json::Value &value, std::string &error )
{
  static const std::string context = "stateChanges entry";
  if ( !value.isObject() )
  {
    error = context + ": must be an object";
    return false;
  }
  if ( !checkAllowedKeys( value,
                          { "aspect", "before", "after", "explanation", "provenance", "evidence" },
                          context, error ) )
    return false;
  std::string aspectText;
  if ( !readNonEmptyString( value, "aspect", aspectText, context, error ) )
    return false;
  if ( !isKnownStateAspect( aspectText ) )
  {
    error = context + ": unknown aspect '" + aspectText + "'";
    return false;
  }
  aspect = aspectText;
  if ( !readString( value, "before", before, context, error ) )
    return false;
  if ( !readString( value, "after", after, context, error ) )
    return false;
  if ( !readNonEmptyString( value, "explanation", explanation, context, error ) )
    return false;
  if ( !readProvenance( value, provenance, context, error ) )
    return false;
  if ( !readEvidence( value, evidence, context, error, false ) )
    return false;
  return checkProvenanceEvidence( provenance, evidence, context, error );
}

Json::Value SkippedStepConsequence::toJson() const
{
  Json::Value value( Json::objectValue );
  value["summary"] = summary;
  if ( !detail.empty() )
    value["detail"] = detail;
  if ( !downstreamRoles.empty() )
  {
    Json::Value roles( Json::arrayValue );
    for ( const std::string &role : downstreamRoles )
      roles.append( role );
    value["downstreamRoles"] = roles;
  }
  value["provenance"] = provenanceToString( provenance );
  appendEvidence( value, evidence );
  return value;
}

bool SkippedStepConsequence::fromJson( const Json::Value &value, std::string &error )
{
  static const std::string context = "skipConsequence";
  if ( !value.isObject() )
  {
    error = context + ": must be an object";
    return false;
  }
  if ( !checkAllowedKeys( value,
                          { "summary", "detail", "downstreamRoles", "provenance", "evidence" },
                          context, error ) )
    return false;
  if ( !readNonEmptyString( value, "summary", summary, context, error ) )
    return false;
  if ( !readString( value, "detail", detail, context, error, false ) )
    return false;
  if ( value.isMember( "downstreamRoles" ) )
  {
    if ( !value["downstreamRoles"].isArray() )
    {
      error = context + ": downstreamRoles must be an array";
      return false;
    }
    for ( const Json::Value &role : value["downstreamRoles"] )
    {
      if ( !role.isString() || role.asString().empty() )
      {
        error = context + ": downstreamRoles entries must be non-empty strings";
        return false;
      }
      downstreamRoles.push_back( role.asString() );
    }
  }
  if ( !readProvenance( value, provenance, context, error ) )
    return false;
  if ( !readEvidence( value, evidence, context, error, false ) )
    return false;
  return checkProvenanceEvidence( provenance, evidence, context, error );
}

Json::Value ExecutionFacts::toJson() const
{
  Json::Value value( Json::objectValue );
  value["status"] = status;
  if ( elapsedMs.has_value() )
    value["elapsedMs"] = static_cast<Json::Int64>( *elapsedMs );
  if ( cacheHit.has_value() )
    value["cacheHit"] = *cacheHit;
  if ( !artifactPath.empty() )
    value["artifactPath"] = artifactPath;
  if ( !artifactDigest.empty() )
    value["artifactDigest"] = artifactDigest;
  if ( !startedUtc.empty() )
    value["startedUtc"] = startedUtc;
  if ( !endedUtc.empty() )
    value["endedUtc"] = endedUtc;
  if ( !errorMessage.empty() )
    value["errorMessage"] = errorMessage;
  appendEvidence( value, evidence );
  return value;
}

bool ExecutionFacts::fromJson( const Json::Value &value, std::string &error )
{
  static const std::string context = "execution";
  if ( !value.isObject() )
  {
    error = context + ": must be an object";
    return false;
  }
  if ( !checkAllowedKeys( value,
                          { "status", "elapsedMs", "cacheHit", "artifactPath", "artifactDigest",
                            "startedUtc", "endedUtc", "errorMessage", "evidence" },
                          context, error ) )
    return false;
  if ( !readNonEmptyString( value, "status", status, context, error ) )
    return false;
  if ( value.isMember( "elapsedMs" ) )
  {
    if ( !value["elapsedMs"].isIntegral() )
    {
      error = context + ": elapsedMs must be an integer";
      return false;
    }
    elapsedMs = value["elapsedMs"].asInt64();
  }
  if ( value.isMember( "cacheHit" ) )
  {
    if ( !value["cacheHit"].isBool() )
    {
      error = context + ": cacheHit must be a boolean";
      return false;
    }
    cacheHit = value["cacheHit"].asBool();
  }
  if ( !readString( value, "artifactPath", artifactPath, context, error, false ) )
    return false;
  if ( !readString( value, "artifactDigest", artifactDigest, context, error, false ) )
    return false;
  if ( !readString( value, "startedUtc", startedUtc, context, error, false ) )
    return false;
  if ( !readString( value, "endedUtc", endedUtc, context, error, false ) )
    return false;
  if ( !readString( value, "errorMessage", errorMessage, context, error, false ) )
    return false;
  if ( !readEvidence( value, evidence, context, error, true ) )
    return false;
  return checkProvenanceEvidence( FactProvenance::SystemFact, evidence, context, error );
}

Json::Value StepExplanation::toJson() const
{
  Json::Value value( Json::objectValue );
  value["schemaVersion"] = schemaVersion;
  value["workflowKind"] = workflowKind;
  value["workflowId"] = workflowId;
  value["stepId"] = stepId;
  if ( !stepTitle.empty() )
    value["stepTitle"] = stepTitle;
  value["stepKind"] = stepKind;
  if ( !operatorId.empty() )
    value["operatorId"] = operatorId;
  if ( !operatorDisplayName.empty() )
    value["operatorDisplayName"] = operatorDisplayName;

  auto appendGrounded = [ & ]( const char *key, const std::vector<GroundedText> &entries )
  {
    if ( entries.empty() )
      return;
    Json::Value array( Json::arrayValue );
    for ( const GroundedText &entry : entries )
      array.append( entry.toJson() );
    value[key] = array;
  };
  appendGrounded( "purpose", purpose );
  appendGrounded( "prerequisites", prerequisites );
  appendGrounded( "scientificAssumptions", scientificAssumptions );

  if ( !stateChanges.empty() )
  {
    Json::Value array( Json::arrayValue );
    for ( const StateTransition &entry : stateChanges )
      array.append( entry.toJson() );
    value["stateChanges"] = array;
  }
  if ( !parameterRationale.empty() )
  {
    Json::Value array( Json::arrayValue );
    for ( const ParameterRationale &entry : parameterRationale )
      array.append( entry.toJson() );
    value["parameterRationale"] = array;
  }
  if ( skipConsequence.has_value() )
    value["skipConsequence"] = skipConsequence->toJson();
  if ( execution.has_value() )
    value["execution"] = execution->toJson();

  if ( !evidenceLinks.empty() )
  {
    Json::Value array( Json::arrayValue );
    for ( const EvidenceLink &link : evidenceLinks )
      array.append( link.toJson() );
    value["evidenceLinks"] = array;
  }
  if ( !sourceReferences.empty() )
  {
    Json::Value array( Json::arrayValue );
    for ( const SourceReference &entry : sourceReferences )
      array.append( entry.toJson() );
    value["sourceReferences"] = array;
  }
  if ( !trustNotes.empty() )
  {
    Json::Value array( Json::arrayValue );
    for ( const std::string &note : trustNotes )
      array.append( note );
    value["trustNotes"] = array;
  }
  return value;
}

bool StepExplanation::fromJson( const Json::Value &value, std::string &error )
{
  static const std::string context = "stepExplanation";
  *this = StepExplanation();

  if ( !value.isObject() )
  {
    error = context + ": must be an object";
    return false;
  }
  if ( !checkAllowedKeys( value,
                          { "schemaVersion", "workflowKind", "workflowId", "stepId", "stepTitle",
                            "stepKind", "operatorId", "operatorDisplayName", "purpose",
                            "prerequisites", "scientificAssumptions", "stateChanges",
                            "parameterRationale", "skipConsequence", "execution",
                            "evidenceLinks", "sourceReferences", "trustNotes" },
                          context, error ) )
    return false;

  std::string schema;
  if ( !readNonEmptyString( value, "schemaVersion", schema, context, error ) )
    return false;
  if ( schema != StepExplanationSchemaV1 )
  {
    error = context + ": unsupported schemaVersion '" + schema + "'";
    return false;
  }
  schemaVersion = schema;

  std::string workflowKindText;
  if ( !readNonEmptyString( value, "workflowKind", workflowKindText, context, error ) )
    return false;
  if ( !isKnownWorkflowKind( workflowKindText ) )
  {
    error = context + ": unknown workflowKind '" + workflowKindText + "'";
    return false;
  }
  workflowKind = workflowKindText;

  if ( !readNonEmptyString( value, "workflowId", workflowId, context, error ) )
    return false;
  if ( !readNonEmptyString( value, "stepId", stepId, context, error ) )
    return false;
  if ( !readString( value, "stepTitle", stepTitle, context, error, false ) )
    return false;

  std::string stepKindText;
  if ( !readNonEmptyString( value, "stepKind", stepKindText, context, error ) )
    return false;
  if ( !isKnownStepKind( stepKindText ) )
  {
    error = context + ": unknown stepKind '" + stepKindText + "'";
    return false;
  }
  stepKind = stepKindText;

  if ( value.isMember( "operatorId" ) )
  {
    if ( !readNonEmptyString( value, "operatorId", operatorId, context, error, false ) )
      return false;
    if ( operatorId.empty() || containsWhitespace( operatorId ) )
    {
      error = context + ": operatorId must be a non-empty id without whitespace";
      return false;
    }
  }
  if ( stepKind == StepKindOperator && operatorId.empty() )
  {
    error = context + ": operator steps require operatorId";
    return false;
  }
  if ( !readString( value, "operatorDisplayName", operatorDisplayName, context, error, false ) )
    return false;

  auto parseArray = [ & ]( const char *key, auto parseEntry ) -> bool
  {
    if ( !value.isMember( key ) )
      return true;
    if ( !value[key].isArray() )
    {
      error = context + ": '" + key + "' must be an array";
      return false;
    }
    for ( const Json::Value &entry : value[key] )
    {
      if ( !parseEntry( entry ) )
        return false;
    }
    return true;
  };

  const bool ok =
    parseArray( "purpose", [ & ]( const Json::Value &entry )
    {
      GroundedText entry_out;
      if ( !parseGroundedText( entry, entry_out, "purpose entry", error ) )
        return false;
      purpose.push_back( entry_out );
      return true;
    } ) &&
    parseArray( "prerequisites", [ & ]( const Json::Value &entry )
    {
      GroundedText entry_out;
      if ( !parseGroundedText( entry, entry_out, "prerequisites entry", error ) )
        return false;
      prerequisites.push_back( entry_out );
      return true;
    } ) &&
    parseArray( "scientificAssumptions", [ & ]( const Json::Value &entry )
    {
      GroundedText entry_out;
      if ( !parseGroundedText( entry, entry_out, "scientificAssumptions entry", error ) )
        return false;
      scientificAssumptions.push_back( entry_out );
      return true;
    } ) &&
    parseArray( "stateChanges", [ & ]( const Json::Value &entry )
    {
      StateTransition entry_out;
      if ( !entry_out.fromJson( entry, error ) )
      {
        error = "stateChanges entry: " + error;
        return false;
      }
      stateChanges.push_back( entry_out );
      return true;
    } ) &&
    parseArray( "parameterRationale", [ & ]( const Json::Value &entry )
    {
      ParameterRationale entry_out;
      if ( !entry_out.fromJson( entry, error ) )
      {
        error = "parameterRationale entry: " + error;
        return false;
      }
      parameterRationale.push_back( entry_out );
      return true;
    } );
  if ( !ok )
    return false;

  if ( value.isMember( "skipConsequence" ) )
  {
    SkippedStepConsequence skipped;
    if ( !skipped.fromJson( value["skipConsequence"], error ) )
    {
      error = "skipConsequence: " + error;
      return false;
    }
    skipConsequence = skipped;
  }
  if ( value.isMember( "execution" ) )
  {
    ExecutionFacts facts;
    if ( !facts.fromJson( value["execution"], error ) )
    {
      error = "execution: " + error;
      return false;
    }
    execution = facts;
  }
  if ( value.isMember( "evidenceLinks" ) )
  {
    if ( !value["evidenceLinks"].isArray() )
    {
      error = context + ": evidenceLinks must be an array";
      return false;
    }
    for ( const Json::Value &entry : value["evidenceLinks"] )
    {
      EvidenceLink link;
      if ( !link.fromJson( entry, error ) )
      {
        error = "evidenceLinks entry: " + error;
        return false;
      }
      evidenceLinks.push_back( link );
    }
  }
  if ( value.isMember( "sourceReferences" ) )
  {
    if ( !value["sourceReferences"].isArray() )
    {
      error = context + ": sourceReferences must be an array";
      return false;
    }
    for ( const Json::Value &entry : value["sourceReferences"] )
    {
      SourceReference reference;
      if ( !reference.fromJson( entry, error ) )
      {
        error = "sourceReferences entry: " + error;
        return false;
      }
      sourceReferences.push_back( reference );
    }
  }
  if ( value.isMember( "trustNotes" ) )
  {
    if ( !value["trustNotes"].isArray() )
    {
      error = context + ": trustNotes must be an array";
      return false;
    }
    for ( const Json::Value &note : value["trustNotes"] )
    {
      if ( !note.isString() || note.asString().empty() )
      {
        error = context + ": trustNotes entries must be non-empty strings";
        return false;
      }
      trustNotes.push_back( note.asString() );
    }
  }
  return true;
}

std::string stepExplanationToCanonicalJson( const StepExplanation &explanation )
{
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  builder["commentStyle"] = "None";
  return Json::writeString( builder, explanation.toJson() );
}

} // namespace sicnu::explain
