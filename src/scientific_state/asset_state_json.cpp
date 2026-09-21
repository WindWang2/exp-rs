/***************************************************************************
  scientific_state/asset_state_json.cpp
  RS14-01 Scientific Data Passport — deterministic JSON serialization.
 ***************************************************************************/

#include "scientific_state/asset_state_json.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sicnu::state
{

namespace
{

double clamp01( double value )
{
    if ( std::isnan( value ) )
        return 0.0;
    return std::max( 0.0, std::min( 1.0, value ) );
}

void sortUnique( std::vector<std::string> &values )
{
    std::sort( values.begin(), values.end() );
    values.erase( std::unique( values.begin(), values.end() ), values.end() );
}

void setStringIfNotEmpty( Json::Value &node, const char *key, const std::string &value )
{
    if ( !value.empty() )
        node[key] = value;
}

bool readString( const Json::Value &node, const char *key, std::string &out,
                 AssetStateError &error )
{
    if ( node.isMember( key ) )
    {
        if ( !node[key].isString() )
        {
            error.code = StateErrorCode::InvalidField;
            error.message = std::string( "field '" ) + key + "' must be a string";
            return false;
        }
        out = node[key].asString();
    }
    return true;
}

bool readBool( const Json::Value &node, const char *key, bool &out, AssetStateError &error )
{
    if ( node.isMember( key ) )
    {
        if ( !node[key].isBool() )
        {
            error.code = StateErrorCode::InvalidField;
            error.message = std::string( "field '" ) + key + "' must be a bool";
            return false;
        }
        out = node[key].asBool();
    }
    return true;
}

bool readDouble( const Json::Value &node, const char *key, double &out, AssetStateError &error )
{
    if ( node.isMember( key ) )
    {
        // JSON numbers parse as int or real; asDouble covers both.
        if ( !node[key].isNumeric() )
        {
            error.code = StateErrorCode::InvalidField;
            error.message = std::string( "field '" ) + key + "' must be a number";
            return false;
        }
        out = node[key].asDouble();
    }
    return true;
}

bool readInt( const Json::Value &node, const char *key, int &out, AssetStateError &error )
{
    if ( node.isMember( key ) )
    {
        if ( !node[key].isIntegral() )
        {
            error.code = StateErrorCode::InvalidField;
            error.message = std::string( "field '" ) + key + "' must be an integer";
            return false;
        }
        out = node[key].asInt();
    }
    return true;
}

bool readStringArray( const Json::Value &node, const char *key, std::vector<std::string> &out,
                      AssetStateError &error )
{
    if ( node.isMember( key ) )
    {
        const Json::Value &array = node[key];
        if ( !array.isArray() )
        {
            error.code = StateErrorCode::InvalidField;
            error.message = std::string( "field '" ) + key + "' must be an array";
            return false;
        }
        out.clear();
        out.reserve( array.size() );
        for ( const Json::Value &item : array )
        {
            if ( !item.isString() )
            {
                error.code = StateErrorCode::InvalidField;
                error.message = std::string( "field '" ) + key + "' must contain strings";
                return false;
            }
            out.push_back( item.asString() );
        }
    }
    return true;
}

} // namespace

std::string claimKindToString( ClaimKind kind )
{
    switch ( kind )
    {
        case ClaimKind::Known: return "known";
        case ClaimKind::Inferred: return "inferred";
        case ClaimKind::Assumed: return "assumed";
        case ClaimKind::Unknown: return "unknown";
        case ClaimKind::Conflicted: return "conflicted";
    }
    return "unknown";
}

bool claimKindFromString( const std::string &text, ClaimKind &out )
{
    const auto match = [&text]( const char *candidate )
    {
        return std::equal( text.begin(), text.end(), candidate,
                           []( char a, char b ) { return std::tolower( static_cast<unsigned char>( a ) ) == b; } );
    };
    if ( match( "known" ) ) { out = ClaimKind::Known; return true; }
    if ( match( "inferred" ) ) { out = ClaimKind::Inferred; return true; }
    if ( match( "assumed" ) ) { out = ClaimKind::Assumed; return true; }
    if ( match( "unknown" ) ) { out = ClaimKind::Unknown; return true; }
    if ( match( "conflicted" ) ) { out = ClaimKind::Conflicted; return true; }
    return false;
}

std::string assetKindToString( AssetKind kind )
{
    switch ( kind )
    {
        case AssetKind::Raster: return "raster";
        case AssetKind::Vector: return "vector";
        case AssetKind::RemoteMap: return "remote_map";
        case AssetKind::VirtualRaster: return "virtual_raster";
        case AssetKind::Unknown: break;
    }
    return "unknown";
}

bool assetKindFromString( const std::string &text, AssetKind &out )
{
    const auto match = [&text]( const char *candidate )
    {
        return std::equal( text.begin(), text.end(), candidate,
                           []( char a, char b ) { return std::tolower( static_cast<unsigned char>( a ) ) == b; } );
    };
    if ( match( "raster" ) ) { out = AssetKind::Raster; return true; }
    if ( match( "vector" ) ) { out = AssetKind::Vector; return true; }
    if ( match( "remote_map" ) ) { out = AssetKind::RemoteMap; return true; }
    if ( match( "virtual_raster" ) ) { out = AssetKind::VirtualRaster; return true; }
    if ( match( "unknown" ) ) { out = AssetKind::Unknown; return true; }
    return false;
}

std::string assetLifecycleToString( AssetLifecycle lifecycle )
{
    switch ( lifecycle )
    {
        case AssetLifecycle::Registered: return "registered";
        case AssetLifecycle::Resolving: return "resolving";
        case AssetLifecycle::Ready: return "ready";
        case AssetLifecycle::Missing: return "missing";
        case AssetLifecycle::UnavailableSource: return "unavailable_source";
        case AssetLifecycle::Offline: return "offline";
        case AssetLifecycle::AuthenticationRequired: return "authentication_required";
        case AssetLifecycle::Error: return "error";
        case AssetLifecycle::Stale: return "stale";
        case AssetLifecycle::Unknown: break;
    }
    return "unknown";
}

bool assetLifecycleFromString( const std::string &text, AssetLifecycle &out )
{
    const auto match = [&text]( const char *candidate )
    {
        return std::equal( text.begin(), text.end(), candidate,
                           []( char a, char b ) { return std::tolower( static_cast<unsigned char>( a ) ) == b; } );
    };
    if ( match( "registered" ) ) { out = AssetLifecycle::Registered; return true; }
    if ( match( "resolving" ) ) { out = AssetLifecycle::Resolving; return true; }
    if ( match( "ready" ) ) { out = AssetLifecycle::Ready; return true; }
    if ( match( "missing" ) ) { out = AssetLifecycle::Missing; return true; }
    if ( match( "unavailable_source" ) ) { out = AssetLifecycle::UnavailableSource; return true; }
    if ( match( "offline" ) ) { out = AssetLifecycle::Offline; return true; }
    if ( match( "authentication_required" ) ) { out = AssetLifecycle::AuthenticationRequired; return true; }
    if ( match( "error" ) ) { out = AssetLifecycle::Error; return true; }
    if ( match( "stale" ) ) { out = AssetLifecycle::Stale; return true; }
    if ( match( "unknown" ) ) { out = AssetLifecycle::Unknown; return true; }
    return false;
}

std::string modalityToString( Modality modality )
{
    switch ( modality )
    {
        case Modality::Optical: return "optical";
        case Modality::Sar: return "sar";
        case Modality::Thermal: return "thermal";
        case Modality::Hyperspectral: return "hyperspectral";
        case Modality::Unknown: break;
    }
    return "unknown";
}

bool modalityFromString( const std::string &text, Modality &out )
{
    const auto match = [&text]( const char *candidate )
    {
        return std::equal( text.begin(), text.end(), candidate,
                           []( char a, char b ) { return std::tolower( static_cast<unsigned char>( a ) ) == b; } );
    };
    if ( match( "optical" ) ) { out = Modality::Optical; return true; }
    if ( match( "sar" ) ) { out = Modality::Sar; return true; }
    if ( match( "thermal" ) ) { out = Modality::Thermal; return true; }
    if ( match( "hyperspectral" ) ) { out = Modality::Hyperspectral; return true; }
    if ( match( "unknown" ) ) { out = Modality::Unknown; return true; }
    return false;
}

ClaimRecord claimFor( const RemoteSensingAssetState &state, const std::string &path )
{
    for ( const ClaimRecord &claim : state.claims )
    {
        if ( claim.path == path )
            return claim;
    }
    ClaimRecord synthesized;
    synthesized.path = path;
    synthesized.kind = ClaimKind::Unknown;
    return synthesized;
}

void normalizeState( RemoteSensingAssetState &state )
{
    std::sort( state.bands.begin(), state.bands.end(),
               []( const BandState &a, const BandState &b ) { return a.index < b.index; } );

    std::sort( state.temporalRefs.begin(), state.temporalRefs.end(),
               []( const TemporalStateRef &a, const TemporalStateRef &b )
               { return a.collectionId < b.collectionId; } );

    std::sort( state.provenance.inputs.begin(), state.provenance.inputs.end(),
               []( const ProvenanceInputRef &a, const ProvenanceInputRef &b )
               {
                   if ( a.assetId != b.assetId )
                       return a.assetId < b.assetId;
                   return a.revision < b.revision;
               } );
    for ( ProvenanceInputRef &input : state.provenance.inputs )
        sortUnique( input.bandReferences );

    sortUnique( state.modelDerived.labels );
    sortUnique( state.assumptions );
    sortUnique( state.unknowns );

    std::sort( state.claims.begin(), state.claims.end(),
               []( const ClaimRecord &a, const ClaimRecord &b ) { return a.path < b.path; } );
    state.claims.erase( std::unique( state.claims.begin(), state.claims.end(),
                                     []( const ClaimRecord &a, const ClaimRecord &b )
                                     { return a.path == b.path; } ),
                        state.claims.end() );
    for ( ClaimRecord &claim : state.claims )
    {
        sortUnique( claim.sources );
        sortUnique( claim.alternatives );
    }

    std::sort( state.notes.begin(), state.notes.end(),
               []( const ResolutionNote &a, const ResolutionNote &b )
               {
                   if ( a.code != b.code )
                       return a.code < b.code;
                   if ( a.path != b.path )
                       return a.path < b.path;
                   return a.detail < b.detail;
               } );
    state.notes.erase( std::unique( state.notes.begin(), state.notes.end() ), state.notes.end() );
}

Json::Value assetStateToJson( const RemoteSensingAssetState &state )
{
    RemoteSensingAssetState canonical = state;
    normalizeState( canonical );

    Json::Value doc( Json::objectValue );
    doc["schema"] = kAssetStateSchemaId;

    Json::Value identity( Json::objectValue );
    setStringIfNotEmpty( identity, "asset_id", canonical.assetId );
    setStringIfNotEmpty( identity, "revision", canonical.revision );
    setStringIfNotEmpty( identity, "source_path", canonical.sourcePath );
    setStringIfNotEmpty( identity, "display_name", canonical.displayName );
    identity["kind"] = assetKindToString( canonical.kind );
    identity["lifecycle"] = assetLifecycleToString( canonical.lifecycle );
    setStringIfNotEmpty( identity, "persistence", canonical.persistence );
    doc["identity"] = identity;

    Json::Value sensor( Json::objectValue );
    setStringIfNotEmpty( sensor, "platform", canonical.sensor.platform );
    setStringIfNotEmpty( sensor, "instrument", canonical.sensor.instrument );
    setStringIfNotEmpty( sensor, "sensor_key", canonical.sensor.sensorKey );
    sensor["modality"] = modalityToString( canonical.sensor.modality );
    setStringIfNotEmpty( sensor, "product_family", canonical.sensor.productFamily );
    setStringIfNotEmpty( sensor, "product_id", canonical.sensor.productId );
    setStringIfNotEmpty( sensor, "processing_level", canonical.sensor.processingLevel );
    doc["sensor"] = sensor;

    Json::Value acquisition( Json::objectValue );
    setStringIfNotEmpty( acquisition, "time_iso", canonical.acquisition.timeIso );
    setStringIfNotEmpty( acquisition, "time_source", canonical.acquisition.timeSource );
    setStringIfNotEmpty( acquisition, "precision", canonical.acquisition.precision );
    if ( canonical.acquisition.valid )
        acquisition["valid"] = true;
    doc["acquisition"] = acquisition;

    Json::Value bands( Json::arrayValue );
    for ( const BandState &band : canonical.bands )
    {
        Json::Value node( Json::objectValue );
        node["index"] = band.index;
        setStringIfNotEmpty( node, "name", band.name );
        setStringIfNotEmpty( node, "role", band.role );
        if ( band.hasWavelengthNm )
            node["wavelength_nm"] = band.wavelengthNm;
        if ( band.hasFwhmNm )
            node["fwhm_nm"] = band.fwhmNm;
        setStringIfNotEmpty( node, "data_type", band.dataType );
        if ( band.hasNoData )
            node["no_data_value"] = band.noDataValue;
        if ( band.hasScale )
            node["scale"] = band.scale;
        if ( band.hasOffset )
            node["offset"] = band.offset;
        setStringIfNotEmpty( node, "radiometric_unit", band.radiometricUnit );
        if ( band.maskBand )
            node["mask_band"] = true;
        bands.append( node );
    }
    doc["bands"] = bands;

    Json::Value geometry( Json::objectValue );
    if ( canonical.geometry.hasCrs )
    {
        geometry["has_crs"] = true;
        setStringIfNotEmpty( geometry, "crs_wkt", canonical.geometry.crsWkt );
        setStringIfNotEmpty( geometry, "crs_authid", canonical.geometry.crsAuthid );
        if ( canonical.geometry.crsGeographic )
            geometry["crs_geographic"] = true;
        if ( canonical.geometry.crsProjected )
            geometry["crs_projected"] = true;
    }
    if ( canonical.geometry.hasGeoTransform )
    {
        Json::Value gt( Json::arrayValue );
        for ( const double value : canonical.geometry.geoTransform )
            gt.append( value );
        geometry["geo_transform"] = gt;
    }
    if ( canonical.geometry.hasPixelSize )
    {
        geometry["pixel_size_x"] = canonical.geometry.pixelSizeX;
        geometry["pixel_size_y"] = canonical.geometry.pixelSizeY;
    }
    if ( canonical.geometry.hasSize )
    {
        geometry["width"] = canonical.geometry.width;
        geometry["height"] = canonical.geometry.height;
    }
    if ( canonical.geometry.hasExtent )
    {
        geometry["min_x"] = canonical.geometry.minX;
        geometry["min_y"] = canonical.geometry.minY;
        geometry["max_x"] = canonical.geometry.maxX;
        geometry["max_y"] = canonical.geometry.maxY;
    }
    doc["geometry"] = geometry;

    Json::Value validity( Json::objectValue );
    setStringIfNotEmpty( validity, "no_data_policy", canonical.validity.noDataPolicy );
    if ( canonical.validity.hasCloudCover )
        validity["cloud_cover_percent"] = canonical.validity.cloudCoverPercent;
    setStringIfNotEmpty( validity, "quality_mask_info", canonical.validity.qualityMaskInfo );
    doc["validity"] = validity;

    Json::Value radiometric( Json::objectValue );
    setStringIfNotEmpty( radiometric, "unit", canonical.radiometric.unit );
    setStringIfNotEmpty( radiometric, "declared_raw", canonical.radiometric.declaredRaw );
    setStringIfNotEmpty( radiometric, "domain", canonical.radiometric.domain );
    if ( canonical.radiometric.hasNumericScale )
        radiometric["numeric_scale"] = canonical.radiometric.numericScale;
    doc["radiometric"] = radiometric;

    Json::Value temporal( Json::objectValue );
    Json::Value refs( Json::arrayValue );
    for ( const TemporalStateRef &ref : canonical.temporalRefs )
    {
        Json::Value node( Json::objectValue );
        setStringIfNotEmpty( node, "collection_id", ref.collectionId );
        setStringIfNotEmpty( node, "role", ref.role );
        refs.append( node );
    }
    temporal["refs"] = refs;
    if ( canonical.temporalRefsTruncated )
        temporal["truncated"] = true;
    if ( canonical.hasTemporalRefs )
        temporal["present"] = true;
    doc["temporal"] = temporal;

    Json::Value provenance( Json::objectValue );
    if ( canonical.provenance.isDerived )
    {
        provenance["is_derived"] = true;
        setStringIfNotEmpty( provenance, "algorithm_id", canonical.provenance.algorithmId );
        setStringIfNotEmpty( provenance, "algorithm_version", canonical.provenance.algorithmVersion );
        Json::Value inputs( Json::arrayValue );
        for ( const ProvenanceInputRef &input : canonical.provenance.inputs )
        {
            Json::Value node( Json::objectValue );
            setStringIfNotEmpty( node, "asset_id", input.assetId );
            setStringIfNotEmpty( node, "revision", input.revision );
            Json::Value bandRefs( Json::arrayValue );
            for ( const std::string &bandRef : input.bandReferences )
                bandRefs.append( bandRef );
            if ( !input.bandReferences.empty() )
                node["band_references"] = bandRefs;
            setStringIfNotEmpty( node, "value_domain", input.valueDomain );
            inputs.append( node );
        }
        provenance["inputs"] = inputs;
        setStringIfNotEmpty( provenance, "completed_at_utc", canonical.provenance.completedAtUtc );
        setStringIfNotEmpty( provenance, "execution_fingerprint",
                             canonical.provenance.executionFingerprint );
        setStringIfNotEmpty( provenance, "software_version", canonical.provenance.softwareVersion );
        setStringIfNotEmpty( provenance, "workflow_ref", canonical.provenance.workflowRef );
        if ( canonical.provenance.cacheHit )
            provenance["cache_hit"] = true;
    }
    doc["provenance"] = provenance;

    Json::Value modelDerived( Json::objectValue );
    if ( canonical.modelDerived.present )
    {
        modelDerived["present"] = true;
        setStringIfNotEmpty( modelDerived, "model_kind", canonical.modelDerived.modelKind );
        Json::Value labels( Json::arrayValue );
        for ( const std::string &label : canonical.modelDerived.labels )
            labels.append( label );
        if ( !canonical.modelDerived.labels.empty() )
            modelDerived["labels"] = labels;
        if ( canonical.modelDerived.hasAccuracy )
            modelDerived["accuracy"] = canonical.modelDerived.accuracy;
        setStringIfNotEmpty( modelDerived, "sidecar_path", canonical.modelDerived.sidecarPath );
        setStringIfNotEmpty( modelDerived, "feature_schema", canonical.modelDerived.featureSchema );
    }
    doc["model_derived"] = modelDerived;

    doc["confidence"] = clamp01( canonical.confidence );

    Json::Value assumptions( Json::arrayValue );
    for ( const std::string &assumption : canonical.assumptions )
        assumptions.append( assumption );
    doc["assumptions"] = assumptions;

    Json::Value unknowns( Json::arrayValue );
    for ( const std::string &path : canonical.unknowns )
        unknowns.append( path );
    doc["unknowns"] = unknowns;

    Json::Value claims( Json::arrayValue );
    for ( const ClaimRecord &claim : canonical.claims )
    {
        Json::Value node( Json::objectValue );
        node["path"] = claim.path;
        node["kind"] = claimKindToString( claim.kind );
        Json::Value sources( Json::arrayValue );
        for ( const std::string &source : claim.sources )
            sources.append( source );
        if ( !claim.sources.empty() )
            node["sources"] = sources;
        setStringIfNotEmpty( node, "note", claim.note );
        Json::Value alternatives( Json::arrayValue );
        for ( const std::string &alternative : claim.alternatives )
            alternatives.append( alternative );
        if ( !claim.alternatives.empty() )
            node["alternatives"] = alternatives;
        claims.append( node );
    }
    doc["claims"] = claims;

    Json::Value notes( Json::arrayValue );
    for ( const ResolutionNote &note : canonical.notes )
    {
        Json::Value node( Json::objectValue );
        node["code"] = note.code;
        setStringIfNotEmpty( node, "path", note.path );
        setStringIfNotEmpty( node, "detail", note.detail );
        notes.append( node );
    }
    doc["notes"] = notes;

    return doc;
}

bool assetStateFromJson( const Json::Value &json, RemoteSensingAssetState &out,
                         AssetStateError &error )
{
    error = AssetStateError{};
    out = RemoteSensingAssetState{};

    if ( !json.isObject() )
    {
        error.code = StateErrorCode::MalformedJson;
        error.message = "document is not a JSON object";
        return false;
    }

    const Json::Value &schema = json["schema"];
    if ( !schema.isString() || schema.asString() != kAssetStateSchemaId )
    {
        error.code = StateErrorCode::SchemaMismatch;
        error.message = std::string( "schema must be " ) + kAssetStateSchemaId;
        return false;
    }

    RemoteSensingAssetState state;
    state.schemaId = kAssetStateSchemaId;

    const Json::Value &identity = json["identity"];
    if ( !identity.isNull() )
    {
        if ( !identity.isObject() )
        {
            error.code = StateErrorCode::InvalidField;
            error.message = "field 'identity' must be an object";
            return false;
        }
        if ( !readString( identity, "asset_id", state.assetId, error ) ||
             !readString( identity, "revision", state.revision, error ) ||
             !readString( identity, "source_path", state.sourcePath, error ) ||
             !readString( identity, "display_name", state.displayName, error ) ||
             !readString( identity, "persistence", state.persistence, error ) )
            return false;
        if ( identity.isMember( "kind" ) )
        {
            if ( !assetKindFromString( identity["kind"].asString(), state.kind ) )
            {
                error.code = StateErrorCode::InvalidField;
                error.message = "field 'identity.kind' is not a known asset kind";
                return false;
            }
        }
        if ( identity.isMember( "lifecycle" ) )
        {
            if ( !assetLifecycleFromString( identity["lifecycle"].asString(), state.lifecycle ) )
            {
                error.code = StateErrorCode::InvalidField;
                error.message = "field 'identity.lifecycle' is not a known lifecycle";
                return false;
            }
        }
    }

    const Json::Value &sensor = json["sensor"];
    if ( !sensor.isNull() )
    {
        if ( !sensor.isObject() )
        {
            error.code = StateErrorCode::InvalidField;
            error.message = "field 'sensor' must be an object";
            return false;
        }
        if ( !readString( sensor, "platform", state.sensor.platform, error ) ||
             !readString( sensor, "instrument", state.sensor.instrument, error ) ||
             !readString( sensor, "sensor_key", state.sensor.sensorKey, error ) ||
             !readString( sensor, "product_family", state.sensor.productFamily, error ) ||
             !readString( sensor, "product_id", state.sensor.productId, error ) ||
             !readString( sensor, "processing_level", state.sensor.processingLevel, error ) )
            return false;
        if ( sensor.isMember( "modality" ) &&
             !modalityFromString( sensor["modality"].asString(), state.sensor.modality ) )
        {
            error.code = StateErrorCode::InvalidField;
            error.message = "field 'sensor.modality' is not a known modality";
            return false;
        }
    }

    const Json::Value &acquisition = json["acquisition"];
    if ( !acquisition.isNull() )
    {
        if ( !acquisition.isObject() )
        {
            error.code = StateErrorCode::InvalidField;
            error.message = "field 'acquisition' must be an object";
            return false;
        }
        if ( !readString( acquisition, "time_iso", state.acquisition.timeIso, error ) ||
             !readString( acquisition, "time_source", state.acquisition.timeSource, error ) ||
             !readString( acquisition, "precision", state.acquisition.precision, error ) ||
             !readBool( acquisition, "valid", state.acquisition.valid, error ) )
            return false;
    }

    const Json::Value &bands = json["bands"];
    if ( !bands.isNull() )
    {
        if ( !bands.isArray() )
        {
            error.code = StateErrorCode::InvalidField;
            error.message = "field 'bands' must be an array";
            return false;
        }
        for ( const Json::Value &node : bands )
        {
            if ( !node.isObject() )
            {
                error.code = StateErrorCode::InvalidField;
                error.message = "field 'bands' must contain objects";
                return false;
            }
            BandState band;
            if ( !readInt( node, "index", band.index, error ) ||
                 !readString( node, "name", band.name, error ) ||
                 !readString( node, "role", band.role, error ) ||
                 !readDouble( node, "wavelength_nm", band.wavelengthNm, error ) ||
                 !readDouble( node, "fwhm_nm", band.fwhmNm, error ) ||
                 !readString( node, "data_type", band.dataType, error ) ||
                 !readDouble( node, "no_data_value", band.noDataValue, error ) ||
                 !readDouble( node, "scale", band.scale, error ) ||
                 !readDouble( node, "offset", band.offset, error ) ||
                 !readString( node, "radiometric_unit", band.radiometricUnit, error ) ||
                 !readBool( node, "mask_band", band.maskBand, error ) )
                return false;
            band.hasWavelengthNm = node.isMember( "wavelength_nm" );
            band.hasFwhmNm = node.isMember( "fwhm_nm" );
            band.hasNoData = node.isMember( "no_data_value" );
            band.hasScale = node.isMember( "scale" );
            band.hasOffset = node.isMember( "offset" );
            state.bands.push_back( band );
        }
    }

    const Json::Value &geometry = json["geometry"];
    if ( !geometry.isNull() )
    {
        if ( !geometry.isObject() )
        {
            error.code = StateErrorCode::InvalidField;
            error.message = "field 'geometry' must be an object";
            return false;
        }
        if ( geometry.isMember( "has_crs" ) )
        {
            if ( !readBool( geometry, "has_crs", state.geometry.hasCrs, error ) )
                return false;
            if ( state.geometry.hasCrs &&
                 ( !readString( geometry, "crs_wkt", state.geometry.crsWkt, error ) ||
                   !readString( geometry, "crs_authid", state.geometry.crsAuthid, error ) ||
                   !readBool( geometry, "crs_geographic", state.geometry.crsGeographic, error ) ||
                   !readBool( geometry, "crs_projected", state.geometry.crsProjected, error ) ) )
                return false;
        }
        if ( geometry.isMember( "geo_transform" ) )
        {
            const Json::Value &gt = geometry["geo_transform"];
            if ( !gt.isArray() || gt.size() != 6 )
            {
                error.code = StateErrorCode::InvalidField;
                error.message = "field 'geometry.geo_transform' must be an array of 6 numbers";
                return false;
            }
            for ( Json::ArrayIndex i = 0; i < 6; ++i )
            {
                if ( !gt[i].isNumeric() )
                {
                    error.code = StateErrorCode::InvalidField;
                    error.message = "field 'geometry.geo_transform' must contain numbers";
                    return false;
                }
                state.geometry.geoTransform[i] = gt[i].asDouble();
            }
            state.geometry.hasGeoTransform = true;
        }
        if ( geometry.isMember( "pixel_size_x" ) || geometry.isMember( "pixel_size_y" ) )
        {
            if ( !readDouble( geometry, "pixel_size_x", state.geometry.pixelSizeX, error ) ||
                 !readDouble( geometry, "pixel_size_y", state.geometry.pixelSizeY, error ) )
                return false;
            state.geometry.hasPixelSize = true;
        }
        if ( geometry.isMember( "width" ) || geometry.isMember( "height" ) )
        {
            if ( !readInt( geometry, "width", state.geometry.width, error ) ||
                 !readInt( geometry, "height", state.geometry.height, error ) )
                return false;
            state.geometry.hasSize = true;
        }
        if ( geometry.isMember( "min_x" ) || geometry.isMember( "min_y" ) ||
             geometry.isMember( "max_x" ) || geometry.isMember( "max_y" ) )
        {
            if ( !readDouble( geometry, "min_x", state.geometry.minX, error ) ||
                 !readDouble( geometry, "min_y", state.geometry.minY, error ) ||
                 !readDouble( geometry, "max_x", state.geometry.maxX, error ) ||
                 !readDouble( geometry, "max_y", state.geometry.maxY, error ) )
                return false;
            state.geometry.hasExtent = true;
        }
    }

    const Json::Value &validity = json["validity"];
    if ( !validity.isNull() )
    {
        if ( !validity.isObject() )
        {
            error.code = StateErrorCode::InvalidField;
            error.message = "field 'validity' must be an object";
            return false;
        }
        if ( !readString( validity, "no_data_policy", state.validity.noDataPolicy, error ) )
            return false;
        state.validity.hasCloudCover = validity.isMember( "cloud_cover_percent" );
        if ( state.validity.hasCloudCover &&
             !readDouble( validity, "cloud_cover_percent", state.validity.cloudCoverPercent,
                          error ) )
            return false;
        if ( !readString( validity, "quality_mask_info", state.validity.qualityMaskInfo, error ) )
            return false;
    }

    const Json::Value &radiometric = json["radiometric"];
    if ( !radiometric.isNull() )
    {
        if ( !radiometric.isObject() )
        {
            error.code = StateErrorCode::InvalidField;
            error.message = "field 'radiometric' must be an object";
            return false;
        }
        if ( !readString( radiometric, "unit", state.radiometric.unit, error ) ||
             !readString( radiometric, "declared_raw", state.radiometric.declaredRaw, error ) ||
             !readString( radiometric, "domain", state.radiometric.domain, error ) )
            return false;
        state.radiometric.hasNumericScale = radiometric.isMember( "numeric_scale" );
        if ( state.radiometric.hasNumericScale &&
             !readDouble( radiometric, "numeric_scale", state.radiometric.numericScale, error ) )
            return false;
    }

    const Json::Value &temporal = json["temporal"];
    if ( !temporal.isNull() )
    {
        if ( !temporal.isObject() )
        {
            error.code = StateErrorCode::InvalidField;
            error.message = "field 'temporal' must be an object";
            return false;
        }
        if ( !readBool( temporal, "present", state.hasTemporalRefs, error ) ||
             !readBool( temporal, "truncated", state.temporalRefsTruncated, error ) )
            return false;
        const Json::Value &refs = temporal["refs"];
        if ( !refs.isNull() )
        {
            if ( !refs.isArray() )
            {
                error.code = StateErrorCode::InvalidField;
                error.message = "field 'temporal.refs' must be an array";
                return false;
            }
            for ( const Json::Value &node : refs )
            {
                TemporalStateRef ref;
                if ( !readString( node, "collection_id", ref.collectionId, error ) ||
                     !readString( node, "role", ref.role, error ) )
                    return false;
                state.temporalRefs.push_back( ref );
            }
        }
    }

    const Json::Value &provenance = json["provenance"];
    if ( !provenance.isNull() )
    {
        if ( !provenance.isObject() )
        {
            error.code = StateErrorCode::InvalidField;
            error.message = "field 'provenance' must be an object";
            return false;
        }
        if ( provenance.isMember( "is_derived" ) )
        {
            if ( !readBool( provenance, "is_derived", state.provenance.isDerived, error ) )
                return false;
            if ( !state.provenance.isDerived )
                return true;
            if ( !readString( provenance, "algorithm_id", state.provenance.algorithmId, error ) ||
                 !readString( provenance, "algorithm_version", state.provenance.algorithmVersion,
                              error ) ||
                 !readString( provenance, "completed_at_utc", state.provenance.completedAtUtc,
                              error ) ||
                 !readString( provenance, "execution_fingerprint",
                              state.provenance.executionFingerprint, error ) ||
                 !readString( provenance, "software_version", state.provenance.softwareVersion,
                              error ) ||
                 !readString( provenance, "workflow_ref", state.provenance.workflowRef, error ) ||
                 !readBool( provenance, "cache_hit", state.provenance.cacheHit, error ) )
                return false;
            const Json::Value &inputs = provenance["inputs"];
            if ( !inputs.isNull() )
            {
                if ( !inputs.isArray() )
                {
                    error.code = StateErrorCode::InvalidField;
                    error.message = "field 'provenance.inputs' must be an array";
                    return false;
                }
                for ( const Json::Value &node : inputs )
                {
                    ProvenanceInputRef input;
                    if ( !readString( node, "asset_id", input.assetId, error ) ||
                         !readString( node, "revision", input.revision, error ) ||
                         !readString( node, "value_domain", input.valueDomain, error ) ||
                         !readStringArray( node, "band_references", input.bandReferences, error ) )
                        return false;
                    state.provenance.inputs.push_back( input );
                }
            }
        }
    }

    const Json::Value &modelDerived = json["model_derived"];
    if ( !modelDerived.isNull() )
    {
        if ( !modelDerived.isObject() )
        {
            error.code = StateErrorCode::InvalidField;
            error.message = "field 'model_derived' must be an object";
            return false;
        }
        if ( modelDerived.isMember( "present" ) )
        {
            if ( !readBool( modelDerived, "present", state.modelDerived.present, error ) )
                return false;
            if ( !state.modelDerived.present )
                return true;
            if ( !readString( modelDerived, "model_kind", state.modelDerived.modelKind, error ) ||
                 !readString( modelDerived, "sidecar_path", state.modelDerived.sidecarPath,
                              error ) ||
                 !readString( modelDerived, "feature_schema", state.modelDerived.featureSchema,
                              error ) )
                return false;
            state.modelDerived.hasAccuracy = modelDerived.isMember( "accuracy" );
            if ( state.modelDerived.hasAccuracy &&
                 !readDouble( modelDerived, "accuracy", state.modelDerived.accuracy, error ) )
                return false;
            if ( !readStringArray( modelDerived, "labels", state.modelDerived.labels, error ) )
                return false;
        }
    }

    if ( !readDouble( json, "confidence", state.confidence, error ) )
        return false;
    state.confidence = clamp01( state.confidence );

    if ( !readStringArray( json, "assumptions", state.assumptions, error ) ||
         !readStringArray( json, "unknowns", state.unknowns, error ) )
        return false;

    const Json::Value &claims = json["claims"];
    if ( !claims.isNull() )
    {
        if ( !claims.isArray() )
        {
            error.code = StateErrorCode::InvalidField;
            error.message = "field 'claims' must be an array";
            return false;
        }
        for ( const Json::Value &node : claims )
        {
            ClaimRecord claim;
            if ( !readString( node, "path", claim.path, error ) )
                return false;
            if ( node.isMember( "kind" ) )
            {
                if ( !claimKindFromString( node["kind"].asString(), claim.kind ) )
                {
                    error.code = StateErrorCode::InvalidField;
                    error.message = "field 'claims[].kind' is not a known claim kind";
                    return false;
                }
            }
            if ( !readStringArray( node, "sources", claim.sources, error ) ||
                 !readString( node, "note", claim.note, error ) ||
                 !readStringArray( node, "alternatives", claim.alternatives, error ) )
                return false;
            state.claims.push_back( claim );
        }
    }

    const Json::Value &notes = json["notes"];
    if ( !notes.isNull() )
    {
        if ( !notes.isArray() )
        {
            error.code = StateErrorCode::InvalidField;
            error.message = "field 'notes' must be an array";
            return false;
        }
        for ( const Json::Value &node : notes )
        {
            ResolutionNote note;
            if ( !readString( node, "code", note.code, error ) ||
                 !readString( node, "path", note.path, error ) ||
                 !readString( node, "detail", note.detail, error ) )
                return false;
            state.notes.push_back( note );
        }
    }

    normalizeState( state );
    out = std::move( state );
    return true;
}

bool assetStateFromJson( const std::string &text, RemoteSensingAssetState &out,
                         AssetStateError &error )
{
    Json::Value doc;
    Json::CharReaderBuilder builder;
    // Untrusted-input discipline (repo convention): bound the recursion stack
    // and surface jsoncpp exceptions as typed errors.
    builder["stackLimit"] = 128;
    Json::CharReader *reader = builder.newCharReader();
    const std::string parseErrors;
    const bool parsed = reader->parse( text.data(), text.data() + text.size(), &doc, nullptr );
    delete reader;
    if ( !parsed )
    {
        error.code = StateErrorCode::MalformedJson;
        error.message = "document is not valid JSON";
        return false;
    }
    return assetStateFromJson( doc, out, error );
}

std::string serializeState( const RemoteSensingAssetState &state )
{
    const Json::Value doc = assetStateToJson( state );
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    builder["commentStyle"] = "None";
    return Json::writeString( builder, doc );
}

} // namespace sicnu::state
