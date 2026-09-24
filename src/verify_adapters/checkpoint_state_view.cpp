// src/verify_adapters/checkpoint_state_view.cpp
#include "verify_adapters/checkpoint_state_view.h"

#include "bounded_io.h"

#include <algorithm>
#include <cctype>

namespace sicnu::verify_adapters
{

const char *const kCheckpointStateUnknown = "unknown";

CheckpointReadResult readCheckpoint( const std::string &checkpointPath )
{
    CheckpointReadResult result;
    const IoResult read = readFileBounded( checkpointPath, kMaxAdapterDocumentBytes );
    if ( read.failure != IoFailure::None )
    {
        if ( read.failure == IoFailure::Missing )
        {
            result.status = CheckpointReadStatus::Missing;
            result.detail = "checkpoint '" + checkpointPath + "' does not exist";
        }
        else if ( read.failure == IoFailure::OverCap )
        {
            result.status = CheckpointReadStatus::Oversized;
            result.detail = "checkpoint '" + checkpointPath + "': " + read.detail;
        }
        else
        {
            result.status = CheckpointReadStatus::Unreadable;
            result.detail = "checkpoint '" + checkpointPath + "': " + read.detail;
        }
        return result;
    }
    std::string error;
    const std::optional<Json::Value> parsed = parseJsonBounded( read.text, error );
    if ( !parsed )
    {
        result.status = CheckpointReadStatus::Unreadable;
        result.detail = "checkpoint '" + checkpointPath + "' is not parseable JSON: " + error;
        return result;
    }
    const Json::Value &kind = ( *parsed )["kind"];
    const Json::Value &version = ( *parsed )["version"];
    const bool knownVersion = version.isString() &&
                              ( version.asString() == kCheckpointVersionCurrent ||
                                version.asString() == kCheckpointVersionLegacy );
    if ( !kind.isString() || kind.asString() != kCheckpointKind || !knownVersion )
    {
        result.status = CheckpointReadStatus::ForeignEnvelope;
        result.detail = "checkpoint '" + checkpointPath + "' carries kind/version '" +
                        ( kind.isString() ? kind.asString() : "" ) + "'/'" +
                        ( version.isString() ? version.asString() : "" ) + "'";
        return result;
    }
    if ( !( *parsed )["nodes"].isArray() )
    {
        result.status = CheckpointReadStatus::ForeignEnvelope;
        result.detail = "checkpoint '" + checkpointPath + "' has no nodes array";
        return result;
    }
    result.status = CheckpointReadStatus::Ok;
    result.document = std::move( *parsed );
    return result;
}

namespace
{

std::string lowercase( std::string s )
{
    std::transform( s.begin(), s.end(), s.begin(),
                    []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
    return s;
}

/// The checkpoint's own state spelling (executionStateString: "Pending" ...)
/// projected into the lower-case wire vocabulary above. Anything this build
/// did not write stays "unknown" — a wider future vocabulary must bump the
/// checkpoint version, and until then no expectation can read it as success.
std::string projectState( const Json::Value &stateValue )
{
    if ( !stateValue.isString() )
        return kCheckpointStateUnknown;
    const std::string lowered = lowercase( stateValue.asString() );
    for ( const char *known : { "pending", "ready", "running", "succeeded", "failed",
                                "cancelled", "skipped" } )
        if ( lowered == known )
            return lowered;
    return kCheckpointStateUnknown;
}

} // namespace

CheckpointStateView::CheckpointStateView( Json::Value document )
{
    // Same closed envelope as readCheckpoint — a direct construction must
    // not smuggle in a version this platform never wrote (a projected state
    // of a foreign document could otherwise read as success).
    const Json::Value &kind = document["kind"];
    const Json::Value &version = document["version"];
    const bool knownVersion =
        version.isString() && ( version.asString() == kCheckpointVersionCurrent ||
                                version.asString() == kCheckpointVersionLegacy );
    const bool envelopeOk =
        document.isObject() && kind.isString() && kind.asString() == kCheckpointKind &&
        knownVersion && document["nodes"].isArray();
    if ( !envelopeOk )
    {
        mUsable = false;
        return;
    }
    Json::Value run( Json::objectValue );
    if ( document["runId"].isString() )
        run["id"] = document["runId"];
    if ( document["finished"].isBool() )
        run["finished"] = document["finished"];
    if ( document["success"].isBool() )
        run["success"] = document["success"];
    if ( document["attempt"].isIntegral() && !document["attempt"].isBool() )
        run["attempt"] = document["attempt"];
    mRun = std::move( run );
    mNodes = std::move( document["nodes"] );
    mUsable = true;
}

std::optional<Json::Value> CheckpointStateView::state( const std::string &key )
{
    if ( !mUsable )
        return std::nullopt;

    if ( key.rfind( "run/", 0 ) == 0 )
    {
        const std::string member = key.substr( 4 );
        if ( !mRun.isMember( member ) )
            return std::nullopt;
        return mRun[member];
    }

    if ( key.rfind( "node/", 0 ) != 0 )
        return std::nullopt;
    // node/<nodeId>/<field> — nodeId is caller-supplied content and may
    // itself contain '/', so split on the LAST '/' and match the field name.
    const std::size_t lastSlash = key.find_last_of( '/' );
    if ( lastSlash == std::string::npos || lastSlash + 1 >= key.size() ||
         lastSlash < 5 /* "node/" */ )
        return std::nullopt;
    const std::string field = key.substr( lastSlash + 1 );
    if ( field != "state" && field != "artifact" && field != "fingerprint" )
        return std::nullopt;
    const std::string nodeId = key.substr( 5, lastSlash - 5 );
    if ( nodeId.empty() )
        return std::nullopt;

    for ( const Json::Value &node : mNodes )
    {
        if ( !node.isObject() || !node["nodeId"].isString() ||
             node["nodeId"].asString() != nodeId )
            continue;
        if ( field == "state" )
        {
            // Projected through the closed vocabulary; a node present with
            // an unprojectable state answers "unknown", never nullopt — the
            // difference between "recorded but unjudgeable" and "no record".
            Json::Value projected( projectState( node["state"] ) );
            return projected;
        }
        const Json::Value &value = node[field == "artifact" ? "artifact" : "artifactFingerprint"];
        if ( !value.isString() )
            return std::nullopt;
        return value;
    }
    // The node is not in the checkpoint at all: no record — the engine's
    // "not recorded" path.
    return std::nullopt;
}

} // namespace sicnu::verify_adapters
