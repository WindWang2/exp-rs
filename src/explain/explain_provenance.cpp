#include "explain/explain_provenance.h"

#include <cctype>

namespace sicnu::explain
{
namespace
{

bool startsWith( const std::string &text, const char *prefix )
{
  const std::string p( prefix );
  return text.size() >= p.size() && text.compare( 0, p.size(), p ) == 0;
}

// remainder := part of a target after the "<kind>:" prefix
bool isValidPlainRemainder( const std::string &remainder )
{
  return !remainder.empty() && remainder.find( '#' ) == std::string::npos &&
         !containsWhitespace( remainder );
}

bool isValidHashedRemainder( const std::string &remainder )
{
  const std::size_t hash = remainder.find( '#' );
  if ( hash == std::string::npos || hash == 0 || hash + 1 >= remainder.size() )
    return false;
  if ( remainder.find( '#', hash + 1 ) != std::string::npos )
    return false;
  return !containsWhitespace( remainder );
}

bool isValidProvenanceRemainder( const std::string &remainder )
{
  const std::size_t hash = remainder.find( '#' );
  if ( hash == std::string::npos || hash == 0 || hash + 1 >= remainder.size() )
    return false;
  if ( remainder.find( '#', hash + 1 ) != std::string::npos )
    return false;
  if ( containsWhitespace( remainder ) )
    return false;

  const std::string nodePart = remainder.substr( hash + 1 );
  if ( nodePart == "run" )
    return true;
  return startsWith( nodePart, "node:" ) && nodePart.size() > std::string( "node:" ).size();
}

} // namespace

const char *factProvenanceToString( FactProvenance provenance )
{
  switch ( provenance )
  {
    case FactProvenance::SystemFact:
      return "system_fact";
    case FactProvenance::AuthoredGuidance:
      return "authored_guidance";
    case FactProvenance::InferredExplanation:
      return "inferred";
  }
  return "";
}

bool parseFactProvenance( const std::string &text, FactProvenance &out )
{
  if ( text == "system_fact" )
  {
    out = FactProvenance::SystemFact;
    return true;
  }
  if ( text == "authored_guidance" )
  {
    out = FactProvenance::AuthoredGuidance;
    return true;
  }
  if ( text == "inferred" )
  {
    out = FactProvenance::InferredExplanation;
    return true;
  }
  return false;
}

bool isKnownEvidenceKind( const std::string &kind )
{
  return kind == EvidenceOperatorSchema || kind == EvidenceWorkflowDocument ||
         kind == EvidenceDerivation || kind == EvidenceProvenance ||
         kind == EvidenceOperationLog || kind == EvidenceContract ||
         kind == EvidenceGuidance;
}

bool isMachineEvidenceKind( const std::string &kind )
{
  return isKnownEvidenceKind( kind ) && kind != EvidenceGuidance;
}

bool containsWhitespace( const std::string &text )
{
  for ( const unsigned char c : text )
  {
    if ( std::isspace( c ) )
      return true;
  }
  return false;
}

bool isValidEvidenceTarget( const std::string &kind, const std::string &target )
{
  if ( !isKnownEvidenceKind( kind ) || target.empty() || containsWhitespace( target ) )
    return false;

  if ( kind == EvidenceOperatorSchema )
    return startsWith( target, "operator:" ) &&
           isValidPlainRemainder( target.substr( std::string( "operator:" ).size() ) );
  if ( kind == EvidenceContract )
    return startsWith( target, "contract:" ) &&
           isValidPlainRemainder( target.substr( std::string( "contract:" ).size() ) );
  if ( kind == EvidenceGuidance )
    return startsWith( target, "guidance:" ) &&
           isValidPlainRemainder( target.substr( std::string( "guidance:" ).size() ) );
  if ( kind == EvidenceOperationLog )
    return startsWith( target, "operation_log:" ) &&
           isValidPlainRemainder( target.substr( std::string( "operation_log:" ).size() ) );
  if ( kind == EvidenceWorkflowDocument )
    return startsWith( target, "workflow:" ) &&
           isValidHashedRemainder( target.substr( std::string( "workflow:" ).size() ) );
  if ( kind == EvidenceDerivation )
    return startsWith( target, "derivation:" ) &&
           isValidHashedRemainder( target.substr( std::string( "derivation:" ).size() ) );
  if ( kind == EvidenceProvenance )
    return startsWith( target, "provenance:" ) &&
           isValidProvenanceRemainder( target.substr( std::string( "provenance:" ).size() ) );
  return false;
}

bool isKnownWorkflowKind( const std::string &kind )
{
  return kind == WorkflowKindD17Designer || kind == WorkflowKindGuidedPipeline ||
         kind == WorkflowKindLab || kind == WorkflowKindAgentPlan;
}

bool isKnownStepKind( const std::string &kind )
{
  return kind == StepKindOperator || kind == StepKindInteractive ||
         kind == StepKindReview || kind == StepKindComposite;
}

bool isKnownStateAspect( const std::string &aspect )
{
  return aspect == AspectRadiometricState || aspect == AspectCrs ||
         aspect == AspectResolution || aspect == AspectBandCount;
}

bool isKnownReferenceKind( const std::string &kind )
{
  return kind == ReferenceKindTextbook || kind == ReferenceKindPaper ||
         kind == ReferenceKindStandard || kind == ReferenceKindDoc;
}

Json::Value EvidenceLink::toJson() const
{
  Json::Value value( Json::objectValue );
  value["kind"] = kind;
  value["target"] = target;
  if ( !note.empty() )
    value["note"] = note;
  return value;
}

bool EvidenceLink::fromJson( const Json::Value &value, std::string &error )
{
  if ( !value.isObject() )
  {
    error = "evidence link must be an object";
    return false;
  }
  for ( const std::string &member : value.getMemberNames() )
  {
    if ( member != "kind" && member != "target" && member != "note" )
    {
      error = "unknown evidence link key '" + member + "'";
      return false;
    }
  }
  if ( !value.isMember( "kind" ) || !value["kind"].isString() ||
       !value.isMember( "target" ) || !value["target"].isString() )
  {
    error = "evidence link requires string 'kind' and 'target'";
    return false;
  }
  kind = value["kind"].asString();
  target = value["target"].asString();
  note = value.isMember( "note" ) && value["note"].isString() ? value["note"].asString() : std::string();
  if ( !isValid() )
  {
    error = "invalid evidence link kind='" + kind + "' target='" + target + "'";
    return false;
  }
  return true;
}

bool EvidenceLink::isValid() const
{
  return isValidEvidenceTarget( kind, target );
}

} // namespace sicnu::explain
