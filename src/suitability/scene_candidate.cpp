#include "scene_candidate.h"

#include "data/band_role.h"

#include <QJsonArray>
#include <QTimeZone>

namespace sicnu::suitability
{

namespace
{

// sicnu::data carries no AssetState <-> string conversion; the catalog UI
// layer (workspace_service.cpp) spells the states CamelCase — reuse that
// vocabulary so a candidate serializes to the same tokens everywhere.
QString assetStateToken( sicnu::data::AssetState state )
{
    switch ( state )
    {
        case sicnu::data::AssetState::Registered: return QStringLiteral( "Registered" );
        case sicnu::data::AssetState::Resolving: return QStringLiteral( "Resolving" );
        case sicnu::data::AssetState::Ready: return QStringLiteral( "Ready" );
        case sicnu::data::AssetState::Missing: return QStringLiteral( "Missing" );
        case sicnu::data::AssetState::UnavailableSource: return QStringLiteral( "UnavailableSource" );
        case sicnu::data::AssetState::Offline: return QStringLiteral( "Offline" );
        case sicnu::data::AssetState::AuthenticationRequired: return QStringLiteral( "AuthenticationRequired" );
        case sicnu::data::AssetState::Error: return QStringLiteral( "Error" );
        case sicnu::data::AssetState::Stale: return QStringLiteral( "Stale" );
    }
    return QStringLiteral( "Registered" );
}

std::optional<sicnu::data::AssetState> assetStateFromToken( const QString &token )
{
    if ( token == QLatin1String( "Registered" ) ) return sicnu::data::AssetState::Registered;
    if ( token == QLatin1String( "Resolving" ) ) return sicnu::data::AssetState::Resolving;
    if ( token == QLatin1String( "Ready" ) ) return sicnu::data::AssetState::Ready;
    if ( token == QLatin1String( "Missing" ) ) return sicnu::data::AssetState::Missing;
    if ( token == QLatin1String( "UnavailableSource" ) ) return sicnu::data::AssetState::UnavailableSource;
    if ( token == QLatin1String( "Offline" ) ) return sicnu::data::AssetState::Offline;
    if ( token == QLatin1String( "AuthenticationRequired" ) ) return sicnu::data::AssetState::AuthenticationRequired;
    if ( token == QLatin1String( "Error" ) ) return sicnu::data::AssetState::Error;
    if ( token == QLatin1String( "Stale" ) ) return sicnu::data::AssetState::Stale;
    return std::nullopt;
}

QJsonObject extentToJson( const sicnu::data::SpatialExtent &extent )
{
    QJsonObject json;
    json.insert( QStringLiteral( "min_x" ), extent.minimumX );
    json.insert( QStringLiteral( "min_y" ), extent.minimumY );
    json.insert( QStringLiteral( "max_x" ), extent.maximumX );
    json.insert( QStringLiteral( "max_y" ), extent.maximumY );
    json.insert( QStringLiteral( "valid" ), extent.valid );
    return json;
}

sicnu::data::SpatialExtent extentFromJson( const QJsonObject &json )
{
    sicnu::data::SpatialExtent extent;
    extent.minimumX = json.value( QStringLiteral( "min_x" ) ).toDouble();
    extent.minimumY = json.value( QStringLiteral( "min_y" ) ).toDouble();
    extent.maximumX = json.value( QStringLiteral( "max_x" ) ).toDouble();
    extent.maximumY = json.value( QStringLiteral( "max_y" ) ).toDouble();
    extent.valid = json.value( QStringLiteral( "valid" ) ).toBool( false );
    return extent;
}

QJsonObject gridToJson( const sicnu::data::RasterGrid &grid )
{
    QJsonObject json;
    json.insert( QStringLiteral( "crs_wkt" ), grid.crsWkt );
    json.insert( QStringLiteral( "has_geo_transform" ), grid.hasGeoTransform );
    QJsonArray transformArray;
    for ( const double value : grid.geoTransform )
        transformArray.append( value );
    json.insert( QStringLiteral( "geo_transform" ), transformArray );
    json.insert( QStringLiteral( "width" ), grid.width );
    json.insert( QStringLiteral( "height" ), grid.height );
    // Unknown NoData serializes as null — absent must stay absent.
    QJsonArray noDataArray;
    for ( const std::optional<double> &noData : grid.bandNoData )
        noDataArray.append( noData.has_value() ? QJsonValue( *noData ) : QJsonValue( QJsonValue::Null ) );
    json.insert( QStringLiteral( "band_no_data" ), noDataArray );
    return json;
}

sicnu::data::RasterGrid gridFromJson( const QJsonObject &json )
{
    sicnu::data::RasterGrid grid;
    grid.crsWkt = json.value( QStringLiteral( "crs_wkt" ) ).toString();
    grid.hasGeoTransform = json.value( QStringLiteral( "has_geo_transform" ) ).toBool( false );
    const QJsonArray transformArray = json.value( QStringLiteral( "geo_transform" ) ).toArray();
    for ( int i = 0; i < transformArray.size() && i < 6; ++i )
        grid.geoTransform[static_cast< std::size_t >( i )] = transformArray.at( i ).toDouble();
    grid.width = json.value( QStringLiteral( "width" ) ).toInt();
    grid.height = json.value( QStringLiteral( "height" ) ).toInt();
    const QJsonArray noDataArray = json.value( QStringLiteral( "band_no_data" ) ).toArray();
    for ( const QJsonValue &value : noDataArray )
    {
        if ( value.isNull() )
            grid.bandNoData.append( std::nullopt );
        else
            grid.bandNoData.append( value.toDouble() );
    }
    return grid;
}

} // namespace

QJsonObject SceneCandidate::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "schema_version" ), kSceneCandidateSerializationVersion );
    json.insert( QStringLiteral( "id" ), id );
    json.insert( QStringLiteral( "state" ), assetStateToken( state ) );
    json.insert( QStringLiteral( "crs_wkt" ), crsWkt );
    json.insert( QStringLiteral( "extent" ), extentToJson( extent ) );
    if ( gsdM.has_value() )
        json.insert( QStringLiteral( "gsd_m" ), *gsdM );
    if ( pixelSizeCrsUnits.has_value() )
        json.insert( QStringLiteral( "pixel_size_crs_units" ), *pixelSizeCrsUnits );
    if ( acquisitionTimeUtc.has_value() )
        json.insert( QStringLiteral( "acquisition_time_utc" ),
                     acquisitionTimeUtc->toString( Qt::ISODate ) );
    if ( cloudCoverPercent.has_value() )
        json.insert( QStringLiteral( "cloud_cover_percent" ), *cloudCoverPercent );

    QJsonArray bandRolesArray;
    for ( const QString &role : bandRoles )
        bandRolesArray.append( role );
    json.insert( QStringLiteral( "band_roles" ), bandRolesArray );
    json.insert( QStringLiteral( "band_count" ), bandCount );

    if ( grid.has_value() )
        json.insert( QStringLiteral( "grid" ), gridToJson( *grid ) );
    json.insert( QStringLiteral( "modality" ), modality );
    json.insert( QStringLiteral( "provenance" ), provenance );
    return json;
}

