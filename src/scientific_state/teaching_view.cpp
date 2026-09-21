/***************************************************************************
  scientific_state/teaching_view.cpp
  RS14-01 Scientific Data Passport — teaching view-model (Qt-free).
 ***************************************************************************/

#include "scientific_state/teaching_view.h"

#include "scientific_state/asset_state_json.h"

#include <json/json.h>

#include <functional>
#include <map>

namespace sicnu::state
{

namespace
{

/// Flattens the canonical JSON document into path → value text (arrays and
/// scalars as their serialized text). Mirrors the diff flattening contract.
void flatten( const Json::Value &node, const std::string &prefix,
              std::map<std::string, std::string> &out )
{
    if ( node.isObject() )
    {
        for ( const std::string &key : node.getMemberNames() )
        {
            if ( key == "schema" || key == "claims" )
                continue;
            flatten( node[key], prefix.empty() ? key : prefix + "." + key, out );
        }
        return;
    }
    if ( node.isArray() )
    {
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        out[prefix] = Json::writeString( builder, node );
        return;
    }
    if ( node.isString() )
        out[prefix] = node.asString();
    else
    {
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        out[prefix] = Json::writeString( builder, node );
    }
}

std::string claimLine( const ClaimRecord &claim,
                       const std::map<std::string, std::string> &values )
{
    std::string line = claim.path + ": ";
    const auto value = values.find( claim.path );
    if ( value != values.end() && !value->second.empty() )
        line += value->second;
    else
        line += "(not resolved)";
    if ( claim.kind == ClaimKind::Conflicted && !claim.alternatives.empty() )
    {
        line += " [alternatives:";
        for ( const std::string &alternative : claim.alternatives )
            line += " " + alternative;
        line += "]";
    }
    if ( !claim.note.empty() && claim.kind != ClaimKind::Known )
        line += " — " + claim.note;
    line += " (source: " + ( claim.sources.empty() ? std::string( "resolver" ) :
                                                    claim.sources.front() ) + ")";
    return line;
}

void bucketByKind( const RemoteSensingAssetState &state,
                   const std::map<std::string, std::string> &values, ClaimKind kind,
                   std::vector<std::string> &out )
{
    for ( const ClaimRecord &claim : state.claims )
    {
        if ( claim.kind == kind )
            out.push_back( claimLine( claim, values ) );
    }
}

} // namespace

TeachingSummary renderTeachingSummary( const RemoteSensingAssetState &state )
{
    TeachingSummary summary;

    const Json::Value doc = assetStateToJson( state );
    std::map<std::string, std::string> values;
    flatten( doc, "", values );

    std::string name = state.displayName;
    if ( name.empty() && !state.sourcePath.empty() )
    {
        const std::size_t slash = state.sourcePath.find_last_of( '/' );
        name = slash == std::string::npos ? state.sourcePath
                                          : state.sourcePath.substr( slash + 1 );
    }
    summary.headline = "Asset: " + ( name.empty() ? std::string( "(unnamed)" ) : name ) +
                       " (" + assetKindToString( state.kind ) + ")";

    bucketByKind( state, values, ClaimKind::Known, summary.known );
    bucketByKind( state, values, ClaimKind::Inferred, summary.inferred );
    bucketByKind( state, values, ClaimKind::Assumed, summary.assumed );
    bucketByKind( state, values, ClaimKind::Conflicted, summary.conflicted );
    bucketByKind( state, values, ClaimKind::Unknown, summary.missing );

    return summary;
}

std::string teachingSummaryToPlainText( const TeachingSummary &summary )
{
    std::string text = summary.headline + "\n";
    const auto section = [&text]( const char *title, const std::vector<std::string> &lines )
    {
        text += std::string( title ) + ":\n";
        for ( const std::string &line : lines )
            text += "  - " + line + "\n";
    };
    section( "Known (declared)", summary.known );
    section( "Inferred", summary.inferred );
    section( "Assumed", summary.assumed );
    section( "Conflicted", summary.conflicted );
    section( "Missing / unknown", summary.missing );
    return text;
}

} // namespace sicnu::state
