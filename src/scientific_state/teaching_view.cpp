/***************************************************************************
  scientific_state/teaching_view.cpp
  RS14-01 Scientific Data Passport — teaching view-model (Qt-free).
 ***************************************************************************/

#include "scientific_state/teaching_view.h"

#include "scientific_state/asset_state_json.h"

#include <json/json.h>

#include <functional>
#include <map>
#include <stdexcept>
#include <system_error>

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
        if ( node.empty() )
            out[prefix] = "[]";
        for ( Json::ArrayIndex i = 0; i < node.size(); ++i )
            flatten( node[i], prefix + "[" + std::to_string( i ) + "]", out );
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

/// Maps a claim path (resolver vocabulary) onto the serialized document
/// paths (JSON vocabulary). Claims talk about logical fields that may map
/// to several document keys or to a different index base (claims use the
/// 1-based GDAL band index, the document array is 0-based).
std::string valueForClaim( const ClaimRecord &claim,
                           const std::map<std::string, std::string> &values )
{
    const auto lookup = [&values]( const std::string &path ) -> std::string
    {
        const auto it = values.find( path );
        return it == values.end() ? std::string() : it->second;
    };

    // Band claims MUST be mapped before any exact-match shortcut: a claim
    // path like "bands[1].role" (1-based GDAL index) collides with the
    // 0-based document path of the SECOND band.
    if ( claim.path.rfind( "bands[", 0 ) == 0 )
    {
        const std::size_t close = claim.path.find( ']' );
        if ( close != std::string::npos )
        {
            int bandNumber = 0;
            try
            {
                bandNumber = std::stoi( claim.path.substr( 6, close - 6 ) );
            }
            catch ( const std::exception & )
            {
                return std::string();
            }
            if ( bandNumber < 1 )
                return std::string();
            const std::string field = claim.path.substr( close + 2 );
            std::string jsonField = field;
            if ( field == "no_data" )
                jsonField = "no_data_value";
            return lookup( "bands[" + std::to_string( bandNumber - 1 ) + "]." + jsonField );
        }
    }

    const auto exact = values.find( claim.path );
    if ( exact != values.end() )
        return exact->second;

    if ( claim.path == "acquisition.time" )
        return lookup( "acquisition.time_iso" );
    if ( claim.path == "geometry.crs" )
    {
        const std::string authid = lookup( "geometry.crs_authid" );
        return authid.empty() ? lookup( "geometry.crs_wkt" ) : authid;
    }
    if ( claim.path == "geometry.pixel_size" )
    {
        const std::string x = lookup( "geometry.pixel_size_x" );
        const std::string y = lookup( "geometry.pixel_size_y" );
        if ( x.empty() && y.empty() )
            return std::string();
        return x + " x " + y;
    }
    if ( claim.path == "geometry.extent" )
    {
        const std::string minX = lookup( "geometry.min_x" );
        const std::string minY = lookup( "geometry.min_y" );
        const std::string maxX = lookup( "geometry.max_x" );
        const std::string maxY = lookup( "geometry.max_y" );
        if ( minX.empty() )
            return std::string();
        return "(" + minX + "," + minY + ")-(" + maxX + "," + maxY + ")";
    }
    if ( claim.path == "validity.cloud_cover" )
        return lookup( "validity.cloud_cover_percent" );
    return std::string();
}

std::string claimLine( const ClaimRecord &claim,
                       const std::map<std::string, std::string> &values )
{
    std::string line = claim.path + ": ";
    const std::string value = valueForClaim( claim, values );
    if ( !value.empty() )
        line += value;
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
    if ( !claim.sources.empty() )
        line += " (source: " + claim.sources.front() + ")";
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
