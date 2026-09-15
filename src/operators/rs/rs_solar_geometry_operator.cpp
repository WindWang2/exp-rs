/***************************************************************************
 * rs_solar_geometry_operator.cpp — see the header for the contract.
 * Metadata-only: no pixel reads; the optional in-place stamp follows the
 * SatelliteProducts::setRadiometricState GA_Update pattern.
 ***************************************************************************/
#include "rs_solar_geometry_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/solar_geometry.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QDate>
#include <QTime>

#include <gdal.h>

namespace sicnu::operators::rs {

using namespace params;

namespace {
/// Stamped dataset-metadata keys (DECISIONS.md D8; consumed by
/// rs:brdf_normalization and documented in docs/processing/radiometric-physics-11.md).
constexpr const char *kSunZenith = "SICNU_SUN_ZENITH";
constexpr const char *kSunAzimuth = "SICNU_SUN_AZIMUTH";
constexpr const char *kSunElevation = "SICNU_SUN_ELEVATION";
constexpr const char *kEarthSunFactor = "SICNU_EARTH_SUN_FACTOR";
constexpr const char *kEarthSunDistance = "SICNU_EARTH_SUN_DISTANCE_AU";
} // namespace

Json::Value RsSolarGeometryOperator::schema() const {
    using namespace schema;
    Json::Value props( Json::objectValue );
    props["date"] = makeStringParam( "date", "Acquisition date (UTC), ISO YYYY-MM-DD", "" );
    props["utc_time"] = makeStringParam( "utc_time", "Acquisition time of day (UTC), HH:mm[:ss]", "" );
    props["latitude"] = makeNumberParam( "latitude", "Scene centre latitude in degrees [-90, 90]", 0.0 );
    props["longitude"] = makeNumberParam( "longitude", "Scene centre longitude in degrees [-180, 180] (east positive)", 0.0 );
    props["input"] = makeRasterParam( "input", "Optional raster whose dataset metadata receives the SICNU_SUN_* / SICNU_EARTH_SUN_* keys (in-place update; read-only sources are refused)", false );
    props["write_metadata"] = makeBooleanParam( "write_metadata", "Write the geometry onto 'input' as dataset metadata", false );

    Json::Value outputs( Json::objectValue );
    outputs["sun_elevation"] = makeNumberParam( "sun_elevation", "Sun elevation in degrees", 0.0 );
    outputs["sun_azimuth"] = makeNumberParam( "sun_azimuth", "Sun azimuth in degrees from north", 0.0 );
    outputs["earth_sun_factor"] = makeNumberParam( "earth_sun_factor", "Inverse-square earth-sun factor E0 = d^-2", 0.0 );

    Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
    root["required"] = makeRequired( { "date", "utc_time", "latitude", "longitude" } );
    return root;
}

Json::Value RsSolarGeometryOperator::metadata() const {
    Json::Value meta( Json::objectValue );
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append( "optical" );
    meta["tags"].append( "solar-geometry" );
    meta["tags"].append( "calibration" );
    meta["task"] = "radiometric-normalization";
    meta["notes"] = "Spencer (1971) Fourier series + NOAA general form; ~0.05 deg accuracy, "
                    "no atmospheric refraction (calibration geometry uses the true position).";
    meta["gpu"] = false;
    meta["purpose"] = "Derive usable sun geometry for calibration/BRDF/topographic operators "
                      "when scene metadata lacks angles.";
    meta["prerequisites"].append( "Acquisition date and UTC time (e.g. Landsat MTL "
                                  "DATE_ACQUIRED + SCENE_CENTER_TIME) and the scene centre "
                                  "latitude/longitude." );
    meta["workflowHints"].append( "Run once per scene before radiometric calibration or "
                                  "rs:brdf_normalization; with write_metadata=true the stamped "
                                  "keys are picked up automatically." );
    meta["limitations"].append( "Below-horizon suns are still stamped for traceability; "
                                "downstream operators refuse to calibrate with them "
                                "(sun_above_horizon=false and elevation <= 0 in the record)." );
    meta["limitations"].append( "In-place metadata update requires a writable raster; read-only "
                                "sources are refused with a typed error." );
    return meta;
}

Json::Value RsSolarGeometryOperator::executionEstimate() const {
    Json::Value est( Json::objectValue );
    est["estimatedRamBytes"] = Json::Value::UInt64( 0ULL ); // no pixel buffers
    return est;
}

Json::Value RsSolarGeometryOperator::run( const Json::Value &params, RSOperatorContext &context )
{
    if ( !params.isObject() )
        throw RSOperatorError( ErrorCode::InvalidParameter, "Operator parameters must be a JSON object" );

    const std::string dateStr = requireString( params, "date" );
    const std::string timeStr = requireString( params, "utc_time" );
    if ( !hasNumber( params, "latitude" ) )
        throw RSOperatorError( ErrorCode::MissingRequiredParameter, "latitude is required" );
    if ( !hasNumber( params, "longitude" ) )
        throw RSOperatorError( ErrorCode::MissingRequiredParameter, "longitude is required" );
    const double latitude = getDouble( params, "latitude", 0.0 );
    const double longitude = getDouble( params, "longitude", 0.0 );
    const bool writeMetadata = getBool( params, "write_metadata", false );
    const bool hasInput = params.isMember( "input" ) && params["input"].isString()
                          && !params["input"].asString().empty();

    const QDate date = QDate::fromString( QString::fromStdString( dateStr ), Qt::ISODate );
    if ( !date.isValid() )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "date must be a valid ISO YYYY-MM-DD date, got " + dateStr );
    QTime time = QTime::fromString( QString::fromStdString( timeStr ), Qt::ISODate );
    if ( !time.isValid() )
        time = QTime::fromString( QString::fromStdString( timeStr ), "HH:mm" );
    if ( !time.isValid() )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "utc_time must be HH:mm or HH:mm:ss, got " + timeStr );
    if ( latitude < -90.0 || latitude > 90.0 )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "latitude must be in [-90, 90] degrees" );
    if ( longitude < -180.0 || longitude > 180.0 )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "longitude must be in [-180, 180] degrees" );
    if ( writeMetadata && !hasInput )
        throw RSOperatorError( ErrorCode::MissingRequiredParameter,
                               "write_metadata=true requires the 'input' raster" );

    SolarGeometry::SunPosition pos;
    QString error;
    if ( !SolarGeometry::solarPosition( date, time, latitude, longitude, &pos, &error ) )
        throw RSOperatorError( ErrorCode::InvalidParameter, error.toStdString() );

    double e0 = 0.0;
    if ( !SolarGeometry::earthSunFactor( SolarGeometry::dayOfYear( date ), &e0, &error ) )
        throw RSOperatorError( ErrorCode::InvalidParameter, error.toStdString() );

    if ( writeMetadata )
    {
        ensureGdalInit();
        const QString path = QString::fromStdString( params["input"].asString() );
        GDALDatasetH ds = GDALOpen( path.toUtf8().constData(), GA_Update );
        if ( !ds )
            throw RSOperatorError( ErrorCode::FileNotWritable,
                                   "Cannot open '" + params["input"].asString()
                                       + "' for in-place metadata update (read-only source?)" );
        const auto stamp = [&]( const char *key, double valueDeg ) {
            GDALSetMetadataItem( ds, key, QString::number( valueDeg, 'g', 10 ).toUtf8().constData(),
                                 nullptr );
        };
        stamp( kSunZenith, pos.zenithDeg );
        stamp( kSunAzimuth, pos.azimuthDeg );
        stamp( kSunElevation, pos.elevationDeg );
        stamp( kEarthSunFactor, e0 );
        stamp( kEarthSunDistance, pos.earthSunDistanceAu );
        GDALClose( ds );
        context.logInfo( "Stamped solar geometry metadata onto " + params["input"].asString() );
    }

    Json::Value result( Json::objectValue );
    result["date"] = dateStr;
    result["utc_time"] = timeStr;
    result["latitude"] = latitude;
    result["longitude"] = longitude;
    result["sun_zenith"] = pos.zenithDeg;
    result["sun_elevation"] = pos.elevationDeg;
    result["sun_azimuth"] = pos.azimuthDeg;
    result["declination"] = pos.declinationDeg;
    result["hour_angle"] = pos.hourAngleDeg;
    result["equation_of_time_minutes"] = pos.equationOfTimeMin;
    result["earth_sun_factor"] = e0;
    result["earth_sun_distance_au"] = pos.earthSunDistanceAu;
    result["sun_above_horizon"] = pos.sunAboveHorizon;
    result["metadata_written"] = writeMetadata;
    context.reportProgress( 1.0, "Solar geometry computed" );
    return result;
}

} // namespace sicnu::operators::rs
