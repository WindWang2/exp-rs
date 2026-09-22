// src/science_context/observed_state.cpp
#include "science_context/observed_state.h"

#include "scientific_state/asset_state_types.h"

#include <set>

namespace sicnu::science_context {

Json::Value observedStateFromPassport( const sicnu::state::RemoteSensingAssetState &state )
{
    using sicnu::state::ClaimKind;
    using sicnu::state::claimFor;

    Json::Value body( Json::objectValue );
    body["asset_id"] = state.assetId;
    body["revision"] = state.revision;
    body["modality"] = sicnu::state::modalityToString( state.sensor.modality );
    if ( !state.radiometric.unit.empty() )
        body["radiometric_state"] = state.radiometric.unit;
    if ( state.geometry.hasCrs && !state.geometry.crsAuthid.empty() )
        body["crs"] = state.geometry.crsAuthid;
    if ( state.geometry.hasPixelSize )
    {
        body["pixel_size_x"] = state.geometry.pixelSizeX;
        body["pixel_size_y"] = state.geometry.pixelSizeY;
    }
    if ( state.geometry.hasSize )
    {
        body["width"] = state.geometry.width;
        body["height"] = state.geometry.height;
    }

    Json::Value roles( Json::arrayValue );
    std::set<std::string> uniq;
    for ( const auto &band : state.bands )
    {
        if ( !band.role.empty() )
            uniq.insert( band.role );
    }
    for ( const auto &r : uniq )
        roles.append( r );
    body["band_roles"] = roles;

    // Evidence lattice — conflicted never auto-picked; unknown stays absent.
    Json::Value claims( Json::objectValue );
    bool anyConflicted = false;
    for ( const auto &c : state.claims )
    {
        Json::Value entry( Json::objectValue );
        entry["kind"] = sicnu::state::claimKindToString( c.kind );
        if ( c.kind == ClaimKind::Conflicted )
        {
            anyConflicted = true;
            Json::Value alts( Json::arrayValue );
            for ( const auto &a : c.alternatives )
                alts.append( a );
            entry["alternatives"] = alts;
        }
        claims[c.path] = entry;
    }
    body["claims"] = claims;
    body["conflicted"] = anyConflicted;

    const auto radClaim = claimFor( state, "radiometric.unit" );
    if ( radClaim.kind == ClaimKind::Unknown )
        body.removeMember( "radiometric_state" ); // unknown ≠ default
    if ( radClaim.kind == ClaimKind::Conflicted )
    {
        body.removeMember( "radiometric_state" );
        body["radiometric_conflicted"] = true;
        Json::Value alts( Json::arrayValue );
        for ( const auto &a : radClaim.alternatives )
            alts.append( a );
        body["radiometric_alternatives"] = alts;
    }

    return body;
}

Json::Value understandingEnvelopeFromPassport( const sicnu::state::RemoteSensingAssetState &state )
{
    Json::Value env( Json::objectValue );
    env["schema_version"] = "1.0";
    env["kind"] = "DatasetUnderstanding";
    Json::Value body = observedStateFromPassport( state );
    for ( const auto &name : body.getMemberNames() )
        env[name] = body[name];
    return env;
}

} // namespace sicnu::science_context