sicnu::data::Result<SceneCandidate> SceneCandidate::fromJson( const QJsonObject &json )
{
    const int version = json.value( QStringLiteral( "schema_version" ) ).toInt( -1 );
    if ( version != kSceneCandidateSerializationVersion )
    {
        return sicnu::data::Result<SceneCandidate>::failure( sicnu::data::Diagnostic{
            QStringLiteral( "suitability.scene_schema" ),
            QStringLiteral( "unsupported scene candidate schema_version %1" ).arg( version ),
            sicnu::data::DiagnosticSeverity::Error } );
    }

    SceneCandidate candidate;
    candidate.id = json.value( QStringLiteral( "id" ) ).toString();
    const auto state = assetStateFromToken( json.value( QStringLiteral( "state" ) ).toString() );
    if ( candidate.id.isEmpty() || !state.has_value() )
    {
        return sicnu::data::Result<SceneCandidate>::failure( sicnu::data::Diagnostic{
            QStringLiteral( "suitability.scene_invalid" ),
            QStringLiteral( "scene candidate is missing its id or carries an unknown state" ),
            sicnu::data::DiagnosticSeverity::Error } );
    }
    candidate.state = *state;
    candidate.crsWkt = json.value( QStringLiteral( "crs_wkt" ) ).toString();
    candidate.extent = extentFromJson( json.value( QStringLiteral( "extent" ) ).toObject() );

    const QJsonValue gsdValue = json.value( QStringLiteral( "gsd_m" ) );
    if ( gsdValue.isDouble() )
        candidate.gsdM = gsdValue.toDouble();
    const QJsonValue pixelValue = json.value( QStringLiteral( "pixel_size_crs_units" ) );
    if ( pixelValue.isDouble() )
        candidate.pixelSizeCrsUnits = pixelValue.toDouble();

    const QString acquisitionText = json.value( QStringLiteral( "acquisition_time_utc" ) ).toString();
    if ( !acquisitionText.isEmpty() )
    {
        QDateTime acquisition = QDateTime::fromString( acquisitionText, Qt::ISODate );
        if ( !acquisition.isValid() )
        {
            // A persisted candidate with an unparsable timestamp must not come
            // back silently time-less — that would fabricate "unknown".
            return sicnu::data::Result<SceneCandidate>::failure( sicnu::data::Diagnostic{
                QStringLiteral( "suitability.scene_invalid" ),
                QStringLiteral( "scene candidate carries an unparsable acquisition_time_utc: %1" )
                    .arg( acquisitionText ),
                sicnu::data::DiagnosticSeverity::Error } );
        }
        if ( acquisition.timeSpec() == Qt::LocalTime )
            acquisition.setTimeZone( QTimeZone::utc() );
        candidate.acquisitionTimeUtc = acquisition;
    }

    const QJsonValue cloudValue = json.value( QStringLiteral( "cloud_cover_percent" ) );
    if ( cloudValue.isDouble() )
        candidate.cloudCoverPercent = cloudValue.toDouble();

    const QJsonArray bandRolesArray = json.value( QStringLiteral( "band_roles" ) ).toArray();
    for ( const QJsonValue &roleValue : bandRolesArray )
        candidate.bandRoles.append( roleValue.toString() );
    candidate.bandCount = json.value( QStringLiteral( "band_count" ) ).toInt();

    const QJsonValue gridValue = json.value( QStringLiteral( "grid" ) );
    if ( gridValue.isObject() )
        candidate.grid = gridFromJson( gridValue.toObject() );

    candidate.modality = json.value( QStringLiteral( "modality" ) ).toString();
    candidate.provenance = json.value( QStringLiteral( "provenance" ) ).toObject();
    return sicnu::data::Result<SceneCandidate>::success( candidate );
}

