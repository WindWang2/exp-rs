#include "teaching/lab_draft_store.h"

#include "platform/durable_sidecar.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace sicnu::teaching
{
namespace
{

std::string strOf( const Json::Value &v, const char *key )
{
    if ( !v.isObject() || !v.isMember( key ) || !v[key].isString() )
        return {};
    return v[key].asString();
}

} // namespace

std::string labDraftKey( const std::string &courseId, const std::string &labId,
                         const std::string &stepId )
{
    // Length-prefixed components: ids may themselves contain '/' or ':'.
    const auto part = []( const std::string &s ) {
        return std::to_string( s.size() ) + ":" + s;
    };
    return part( courseId ) + "/" + part( labId ) + "/" + part( stepId );
}

LabDraftStoreState loadLabDrafts( const std::string &path, std::size_t maxBytes )
{
    LabDraftStoreState state;
    const sicnu::platform::sidecar::ReadResult read =
        sicnu::platform::sidecar::read( path, "", maxBytes );
    using sicnu::platform::sidecar::ReadSource;
    if ( read.source == ReadSource::Missing )
    {
        state.ok = true; // fresh state: no drafts yet
        return state;
    }
    if ( read.source == ReadSource::Corrupt )
    {
        state.issues.push_back( "lab_draft_store.unreadable: " + read.error );
        return state;
    }
    Json::Value doc;
    Json::CharReaderBuilder reader;
    reader["stackLimit"] = 64;
    std::string errors;
    std::istringstream input( read.bytes );
    try
    {
        if ( !Json::parseFromStream( reader, input, &doc, &errors ) )
        {
            state.issues.push_back( "lab_draft_store.parse_failed: " + errors );
            return state;
        }
    }
    catch ( const std::exception &e )
    {
        state.issues.push_back( std::string( "lab_draft_store.parse_failed: " ) + e.what() );
        return state;
    }
    if ( !doc.isObject() )
    {
        state.issues.push_back( "lab_draft_store.not_object" );
        return state;
    }
    for ( const auto &name : doc.getMemberNames() )
    {
        if ( name != "schema" && name != "drafts" )
        {
            state.issues.push_back( "lab_draft_store.unknown_field: " + name );
            return state;
        }
    }
    if ( strOf( doc, "schema" ) != kLabDraftStoreSchema )
    {
        state.issues.push_back( "lab_draft_store.schema_mismatch" );
        return state;
    }
    if ( !doc.isMember( "drafts" ) || !doc["drafts"].isArray() )
    {
        state.issues.push_back( "lab_draft_store.drafts_must_be_array" );
        return state;
    }
    for ( const auto &item : doc["drafts"] )
    {
        if ( !item.isObject() )
        {
            state.issues.push_back( "lab_draft_store.entry_not_object" );
            return state;
        }
        for ( const auto &field : item.getMemberNames() )
        {
            if ( field != "course_id" && field != "lab_id" && field != "step_id"
                 && field != "text" && field != "submitted" && field != "rules_version"
                 && field != "artifact_path" && field != "updated_at_iso" )
            {
                state.issues.push_back( "lab_draft_store.unknown_entry_field: " + field );
                return state;
            }
        }
        LabDraftRecord r;
        r.courseId = strOf( item, "course_id" );
        r.labId = strOf( item, "lab_id" );
        r.stepId = strOf( item, "step_id" );
        r.text = strOf( item, "text" );
        r.rulesVersion = strOf( item, "rules_version" );
        r.artifactPath = strOf( item, "artifact_path" );
        r.updatedAtIso = strOf( item, "updated_at_iso" );
        if ( item.isMember( "submitted" ) )
        {
            if ( !item["submitted"].isBool() )
            {
                state.issues.push_back( "lab_draft_store.bad_submitted_type" );
                return state;
            }
            r.submitted = item["submitted"].asBool();
        }
        if ( r.courseId.empty() || r.labId.empty() || r.stepId.empty() )
        {
            state.issues.push_back( "lab_draft_store.missing_identity" );
            return state;
        }
        state.drafts.push_back( r );
    }
    state.ok = true;
    return state;
}

bool saveLabDrafts( const std::string &path, const std::vector<LabDraftRecord> &drafts,
                    std::string *error, std::size_t maxBytes )
{
    const auto fail = [error]( const std::string &reason ) {
        if ( error ) *error = reason;
        return false;
    };
    Json::Value doc( Json::objectValue );
    doc["schema"] = kLabDraftStoreSchema;
    Json::Value list( Json::arrayValue );
    for ( const auto &r : drafts )
    {
        if ( r.courseId.empty() || r.labId.empty() || r.stepId.empty() )
            return fail( "lab_draft_store.missing_identity" );
        Json::Value e( Json::objectValue );
        e["course_id"] = r.courseId;
        e["lab_id"] = r.labId;
        e["step_id"] = r.stepId;
        e["text"] = r.text;
        e["submitted"] = r.submitted;
        e["rules_version"] = r.rulesVersion;
        e["artifact_path"] = r.artifactPath;
        e["updated_at_iso"] = r.updatedAtIso;
        list.append( e );
    }
    doc["drafts"] = list;
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "  ";
    writer["emitUTF8"] = true;
    const std::string bytes = Json::writeString( writer, doc );
    if ( bytes.size() > maxBytes )
        return fail( "lab_draft_store.oversize" );
    sicnu::platform::sidecar::WriteRequest request;
    request.targetPath = path;
    request.bytes = bytes;
    request.lastGoodSuffix = ""; // single draft document; no separate rotation
    request.maxBytes = maxBytes;
    const sicnu::platform::sidecar::WriteResult result =
        sicnu::platform::sidecar::write( request );
    if ( !result )
        return fail( std::string( "lab_draft_store.write_failed: " )
                     + sicnu::platform::sidecar::writeStatusName( result.status ) + ": "
                     + result.error );
    return true;
}

LegacyHumanRef parseLegacyHumanRef( const std::string &ref )
{
    LegacyHumanRef out;
    const std::string prefix = "human:";
    if ( ref.rfind( prefix, 0 ) != 0 )
    {
        out.reason = "not_a_human_ref";
        return out;
    }
    const std::string body = ref.substr( prefix.size() );
    const std::size_t lastColon = body.rfind( ':' );
    if ( lastColon == std::string::npos || lastColon == 0
         || lastColon + 1 >= body.size() )
    {
        out.reason = "malformed_human_ref";
        return out;
    }
    const std::string lengthText = body.substr( lastColon + 1 );
    if ( !std::all_of( lengthText.begin(), lengthText.end(),
                       []( char c ) { return c >= '0' && c <= '9'; } ) )
    {
        out.reason = "non_numeric_length";
        return out;
    }
    try
    {
        out.length = std::stoull( lengthText );
    }
    catch ( ... )
    {
        out.reason = "length_overflow";
        return out;
    }
    out.parsed = true;
    out.stepId = body.substr( 0, lastColon );
    out.textRecoverable = false;
    out.reason = "legacy_length_only_ref: the submitted text cannot be restored from a length";
    return out;
}

LabDraftValidity checkLabDraftValidity( const LabDraftRecord &record,
                                        const std::string &currentRulesVersion,
                                        const std::string &currentArtifactPath )
{
    LabDraftValidity out;
    if ( !currentRulesVersion.empty() && !record.rulesVersion.empty()
         && record.rulesVersion != currentRulesVersion )
        out.reasons.push_back( "lab_draft_store.rules_changed" );
    if ( !currentArtifactPath.empty() && !record.artifactPath.empty()
         && record.artifactPath != currentArtifactPath )
        out.reasons.push_back( "lab_draft_store.artifact_changed" );
    out.valid = out.reasons.empty();
    return out;
}

} // namespace sicnu::teaching
