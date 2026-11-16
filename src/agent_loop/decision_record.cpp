// src/agent_loop/decision_record.cpp
#include "decision_record.h"

#include "session_state.h"

#include <json/writer.h>

namespace sicnu::agent_loop {
namespace {

Json::Value alternativeToJson( const DecisionAlternative &alt )
{
    Json::Value doc( Json::objectValue );
    doc["id"] = alt.id;
    doc["description"] = alt.description;
    doc["why_not"] = alt.whyNot;
    return doc;
}

Json::Value evidenceToJson( const DecisionEvidence &ev )
{
    Json::Value doc( Json::objectValue );
    doc["kind"] = ev.kind;
    doc["ref"] = ev.ref;
    return doc;
}

bool readString( const Json::Value &doc, const char *key, std::string &out )
{
    if ( !doc.isMember( key ) || !doc[ key ].isString() )
        return false;
    out = doc[ key ].asString();
    return true;
}

} // namespace

Json::Value DecisionRecord::toJson() const
{
    Json::Value doc( Json::objectValue );
    doc["schema_version"] = schemaVersion;
    doc["decision_id"] = decisionId;
    doc["session_id"] = sessionId;
    doc["stage"] = stage;
    doc["recorded_at"] = Json::Value( static_cast< Json::Int64 >( recordedAt ) );
    doc["inputs"] = inputs;
    doc["selected"] = selected;
    doc["reason"] = reason;
    doc["policy"] = policy;

    Json::Value alternatives( Json::arrayValue );
    for ( const DecisionAlternative &alt : this->alternatives )
        alternatives.append( alternativeToJson( alt ) );
    doc["alternatives"] = alternatives;

    Json::Value evidence( Json::arrayValue );
    for ( const DecisionEvidence &ev : this->evidence )
        evidence.append( evidenceToJson( ev ) );
    doc["evidence"] = evidence;
    return doc;
}

std::optional< DecisionRecord > DecisionRecord::fromJson( const Json::Value &doc,
                                                          std::string *error )
{
    auto fail = [ &error ]( const std::string &message ) {
        if ( error )
            *error = message;
        return std::optional< DecisionRecord >{};
    };

    if ( !doc.isObject() )
        return fail( "decision record is not an object" );

    std::string version;
    if ( !readString( doc, "schema_version", version ) )
        return fail( "decision record: missing schema_version" );
    if ( version != kDecisionRecordSchemaVersion )
        return fail( "decision record: unsupported schema_version '" + version + "'" );

    DecisionRecord record;
    record.schemaVersion = version;
    if ( !readString( doc, "decision_id", record.decisionId ) || record.decisionId.empty() )
        return fail( "decision record: missing decision_id" );
    if ( !readString( doc, "session_id", record.sessionId ) || record.sessionId.empty() )
        return fail( "decision record: missing session_id" );
    if ( !readString( doc, "stage", record.stage ) || !isKnownStage( record.stage ) )
        return fail( "decision record: unknown stage '" + record.stage + "'" );

    if ( !doc.isMember( "recorded_at" ) || !doc[ "recorded_at" ].isInt64() )
        return fail( "decision record: missing recorded_at" );
    record.recordedAt = doc[ "recorded_at" ].asInt64();

    if ( !doc.isMember( "inputs" ) || ( !doc[ "inputs" ].isObject() && !doc[ "inputs" ].isNull() ) )
        return fail( "decision record: inputs must be an object" );
    record.inputs = doc[ "inputs" ].isNull() ? Json::Value( Json::objectValue ) : doc[ "inputs" ];
    // #2079: the inner fields the resume reconciler and delivery assembler
    // read as strings must be strings in a persisted record, so a torn or
    // foreign journal is a diagnosable refusal instead of a Json::LogicError
    // escaping a direct (non-driver) caller.
    for ( const char *field : { "run_id", "rule_id", "risk_class", "root_cause_code" } )
        if ( record.inputs.isMember( field ) && !record.inputs[ field ].isString() )
            return fail( std::string( "decision record: inputs." ) + field + " must be a string" );

    if ( !doc.isMember( "selected" ) || !doc[ "selected" ].isObject() )
        return fail( "decision record: selected must be an object" );
    record.selected = doc[ "selected" ];
    for ( const char *field : { "action", "plan_id" } )
        if ( record.selected.isMember( field ) && !record.selected[ field ].isString() )
            return fail( std::string( "decision record: selected." ) + field + " must be a string" );

    if ( !readString( doc, "reason", record.reason ) || record.reason.empty() )
        return fail( "decision record: reason must be a non-empty string" );

    const Json::Value &alternatives = doc[ "alternatives" ];
    if ( !alternatives.isArray() )
        return fail( "decision record: alternatives must be an array" );
    for ( const Json::Value &alt : alternatives )
    {
        if ( !alt.isObject() )
            return fail( "decision record: alternative is not an object" );
        DecisionAlternative parsed;
        if ( !readString( alt, "id", parsed.id ) || parsed.id.empty() )
            return fail( "decision record: alternative missing id" );
        if ( !readString( alt, "description", parsed.description ) )
            return fail( "decision record: alternative missing description" );
        readString( alt, "why_not", parsed.whyNot );
        record.alternatives.push_back( parsed );
    }

    const Json::Value &evidence = doc[ "evidence" ];
    if ( !evidence.isArray() )
        return fail( "decision record: evidence must be an array" );
    for ( const Json::Value &ev : evidence )
    {
        if ( !ev.isObject() )
            return fail( "decision record: evidence entry is not an object" );
        DecisionEvidence parsed;
        if ( !readString( ev, "kind", parsed.kind ) || parsed.kind.empty() )
            return fail( "decision record: evidence missing kind" );
        if ( !readString( ev, "ref", parsed.ref ) || parsed.ref.empty() )
            return fail( "decision record: evidence missing ref" );
        record.evidence.push_back( parsed );
    }

    if ( doc.isMember( "policy" ) && !doc[ "policy" ].isObject() && !doc[ "policy" ].isNull() )
        return fail( "decision record: policy must be an object" );
    record.policy = doc.get( "policy", Json::Value( Json::objectValue ) );

    return record;
}

} // namespace sicnu::agent_loop