SceneCandidate sceneCandidateFromRasterStructure(
    const QString &id, sicnu::data::AssetState state,
    const sicnu::data::RasterStructure &structure, const QJsonObject &hints )
{
    SceneCandidate candidate;
    candidate.id = id;
    candidate.state = state;
    candidate.crsWkt = structure.crsWkt;
    candidate.extent = structure.extent;
    candidate.bandCount = structure.bandCount;

    if ( structure.hasGeoTransform )
    {
        // Evidence only: the raw pixel size in CRS units. Never presented as
        // meters — see SceneCandidate::gsdM.
        const double pixelSize = structure.geoTransform[1];
        if ( pixelSize != 0.0 )
            candidate.pixelSizeCrsUnits = pixelSize;
    }

    for ( const sicnu::data::RasterBandStructure &band : structure.bands )
    {
        if ( band.role == sicnu::data::BandRole::Unknown )
            continue;
        const QString role = sicnu::data::bandRoleToString( band.role );
        if ( !candidate.bandRoles.contains( role ) )
            candidate.bandRoles.append( role );
    }

    const QJsonValue gsdHint = hints.value( QStringLiteral( "gsd_m" ) );
    if ( gsdHint.isDouble() )
        candidate.gsdM = gsdHint.toDouble();

    const QString acquisitionHint = hints.value( QStringLiteral( "acquisition_time_utc" ) ).toString();
    if ( !acquisitionHint.isEmpty() )
    {
        QDateTime acquisition = QDateTime::fromString( acquisitionHint, Qt::ISODate );
        if ( acquisition.isValid() )
        {
            // A zoneless ISO timestamp is read as UTC (key says _utc).
            if ( acquisition.timeSpec() == Qt::LocalTime )
                acquisition.setTimeZone( QTimeZone::utc() );
            candidate.acquisitionTimeUtc = acquisition;
        }
        // Unparsable hint leaves the field unset — unknown stays unknown.
    }

    const QJsonValue cloudHint = hints.value( QStringLiteral( "cloud_cover_percent" ) );
    if ( cloudHint.isDouble() )
        candidate.cloudCoverPercent = cloudHint.toDouble();

    const QString modalityHint = hints.value( QStringLiteral( "modality" ) ).toString();
    if ( !modalityHint.isEmpty() )
        candidate.modality = modalityHint;

    return candidate;
}

} // namespace sicnu::suitability
