// src/agent/autonomy/autonomy_audit.cpp
#include "agent/autonomy/autonomy_audit.h"

namespace sicnu::agent::autonomy {

AutonomyAuditLog &AutonomyAuditLog::instance()
{
    static AutonomyAuditLog log;
    return log;
}

void AutonomyAuditLog::record( const AutonomyRequest &request, const AutonomyPolicy &policy,
                               const AutonomyDecision &decision )
{
    AutonomyAuditRecord entry;
    entry.domain = request.domain;
    entry.role = request.role;
    entry.mode = policy.mode;
    entry.capability = request.capability;
    entry.intent = request.intent;
    entry.actionKey = request.actionKey;
    entry.toolId = request.toolId;
    entry.riskClass = request.riskClass;
    entry.decision = decision.kindString();
    entry.reasonCode = decision.reasonCode;
    entry.downgradeTo = decision.downgradeTo;
    entry.effectiveLevel = decision.effectiveLevel;

    const std::lock_guard<std::mutex> guard( mMutex );
    // Sequence is assigned under the lock; eviction never reuses a number.
    entry.sequence = mNextSequence++;
    mRecords.push_back( std::move( entry ) );
    while ( mRecords.size() > kMaxRecords )
        mRecords.erase( mRecords.begin() );
}

std::vector<AutonomyAuditRecord> AutonomyAuditLog::records() const
{
    const std::lock_guard<std::mutex> guard( mMutex );
    return mRecords;
}

Json::Value AutonomyAuditLog::toJson() const
{
    const std::lock_guard<std::mutex> guard( mMutex );
    Json::Value doc( Json::objectValue );
    doc[ "schema" ] = kAutonomyDecisionSchema;
    doc[ "count" ] = static_cast<Json::UInt64>( mRecords.size() );
    Json::Value entries( Json::arrayValue );
    for ( const AutonomyAuditRecord &entry : mRecords )
        entries.append( entry.toJson() );
    doc[ "records" ] = entries;
    return doc;
}

std::size_t AutonomyAuditLog::size() const
{
    const std::lock_guard<std::mutex> guard( mMutex );
    return mRecords.size();
}

void AutonomyAuditLog::clear()
{
    const std::lock_guard<std::mutex> guard( mMutex );
    mRecords.clear();
    mNextSequence = 1;
}

Json::Value AutonomyAuditRecord::toJson() const
{
    Json::Value doc( Json::objectValue );
    doc[ "schema" ] = schema;
    doc[ "sequence" ] = static_cast<Json::UInt64>( sequence );
    if ( !domain.empty() )
        doc[ "domain" ] = domain;
    if ( !role.empty() )
        doc[ "role" ] = role;
    if ( !mode.empty() )
        doc[ "mode" ] = mode;
    doc[ "capability" ] = capability;
    if ( !intent.empty() )
        doc[ "intent" ] = intent;
    if ( !actionKey.empty() )
        doc[ "action_key" ] = actionKey;
    if ( !toolId.empty() )
        doc[ "tool_id" ] = toolId;
    if ( !riskClass.empty() )
        doc[ "risk_class" ] = riskClass;
    doc[ "decision" ] = decision;
    doc[ "reason_code" ] = reasonCode;
    if ( !downgradeTo.empty() )
        doc[ "downgrade_to" ] = downgradeTo;
    doc[ "effective_level" ] = autonomyLevelToString( effectiveLevel );
    return doc;
}

} // namespace sicnu::agent::autonomy
