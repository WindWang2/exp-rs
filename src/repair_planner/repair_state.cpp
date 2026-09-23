// src/repair_planner/repair_state.cpp
#include "repair_state.h"

#include "repair_sha256.h"

#include <algorithm>
#include <set>

namespace sicnu::repair {

namespace {

Json::Value recordsToJson( const std::vector<RepairPlanningRecord> &records )
{
    Json::Value array( Json::arrayValue );
    for ( const RepairPlanningRecord &record : records )
    {
        Json::Value entry( Json::objectValue );
        entry["subject"] = record.subject;
        entry["findings_digest"] = record.findingsDigest;
        entry["plan_id"] = record.planId;
        entry["plan_fingerprint"] = record.planFingerprint;
        entry["status"] = record.status;
        entry["sequence"] = static_cast<Json::Int64>( record.sequence );
        array.append( entry );
    }
    return array;
}

/// The state digest covers everything a reader would trust: every record and
/// the eviction count. Any byte-level mutation of history changes it.
std::string stateDigest( const std::vector<RepairPlanningRecord> &records,
                         long long evicted )
{
    Json::Value content( Json::objectValue );
    content["records"] = recordsToJson( records );
    content["evicted"] = static_cast<Json::Int64>( evicted );
    std::string hex = sha256Hex( jsonToString( content ) );
    hex.resize( 16 );
    return hex;
}

bool validRecord( const RepairPlanningRecord &record )
{
    return !record.subject.empty() && record.findingsDigest.size() == 16 &&
           !record.planId.empty() && record.planFingerprint.size() == 16 &&
           isKnownPlanStatus( record.status );
}

} // namespace

void RepairPlanningState::evictIfNeeded()
{
    while ( mRecords.size() > kCapacity )
    {
        // Deterministic: the lowest-sequence record goes first.
        auto oldest = std::min_element( mRecords.begin(), mRecords.end(),
                                        []( const RepairPlanningRecord &a,
                                           const RepairPlanningRecord &b ) {
                                            return a.sequence < b.sequence;
                                        } );
        mRecords.erase( oldest );
        ++mEvicted;
    }
}

bool RepairPlanningState::record( const RepairPlanningRecord &record, RepairError &error )
{
    if ( !validRecord( record ) )
    {
        error = RepairError{ "invalid_state",
                             "planning record is missing required fields or carries "
                             "unknown statuses/digest shapes" };
        return false;
    }
    for ( const RepairPlanningRecord &existing : mRecords )
    {
        if ( existing.subject == record.subject &&
             existing.findingsDigest == record.findingsDigest )
        {
            if ( existing.planFingerprint == record.planFingerprint )
                return true; // idempotent replay of the same planning outcome
            error = RepairError{ "invalid_state",
                                 "conflicting plan fingerprint for the same "
                                 "(subject, findings digest)" };
            return false;
        }
    }
    mRecords.push_back( record );
    evictIfNeeded();
    return true;
}

bool RepairPlanningState::contains( const std::string &subject,
                                    const std::string &findingsDigest ) const
{
    for ( const RepairPlanningRecord &record : mRecords )
    {
        if ( record.subject == subject && record.findingsDigest == findingsDigest )
            return true;
    }
    return false;
}

RepairPlanningState::Verify RepairPlanningState::verifyPlan( const Json::Value &planDoc ) const
{
    RepairPlan parsed;
    RepairError error;
    if ( !readRepairPlan( planDoc, parsed, error ) )
        return Verify::Tampered;
    const RepairPlanningRecord *matched = nullptr;
    for ( const RepairPlanningRecord &record : mRecords )
    {
        if ( record.planId == parsed.planId )
        {
            matched = &record;
            break;
        }
    }
    if ( matched == nullptr )
        return Verify::Unknown;
    return matched->planFingerprint == repairPlanFingerprint( parsed ) ? Verify::Match
                                                                      : Verify::Tampered;
}

bool RepairPlanningState::resultDigestMatches( const Json::Value &resultDoc ) const
{
    if ( !resultDoc.isObject() || resultDoc.get( "kind", "" ).asString() != "repair_result" )
        return false;
    const std::string planId = resultDoc.get( "plan_id", "" ).asString();
    const std::string digest = resultDoc.get( "findings_digest", "" ).asString();
    if ( planId.empty() || digest.empty() )
        return false;
    for ( const RepairPlanningRecord &record : mRecords )
    {
        if ( record.planId == planId )
            return record.findingsDigest == digest;
    }
    return false;
}

std::size_t RepairPlanningState::size() const
{
    return mRecords.size();
}

std::size_t RepairPlanningState::capacity() const
{
    return kCapacity;
}

long long RepairPlanningState::evictedCount() const
{
    return mEvicted;
}

Json::Value RepairPlanningState::toJson() const
{
    Json::Value doc( Json::objectValue );
    doc["kind"] = "repair_planning_state";
    doc["schema_version"] = "1.0";
    doc["capacity"] = static_cast<Json::Int64>( kCapacity );
    doc["evicted"] = static_cast<Json::Int64>( mEvicted );
    doc["records"] = recordsToJson( mRecords );
    doc["state_digest"] = stateDigest( mRecords, mEvicted );
    return doc;
}

bool RepairPlanningState::fromJson( const Json::Value &doc, RepairPlanningState &out,
                                    RepairError &error )
{
    out = RepairPlanningState{};
    if ( !doc.isObject() || doc.get( "kind", "" ).asString() != "repair_planning_state" )
    {
        error = RepairError{ "invalid_document", "not a repair_planning_state document" };
        return false;
    }
    const Json::Value &records = doc["records"];
    if ( !records.isArray() )
    {
        error = RepairError{ "invalid_document", "records must be an array" };
        return false;
    }
    std::vector<RepairPlanningRecord> parsed;
    std::set<std::pair<std::string, std::string>> seen;
    for ( const Json::Value &entry : records )
    {
        if ( !entry.isObject() )
        {
            error = RepairError{ "invalid_document", "record must be an object" };
            return false;
        }
        RepairPlanningRecord record;
        record.subject = entry.get( "subject", "" ).asString();
        record.findingsDigest = entry.get( "findings_digest", "" ).asString();
        record.planId = entry.get( "plan_id", "" ).asString();
        record.planFingerprint = entry.get( "plan_fingerprint", "" ).asString();
        record.status = entry.get( "status", "" ).asString();
        record.sequence = entry.get( "sequence", 0 ).asInt64();
        if ( !validRecord( record ) )
        {
            error = RepairError{ "invalid_document", "record failed validation" };
            return false;
        }
        const auto key = std::make_pair( record.subject, record.findingsDigest );
        if ( !seen.insert( key ).second )
        {
            error = RepairError{ "invalid_document",
                                 "duplicate (subject, findings digest) record" };
            return false;
        }
        parsed.push_back( record );
    }
    const long long evicted = doc.get( "evicted", 0 ).asInt64();
    if ( evicted < 0 )
    {
        error = RepairError{ "invalid_document", "negative eviction count" };
        return false;
    }
    const std::string expectedDigest = doc.get( "state_digest", "" ).asString();
    if ( expectedDigest != stateDigest( parsed, evicted ) )
    {
        error = RepairError{ "tampered_state",
                             "state digest mismatch: the recorded history was modified" };
        return false;
    }
    out.mRecords = std::move( parsed );
    out.mEvicted = evicted;
    return true;
}

} // namespace sicnu::repair
