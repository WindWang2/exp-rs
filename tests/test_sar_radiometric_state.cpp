// tests/test_sar_radiometric_state.cpp — SAR Radiometric State 13.0 (Track E):
// machine-readable census of every registered rs:sar_* operator, behavioral
// state gates for the first-class radiometric family, derived-product
// semantics (ratio/texture), DN LUT calibration with a hand-computed pixel
// oracle, and the end-to-end chain provenance gate
// (import → calibrate → speckle → terrain → geocode → derived).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QStringList>

#include <json/json.h>

#include <gdal_priv.h>
#include <ogr_spatialref.h>

#include <cmath>
#include <string>
#include <vector>

#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/algorithms/sar/sar_metadata.h"
#include "processing/algorithms/sar/sar_orbit.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

using Catch::Approx;
using namespace sicnu::operators;

namespace
{

int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_sar_radiometric_state";
char *appArgv[] = { appArgv0, nullptr };

struct AppInit
{
    AppInit()
    {
        if ( !QCoreApplication::instance() )
            new QCoreApplication( appArgc(), appArgv );
    }
};

// ─── Fixture helpers ────────────────────────────────────────────────────────

/// Writes a single-band Float32 GeoTIFF (EPSG:32648, 10 m pixels) with
/// arbitrary dataset-level metadata items.
bool writeRasterEx( const QString &path, const std::vector<float> &values, int width, int height,
                    const std::vector<std::pair<std::string, std::string>> &meta )
{
    ensureGdalInit();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
        return false;
    GDALDatasetH ds =
        GDALCreate( driver, path.toUtf8().constData(), width, height, 1, GDT_Float32, nullptr );
    if ( !ds )
        return false;
    const double gt[6] = { 500000, 10, 0, 4500000, 0, -10 };
    GDALSetGeoTransform( ds, const_cast<double *>( gt ) );
    OGRSpatialReference srs;
    if ( srs.importFromEPSG( 32648 ) == OGRERR_NONE )
    {
        char *wkt = nullptr;
        srs.exportToWkt( &wkt );
        GDALSetProjection( ds, wkt );
        CPLFree( wkt );
    }
    for ( const auto &item : meta )
        GDALSetMetadataItem( ds, item.first.c_str(), item.second.c_str(), nullptr );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    const bool ok = GDALRasterIO( band, GF_Write, 0, 0, width, height,
                                  const_cast<float *>( values.data() ), width, height,
                                  GDT_Float32, 0, 0 ) == CE_None;
    GDALClose( ds );
    return ok;
}

std::vector<float> readBand( const QString &path, int band = 1 )
{
    GdalDatasetWrapper ds;
    if ( !ds.open( path ) )
        return {};
    std::vector<float> out( static_cast<size_t>( ds.width() ) * ds.height() );
    if ( !ds.readBandData( band, out.data(), ds.width(), ds.height() ) )
        return {};
    return out;
}

std::string metaItem( const QString &path, const char *key )
{
    GdalDatasetWrapper ds;
    if ( !ds.open( path ) )
        return {};
    const QString v = sicnu::sar::datasetMeta( ds, key );
    return v.toStdString();
}

bool writeTextFile( const QString &path, const QString &content )
{
    QFile f( path );
    if ( !f.open( QIODevice::WriteOnly | QIODevice::Text ) )
        return false;
    const QByteArray bytes = content.toUtf8();
    return f.write( bytes ) == bytes.size() && f.flush();
}

Json::Value runOp( const std::string &id, const Json::Value &params )
{
    auto op = RSOperatorRegistry::instance().create( id );
    if ( !op )
        throw RSOperatorError( ErrorCode::InvalidParameter, "operator not registered: " + id );
    RSOperatorContext ctx;
    return op->run( params, ctx );
}

// ─── Census table (machine-readable) ────────────────────────────────────────
//
// kind: "radiometric" — the operator writes/derives a declared SAR radiometric
//       state and the behavioral gate runs it against a synthetic fixture;
//       "exempt" — the product is not a single-state backscatter raster
//       (complex/phase/geometry/mask/statistics), so no radiometric state is
//       declared or required; the reason is recorded here, not implied.
struct CensusEntry
{
    const char *id;
    const char *kind;
    const char *rule;
    const char *expectState; ///< radiometric entries only
};

const std::vector<CensusEntry> &sarCensus()
{
    static const std::vector<CensusEntry> table = {
        // ── first-class radiometric family ──
        { "rs:sar_calibrate", "radiometric",
          "DN → sigma0 via calibrationA or a per-row calibration LUT; refuses "
          "declared calibrated/derived/unknown states (double-scale guard)",
          "sigma0" },
        { "rs:sar_backscatter", "radiometric",
          "converts between declared calibrated states with explicit geometry; "
          "fromCalibration must match the declared state",
          "gamma0" },
        { "rs:sar_speckle", "radiometric",
          "radiometrically neutral: propagates the input's declared state "
          "(canonical or derived) onto the output",
          "sigma0" },
        { "rs:sar_terrain_flatten", "radiometric",
          "requires declared sigma0 (legacy undeclared accepted with a "
          "warning): sigma0·cosθ0/cosθi → gamma0; other declared states refuse",
          "gamma0" },
        { "rs:sar_terrain_correction", "radiometric",
          "same input-state contract as terrain_flatten, 3-band product",
          "gamma0" },
        { "rs:sar_geocode", "radiometric",
          "requires declared sigma0 (legacy undeclared accepted with a "
          "warning); writes the propagated state plus per-band states "
          "(band 2 gamma0 = sigma0·sinθL/sinθ0)",
          "sigma0" },
        { "rs:sar_ratio", "radiometric",
          "pair metric over two same-state scenes; declares the derived token "
          "sar_pair_metric — never a backscatter calibration",
          "sar_pair_metric" },
        { "rs:sar_texture", "radiometric",
          "GLCM measures over an intensity band; declares the derived token "
          "sar_texture — never a backscatter calibration",
          "sar_texture" },
        // ── exempt families ──
        { "rs:sar_change", "exempt",
          "binary change mask over calibrated scenes: a classification "
          "product, not a radiometric quantity", "" },
        { "rs:sar_coregister", "exempt",
          "complex-SLC co-registration: outputs resampled complex data", "" },
        { "rs:sar_coregister_local", "exempt",
          "offset-field co-registration: complex-SLC geometry product", "" },
        { "rs:sar_displacement", "exempt",
          "line-of-sight displacement field: a geometry product", "" },
        { "rs:sar_dualpol_features", "exempt",
          "dual-pol feature cube: dimensionless features, not single-state "
          "backscatter", "" },
        { "rs:sar_interferogram", "exempt",
          "complex interferogram: phase product", "" },
        { "rs:sar_network_inversion", "exempt",
          "small-baseline inversion: displacement time series", "" },
        { "rs:sar_pair_network", "exempt",
          "pair-network builder: metadata/graph product, no raster radiometry", "" },
        { "rs:sar_phase_filter", "exempt",
          "interferometric phase filtering: phase product", "" },
        { "rs:sar_polsar_decompose", "exempt",
          "polarimetric decompositions (H/A/alpha, Yamaguchi…): feature "
          "products, not single-state backscatter", "" },
        { "rs:sar_remove_topographic_phase", "exempt",
          "topographic-phase removal: phase product", "" },
        { "rs:sar_temporal_events", "exempt",
          "temporal change events: event table product", "" },
        { "rs:sar_temporal_stats", "exempt",
          "multi-date statistics: aggregate product, not a single-scene "
          "radiometric state", "" },
        { "rs:sar_terrain_masks", "exempt",
          "layover/shadow/incidence masks and geometry products", "" },
        { "rs:sar_unwrap", "exempt",
          "phase unwrapping: phase product", "" },
    };
    return table;
}

// ─── Geocode fixture (circular-orbit scene contract, from test_sar_geocoding) ──

constexpr double kR = 7000000.0;
constexpr double kOmega = 2.0 * M_PI / 5880.0;
constexpr double kA = 6378137.0;
constexpr double kNadirRange = kR - kA;
constexpr int kSarW = 8;
constexpr int kSarH = 8;
constexpr double kPrf = 100.0;
constexpr double kAzStart = 30.0;
constexpr double kRangeRate = 299792458.0 / ( 2.0 * 30.0 );
constexpr double kSampleSpacing = 30.0;
constexpr double kRangeStart = kNadirRange - 90.0;
constexpr double kSceneCenterLonDeg = kOmega * kAzStart * 180.0 / M_PI;

sicnu::sar::OrbitSegment makeCircularOrbit()
{
    sicnu::sar::OrbitSegment orbit;
    for ( int i = 0; i <= 6; ++i )
    {
        const double t = 10.0 * i;
        const double phase = kOmega * t;
        sicnu::sar::OrbitStateVector s;
        s.t = t;
        s.x = kR * std::cos( phase );
        s.y = kR * std::sin( phase );
        s.z = 0.0;
        s.vx = -kR * kOmega * std::sin( phase );
        s.vy = kR * kOmega * std::cos( phase );
        s.vz = 0.0;
        orbit.states.push_back( s );
    }
    return orbit;
}

QString encodeOrbit( const sicnu::sar::OrbitSegment &orbit )
{
    QStringList records;
    for ( const sicnu::sar::OrbitStateVector &s : orbit.states )
        records << QString::number( s.t, 'g', 17 ) + ";" + QString::number( s.x, 'g', 17 )
                       + ";" + QString::number( s.y, 'g', 17 ) + ";"
                       + QString::number( s.z, 'g', 17 ) + ";"
                       + QString::number( s.vx, 'g', 17 ) + ";"
                       + QString::number( s.vy, 'g', 17 ) + ";"
                       + QString::number( s.vz, 'g', 17 );
    return records.join( QLatin1Char( '|' ) );
}

/// Writes the 8x8 SAR scene carrying the orbit/timing contract.
bool writeSarScene( const QString &path, const std::vector<float> &values,
                    const std::vector<std::pair<std::string, std::string>> &extraMeta )
{
    ensureGdalInit();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
        return false;
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), kSarW, kSarH, 1,
                                  GDT_Float32, nullptr );
    if ( !ds )
        return false;
    const double startT = kRangeStart / ( 299792458.0 / 2.0 );
    const double stopT = startT + ( kSarW - 1.0 ) / kRangeRate;
    GDALSetMetadataItem( ds, "SICNU_SAR_ORBIT_STATES",
                         encodeOrbit( makeCircularOrbit() ).toUtf8().constData(), nullptr );
    GDALSetMetadataItem( ds, "SICNU_SAR_AZIMUTH_START_UTC",
                         QString::number( kAzStart, 'g', 17 ).toUtf8().constData(), nullptr );
    GDALSetMetadataItem( ds, "SICNU_SAR_PRF", QString::number( kPrf, 'g', 17 ).toUtf8().constData(),
                         nullptr );
    GDALSetMetadataItem( ds, "SICNU_SAR_RANGE_RATE",
                         QString::number( kRangeRate, 'g', 17 ).toUtf8().constData(), nullptr );
    GDALSetMetadataItem( ds, "SICNU_SAR_RANGE_WINDOW",
                         ( QString::number( startT, 'g', 17 ) + ";"
                           + QString::number( stopT, 'g', 17 ) )
                             .toUtf8()
                             .constData(),
                         nullptr );
    for ( const auto &item : extraMeta )
        GDALSetMetadataItem( ds, item.first.c_str(), item.second.c_str(), nullptr );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    const bool ok = GDALRasterIO( band, GF_Write, 0, 0, kSarW, kSarH,
                                  const_cast<float *>( values.data() ), kSarW, kSarH,
                                  GDT_Float32, 0, 0 ) == CE_None;
    GDALClose( ds );
    return ok;
}

/// 6x6 geographic DEM straddling the swath's leading edge.
constexpr int kDemW = 6;
constexpr int kDemH = 6;
constexpr double kDlonDeg = 0.001;
constexpr double kDlatDeg = 0.004;
constexpr double kLonStartDeg = kSceneCenterLonDeg - 0.0015;
constexpr double kLatStartDeg = 0.022;

bool writeGeoDem( const QString &path )
{
    ensureGdalInit();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
        return false;
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), kDemW, kDemH, 1,
                                  GDT_Float32, nullptr );
    if ( !ds )
        return false;
    const double gt[6] = { kLonStartDeg - 0.5 * kDlonDeg,        kDlonDeg, 0.0,
                           kLatStartDeg + 0.5 * kDlatDeg,         0.0,     -kDlatDeg };
    GDALSetGeoTransform( ds, const_cast<double *>( gt ) );
    OGRSpatialReference srs;
    if ( srs.importFromEPSG( 4326 ) == OGRERR_NONE )
    {
        char *wkt = nullptr;
        srs.exportToWkt( &wkt );
        GDALSetProjection( ds, wkt );
        CPLFree( wkt );
    }
    std::vector<float> dem( static_cast<size_t>( kDemW ) * kDemH, 100.0f );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    const bool ok = GDALRasterIO( band, GF_Write, 0, 0, kDemW, kDemH, dem.data(), kDemW, kDemH,
                                  GDT_Float32, 0, 0 ) == CE_None;
    GDALClose( ds );
    return ok;
}

Json::Value baseParams( const QString &input, const QString &output )
{
    Json::Value params( Json::objectValue );
    params["input"] = input.toStdString();
    params["output"] = output.toStdString();
    return params;
}

} // namespace

// ---------------------------------------------------------------------------
// O1 — census completeness: every registered rs:sar_* operator is classified,
// and the table carries no entry without a registered operator.
// ---------------------------------------------------------------------------

TEST_CASE( "SAR radiometric census classifies every registered rs:sar_* operator",
           "[sar][radiometry][census]" )
{
    const AppInit app;
    const std::vector<std::string> names = RSOperatorRegistry::instance().operatorNames();
    std::vector<std::string> sarIds;
    for ( const std::string &name : names )
        if ( name.rfind( "rs:sar_", 0 ) == 0 )
            sarIds.push_back( name );

    // The census is only meaningful if the registry really exposes the family.
    REQUIRE( sarIds.size() >= 20 );

    const auto &table = sarCensus();
    auto findEntry = [&]( const std::string &id ) -> const CensusEntry * {
        for ( const CensusEntry &e : table )
            if ( id == e.id )
                return &e;
        return nullptr;
    };

    for ( const std::string &id : sarIds )
    {
        const CensusEntry *entry = findEntry( id );
        if ( !entry )
        {
            FAIL( "registered operator '" << id
                  << "' has no radiometric-state census entry (rule or exemption)" );
        }
        INFO( "census entry: " << id << " kind=" << entry->kind << " rule=" << entry->rule );
        if ( std::string( entry->kind ) == "radiometric" )
            REQUIRE( std::string( entry->expectState ).size() > 0 );
        else
            REQUIRE( std::string( entry->kind ) == "exempt" );
    }
    for ( const CensusEntry &entry : table )
        REQUIRE( RSOperatorRegistry::instance().hasOperator( entry.id ) );
}

// ---------------------------------------------------------------------------
// O2 — behavioral gate: each first-class radiometric operator's OUTPUT state
// matches its census entry. Deleting any state write fails this gate.
// ---------------------------------------------------------------------------

TEST_CASE( "SAR radiometric census behavioral gate: declared state on every output",
           "[sar][radiometry][census]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    const std::vector<float> flat4( 16, 4.0f );
    const std::vector<float> flat8( 64, 0.4f );

    // ── rs:sar_calibrate: DN (declared) → sigma0 ──
    {
        const QString in = tmp.filePath( "cal_in.tif" );
        const QString out = tmp.filePath( "cal_out.tif" );
        REQUIRE( writeRasterEx( in, flat4, 4, 4,
                                { { sicnu::sar::kCalibrationKey, "dn" } } ) );
        Json::Value params = baseParams( in, out );
        params["calibrationA"] = 2.0;
        runOp( "rs:sar_calibrate", params );
        REQUIRE( metaItem( out, sicnu::sar::kCalibrationKey ) == "sigma0" );
        REQUIRE( metaItem( out, sicnu::sar::kRadiometricStateKey ) == "sigma0" );
    }

    // ── rs:sar_backscatter: sigma0 → gamma0 (constant incidence) ──
    {
        const QString in = tmp.filePath( "bs_in.tif" );
        const QString out = tmp.filePath( "bs_out.tif" );
        REQUIRE( writeRasterEx( in, flat4, 4, 4,
                                { { sicnu::sar::kCalibrationKey, "sigma0" },
                                  { sicnu::sar::kDomainKey, "linear_power" } } ) );
        Json::Value params = baseParams( in, out );
        params["fromCalibration"] = "sigma0";
        params["toCalibration"] = "gamma0";
        params["incidenceDeg"] = 30.0;
        runOp( "rs:sar_backscatter", params );
        REQUIRE( metaItem( out, sicnu::sar::kCalibrationKey ) == "gamma0" );
        REQUIRE( metaItem( out, sicnu::sar::kRadiometricStateKey ) == "gamma0" );
    }

    // ── rs:sar_speckle: sigma0 in → sigma0 out (propagation) ──
    {
        const QString in = tmp.filePath( "sp_in.tif" );
        const QString out = tmp.filePath( "sp_out.tif" );
        REQUIRE( writeRasterEx( in, flat8, 8, 8,
                                { { sicnu::sar::kCalibrationKey, "sigma0" },
                                  { sicnu::sar::kDomainKey, "linear_power" } } ) );
        Json::Value params = baseParams( in, out );
        params["method"] = "lee";
        params["kernelSize"] = 3;
        params["noiseVariance"] = 0.25;
        runOp( "rs:sar_speckle", params );
        REQUIRE( metaItem( out, sicnu::sar::kCalibrationKey ) == "sigma0" );
        REQUIRE( metaItem( out, sicnu::sar::kRadiometricStateKey ) == "sigma0" );
    }

    // ── rs:sar_terrain_flatten / rs:sar_terrain_correction: sigma0 → gamma0 ──
    for ( const char *id : { "rs:sar_terrain_flatten", "rs:sar_terrain_correction" } )
    {
        const QString tag = QString::fromLatin1( id ).mid( 8 );
        const QString in = tmp.filePath( tag + "_in.tif" );
        const QString out = tmp.filePath( tag + "_out.tif" );
        const QString dem = tmp.filePath( tag + "_dem.tif" );
        REQUIRE( writeRasterEx( in, flat4, 4, 4,
                                { { sicnu::sar::kCalibrationKey, "sigma0" },
                                  { sicnu::sar::kDomainKey, "linear_power" } } ) );
        REQUIRE( writeRasterEx( dem, std::vector<float>( 16, 100.0f ), 4, 4, {} ) );
        Json::Value params = baseParams( in, out );
        params["dem"] = dem.toStdString();
        params["incidenceDeg"] = 35.0;
        runOp( id, params );
        REQUIRE( metaItem( out, sicnu::sar::kCalibrationKey ) == "gamma0" );
        REQUIRE( metaItem( out, sicnu::sar::kRadiometricStateKey ) == "gamma0" );
    }

    // ── rs:sar_geocode: sigma0 scene → sigma0 + per-band states ──
    {
        const QString in = tmp.filePath( "geo_in.tif" );
        const QString out = tmp.filePath( "geo_out.tif" );
        const QString dem = tmp.filePath( "geo_dem.tif" );
        REQUIRE( writeSarScene( in, flat8,
                                { { sicnu::sar::kCalibrationKey, "sigma0" },
                                  { sicnu::sar::kDomainKey, "linear_power" } } ) );
        REQUIRE( writeGeoDem( dem ) );
        Json::Value params( Json::objectValue );
        params["input"] = in.toStdString();
        params["dem"] = dem.toStdString();
        params["output"] = out.toStdString();
        runOp( "rs:sar_geocode", params );
        REQUIRE( metaItem( out, sicnu::sar::kCalibrationKey ) == "sigma0" );
        REQUIRE( metaItem( out, sicnu::sar::kRadiometricStateKey ) == "sigma0" );
        REQUIRE( metaItem( out, "SICNU_SAR_GEOCODE_BAND_STATES" )
                 == "sigma0,gamma0,incidence_deg,local_incidence_deg,mask_class" );
    }

    // ── rs:sar_ratio: derived pair metric, not a backscatter calibration ──
    {
        const QString inA = tmp.filePath( "ratio_a.tif" );
        const QString inB = tmp.filePath( "ratio_b.tif" );
        const QString out = tmp.filePath( "ratio_out.tif" );
        REQUIRE( writeRasterEx( inA, std::vector<float>( 16, 0.4f ), 4, 4,
                                { { sicnu::sar::kCalibrationKey, "sigma0" },
                                  { sicnu::sar::kDomainKey, "linear_power" } } ) );
        REQUIRE( writeRasterEx( inB, std::vector<float>( 16, 0.2f ), 4, 4,
                                { { sicnu::sar::kCalibrationKey, "sigma0" },
                                  { sicnu::sar::kDomainKey, "linear_power" } } ) );
        Json::Value params( Json::objectValue );
        params["inputA"] = inA.toStdString();
        params["inputB"] = inB.toStdString();
        params["output"] = out.toStdString();
        runOp( "rs:sar_ratio", params );
        REQUIRE( metaItem( out, sicnu::sar::kRadiometricStateKey ) == "sar_pair_metric" );
        REQUIRE( metaItem( out, sicnu::sar::kCalibrationKey ) == "sar_pair_metric" );
    }

    // ── rs:sar_texture: derived texture measures, not backscatter ──
    {
        const QString in = tmp.filePath( "tex_in.tif" );
        const QString out = tmp.filePath( "tex_out.tif" );
        REQUIRE( writeRasterEx( in, flat8, 8, 8,
                                { { sicnu::sar::kCalibrationKey, "sigma0" },
                                  { sicnu::sar::kDomainKey, "linear_power" } } ) );
        Json::Value params = baseParams( in, out );
        params["windowSize"] = 3;
        params["quantLevels"] = 8;
        runOp( "rs:sar_texture", params );
        REQUIRE( metaItem( out, sicnu::sar::kRadiometricStateKey ) == "sar_texture" );
        REQUIRE( metaItem( out, sicnu::sar::kCalibrationKey ) == "sar_texture" );
    }
}

// ---------------------------------------------------------------------------
// O3 — terrain operators: declared sigma0 required (legacy undeclared warns),
// every other declared state is a typed refusal with no partial output.
// ---------------------------------------------------------------------------

TEST_CASE( "terrain operators refuse declared states other than sigma0",
           "[sar][radiometry][terrain]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    for ( const char *id : { "rs:sar_terrain_flatten", "rs:sar_terrain_correction" } )
    {
        const QString tag = QString::fromLatin1( id ).mid( 8 );
        for ( const char *declared : { "gamma0", "beta0", "dn", "sar_pair_metric", "sigm0" } )
        {
            const QString in = tmp.filePath( tag + "_" + declared + "_in.tif" );
            const QString out = tmp.filePath( tag + "_" + declared + "_out.tif" );
            const QString dem = tmp.filePath( tag + "_" + declared + "_dem.tif" );
            REQUIRE( writeRasterEx( in, std::vector<float>( 16, 0.4f ), 4, 4,
                                    { { sicnu::sar::kCalibrationKey, declared },
                                      { sicnu::sar::kDomainKey, "linear_power" } } ) );
            REQUIRE( writeRasterEx( dem, std::vector<float>( 16, 100.0f ), 4, 4, {} ) );
            Json::Value params = baseParams( in, out );
            params["dem"] = dem.toStdString();
            params["incidenceDeg"] = 35.0;
            REQUIRE_THROWS_AS( runOp( id, params ), RSOperatorError );
            REQUIRE( !QFileInfo::exists( out ) );
        }
    }
}

TEST_CASE( "terrain operators accept a legacy undeclared input with a warning",
           "[sar][radiometry][terrain]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString dem = tmp.filePath( "undeclared_dem.tif" );
    REQUIRE( writeRasterEx( dem, std::vector<float>( 16, 100.0f ), 4, 4, {} ) );

    for ( const char *id : { "rs:sar_terrain_flatten", "rs:sar_terrain_correction" } )
    {
        const QString tag = QString::fromLatin1( id ).mid( 8 );
        const QString in = tmp.filePath( tag + "_undeclared_in.tif" );
        const QString out = tmp.filePath( tag + "_undeclared_out.tif" );
        REQUIRE( writeRasterEx( in, std::vector<float>( 16, 0.4f ), 4, 4, {} ) );
        Json::Value params = baseParams( in, out );
        params["dem"] = dem.toStdString();
        params["incidenceDeg"] = 35.0;
        runOp( id, params );
        REQUIRE( metaItem( out, sicnu::sar::kRadiometricStateKey ) == "gamma0" );
        // The sigma0 assumption is persisted as machine-readable provenance so
        // downstream guards can see it (a log line does not travel with the
        // file).
        REQUIRE( metaItem( out, "SICNU_SAR_STATE_ASSUMED" ) == "sigma0_legacy_undeclared" );
        // #1165: the numeric-domain assumption travels the same way (the
        // undeclared-domain case was previously assumed linear with no flag).
        REQUIRE( metaItem( out, "SICNU_SAR_DOMAIN_ASSUMED" ) == "linear_power" );

        // A declared sigma0 input carries no assumed-state key.
        const QString declaredIn = tmp.filePath( tag + "_declared_in.tif" );
        const QString declaredOut = tmp.filePath( tag + "_declared_out.tif" );
        REQUIRE( writeRasterEx( declaredIn, std::vector<float>( 16, 0.4f ), 4, 4,
                                { { sicnu::sar::kCalibrationKey, "sigma0" } } ) );
        Json::Value declaredParams = baseParams( declaredIn, declaredOut );
        declaredParams["dem"] = dem.toStdString();
        declaredParams["incidenceDeg"] = 35.0;
        runOp( id, declaredParams );
        REQUIRE( metaItem( declaredOut, sicnu::sar::kRadiometricStateKey ) == "gamma0" );
        REQUIRE( metaItem( declaredOut, "SICNU_SAR_STATE_ASSUMED" ).empty() );
        // The calibration is declared but the numeric DOMAIN is not — the
        // linear-power assumption is still persisted honestly (#1165).
        REQUIRE( metaItem( declaredOut, "SICNU_SAR_DOMAIN_ASSUMED" ) == "linear_power" );
    }
}

// ---------------------------------------------------------------------------
// O4 — geocode: declared sigma0 required; conflicting declarations refuse;
// the output carries the full state block and reopens lossless.
// ---------------------------------------------------------------------------

TEST_CASE( "geocode refuses declared states other than sigma0 and conflicting metadata",
           "[sar][radiometry][geocode]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString dem = tmp.filePath( "dem.tif" );
    REQUIRE( writeGeoDem( dem ) );

    for ( const char *declared : { "gamma0", "beta0", "dn" } )
    {
        const QString in =
            tmp.filePath( QStringLiteral( "geo_%1_in.tif" ).arg( QString::fromLatin1( declared ) ) );
        const QString out =
            tmp.filePath( QStringLiteral( "geo_%1_out.tif" ).arg( QString::fromLatin1( declared ) ) );
        REQUIRE( writeSarScene( in, std::vector<float>( 64, 0.4f ),
                                { { sicnu::sar::kCalibrationKey, declared } } ) );
        Json::Value params( Json::objectValue );
        params["input"] = in.toStdString();
        params["dem"] = dem.toStdString();
        params["output"] = out.toStdString();
        REQUIRE_THROWS_AS( runOp( "rs:sar_geocode", params ), RSOperatorError );
        REQUIRE( !QFileInfo::exists( out ) );
    }

    // Conflicting declarations (calibration token vs radiometric-state token)
    // are a typed refusal across the radiometric family, never a guess.
    {
        const QString in = tmp.filePath( "conflict_in.tif" );
        const QString out = tmp.filePath( "conflict_out.tif" );
        REQUIRE( writeSarScene( in, std::vector<float>( 64, 0.4f ),
                                { { sicnu::sar::kCalibrationKey, "sigma0" },
                                  { sicnu::sar::kRadiometricStateKey, "gamma0" } } ) );
        Json::Value params( Json::objectValue );
        params["input"] = in.toStdString();
        params["dem"] = dem.toStdString();
        params["output"] = out.toStdString();
        REQUIRE_THROWS_AS( runOp( "rs:sar_geocode", params ), RSOperatorError );
        REQUIRE( !QFileInfo::exists( out ) );
    }

    // The legacy undeclared path still geocodes, and persists the sigma0
    // assumption as machine-readable provenance.
    {
        const QString in = tmp.filePath( "geo_undeclared_in.tif" );
        const QString out = tmp.filePath( "geo_undeclared_out.tif" );
        REQUIRE( writeSarScene( in, std::vector<float>( 64, 0.4f ), {} ) );
        Json::Value params( Json::objectValue );
        params["input"] = in.toStdString();
        params["dem"] = dem.toStdString();
        params["output"] = out.toStdString();
        runOp( "rs:sar_geocode", params );
        REQUIRE( metaItem( out, sicnu::sar::kRadiometricStateKey ) == "sigma0" );
        REQUIRE( metaItem( out, "SICNU_SAR_STATE_ASSUMED" ) == "sigma0_legacy_undeclared" );
    }

    // A declared dB scene is linear-power-only territory for these products.
    {
        const QString in = tmp.filePath( "db_in.tif" );
        const QString out = tmp.filePath( "db_out.tif" );
        REQUIRE( writeSarScene( in, std::vector<float>( 64, 0.4f ),
                                { { sicnu::sar::kCalibrationKey, "sigma0" },
                                  { sicnu::sar::kDomainKey, "db" } } ) );
        Json::Value params( Json::objectValue );
        params["input"] = in.toStdString();
        params["dem"] = dem.toStdString();
        params["output"] = out.toStdString();
        REQUIRE_THROWS_AS( runOp( "rs:sar_geocode", params ), RSOperatorError );
        REQUIRE( !QFileInfo::exists( out ) );
    }
}

// ---------------------------------------------------------------------------
// O5 — derived products cannot be re-calibrated or converted.
// ---------------------------------------------------------------------------

TEST_CASE( "derived ratio/texture products are refused by calibrate and backscatter",
           "[sar][radiometry][derived]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    const QString ratio = tmp.filePath( "derived_ratio.tif" );
    REQUIRE( writeRasterEx( ratio, std::vector<float>( 16, 1.0f ), 4, 4,
                            { { sicnu::sar::kCalibrationKey, "sar_pair_metric" },
                              { sicnu::sar::kRadiometricStateKey, "sar_pair_metric" } } ) );
    const QString texture = tmp.filePath( "derived_texture.tif" );
    REQUIRE( writeRasterEx( texture, std::vector<float>( 16, 1.0f ), 4, 4,
                            { { sicnu::sar::kCalibrationKey, "sar_texture" },
                              { sicnu::sar::kRadiometricStateKey, "sar_texture" } } ) );

    for ( const QString &in : { ratio, texture } )
    {
        const QString outCal = tmp.filePath( QFileInfo( in ).fileName() + ".cal.tif" );
        Json::Value calParams = baseParams( in, outCal );
        calParams["calibrationA"] = 2.0;
        // The refusal must name the derived-product class, not the generic
        // "unrecognized token" text (message quality is part of the contract).
        REQUIRE_THROWS_WITH( runOp( "rs:sar_calibrate", calParams ),
                             Catch::Matchers::ContainsSubstring( "derived SAR product" ) );
        REQUIRE( !QFileInfo::exists( outCal ) );

        const QString outBs = tmp.filePath( QFileInfo( in ).fileName() + ".bs.tif" );
        Json::Value bsParams = baseParams( in, outBs );
        bsParams["fromCalibration"] = "sigma0";
        bsParams["toCalibration"] = "gamma0";
        bsParams["incidenceDeg"] = 30.0;
        REQUIRE_THROWS_AS( runOp( "rs:sar_backscatter", bsParams ), RSOperatorError );
        REQUIRE( !QFileInfo::exists( outBs ) );
    }
}

TEST_CASE( "ratio refuses scenes declaring different radiometric states",
           "[sar][radiometry][derived]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString a = tmp.filePath( "mix_a.tif" );
    const QString b = tmp.filePath( "mix_b.tif" );
    const QString out = tmp.filePath( "mix_out.tif" );
    REQUIRE( writeRasterEx( a, std::vector<float>( 16, 0.4f ), 4, 4,
                            { { sicnu::sar::kCalibrationKey, "sigma0" } } ) );
    REQUIRE( writeRasterEx( b, std::vector<float>( 16, 0.2f ), 4, 4,
                            { { sicnu::sar::kCalibrationKey, "gamma0" } } ) );
    Json::Value params( Json::objectValue );
    params["inputA"] = a.toStdString();
    params["inputB"] = b.toStdString();
    params["output"] = out.toStdString();
    REQUIRE_THROWS_AS( runOp( "rs:sar_ratio", params ), RSOperatorError );
    REQUIRE( !QFileInfo::exists( out ) );
}

TEST_CASE( "ratio refuses conflicting and derived input declarations",
           "[sar][radiometry][derived]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    // Conflicting declarations on BOTH inputs: both effective tokens are
    // unreadable (""), so only the explicit conflict check can refuse — the
    // mismatch check alone would see "" == "" and pass.
    {
        const QString a = tmp.filePath( "conf_a.tif" );
        const QString b = tmp.filePath( "conf_b.tif" );
        const QString out = tmp.filePath( "conf_out.tif" );
        REQUIRE( writeRasterEx( a, std::vector<float>( 16, 0.4f ), 4, 4,
                                { { sicnu::sar::kCalibrationKey, "sigma0" },
                                  { sicnu::sar::kRadiometricStateKey, "gamma0" } } ) );
        REQUIRE( writeRasterEx( b, std::vector<float>( 16, 0.2f ), 4, 4,
                                { { sicnu::sar::kCalibrationKey, "sigma0" },
                                  { sicnu::sar::kRadiometricStateKey, "beta0" } } ) );
        Json::Value params( Json::objectValue );
        params["inputA"] = a.toStdString();
        params["inputB"] = b.toStdString();
        params["output"] = out.toStdString();
        REQUIRE_THROWS_AS( runOp( "rs:sar_ratio", params ), RSOperatorError );
        REQUIRE( !QFileInfo::exists( out ) );
    }

    // Derived inputs: a ratio of pair metrics / texture measures is undefined.
    for ( const char *derived : { "sar_pair_metric", "sar_texture" } )
    {
        const QString a = tmp.filePath( QStringLiteral( "der_a_%1.tif" ).arg( QString::fromLatin1( derived ) ) );
        const QString b = tmp.filePath( QStringLiteral( "der_b_%1.tif" ).arg( QString::fromLatin1( derived ) ) );
        const QString out = tmp.filePath( QStringLiteral( "der_out_%1.tif" ).arg( QString::fromLatin1( derived ) ) );
        REQUIRE( writeRasterEx( a, std::vector<float>( 16, 1.0f ), 4, 4,
                                { { sicnu::sar::kCalibrationKey, derived },
                                  { sicnu::sar::kRadiometricStateKey, derived } } ) );
        REQUIRE( writeRasterEx( b, std::vector<float>( 16, 2.0f ), 4, 4,
                                { { sicnu::sar::kCalibrationKey, derived },
                                  { sicnu::sar::kRadiometricStateKey, derived } } ) );
        Json::Value params( Json::objectValue );
        params["inputA"] = a.toStdString();
        params["inputB"] = b.toStdString();
        params["output"] = out.toStdString();
        REQUIRE_THROWS_AS( runOp( "rs:sar_ratio", params ), RSOperatorError );
        REQUIRE( !QFileInfo::exists( out ) );
    }

    // An unrecognized token is named as such, not silently "undeclared".
    {
        const QString a = tmp.filePath( "unk_a.tif" );
        const QString b = tmp.filePath( "unk_b.tif" );
        const QString out = tmp.filePath( "unk_out.tif" );
        REQUIRE( writeRasterEx( a, std::vector<float>( 16, 0.4f ), 4, 4,
                                { { sicnu::sar::kCalibrationKey, "sigma0" } } ) );
        REQUIRE( writeRasterEx( b, std::vector<float>( 16, 0.2f ), 4, 4,
                                { { sicnu::sar::kCalibrationKey, "sigm0" } } ) );
        Json::Value params( Json::objectValue );
        params["inputA"] = a.toStdString();
        params["inputB"] = b.toStdString();
        params["output"] = out.toStdString();
        REQUIRE_THROWS_AS( runOp( "rs:sar_ratio", params ), RSOperatorError );
        REQUIRE( !QFileInfo::exists( out ) );
    }
}

// ---------------------------------------------------------------------------
// O6 — DN LUT calibration: hand-computed per-row oracle + fail-closed refusals.
// ---------------------------------------------------------------------------

TEST_CASE( "calibrate applies a per-row LUT with hand-computed pixel values",
           "[sar][radiometry][lut]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    // 4 rows; DN = 10·(row+1). LUT A = [2, 4, 5, 10].
    // sigma0 = DN²/A²  →  row0 100/4 = 25, row1 400/16 = 25,
    //                      row2 900/25 = 36, row3 1600/100 = 16.
    const float expected[4] = { 25.0f, 25.0f, 36.0f, 16.0f };
    std::vector<float> dn( 16 );
    for ( int row = 0; row < 4; ++row )
        for ( int col = 0; col < 4; ++col )
            dn[static_cast<size_t>( row ) * 4 + col] = static_cast<float>( 10 * ( row + 1 ) );
    const QString in = tmp.filePath( "lut_in.tif" );
    const QString lut = tmp.filePath( "lut.txt" );
    const QString out = tmp.filePath( "lut_out.tif" );
    REQUIRE( writeRasterEx( in, dn, 4, 4, {} ) );
    REQUIRE( writeTextFile( lut, QStringLiteral( "2\n4\n5\n10\n" ) ) );

    Json::Value params = baseParams( in, out );
    params["calibrationA"] = 2.0; // inert on the LUT path
    params["calibrationLut"] = lut.toStdString();
    runOp( "rs:sar_calibrate", params );

    // The same LUT without a trailing newline must parse identically.
    const QString lutNoEol = tmp.filePath( "lut_no_eol.txt" );
    const QString outNoEol = tmp.filePath( "lut_no_eol_out.tif" );
    REQUIRE( writeTextFile( lutNoEol, QStringLiteral( "2\n4\n5\n10" ) ) );
    Json::Value noEolParams = baseParams( in, outNoEol );
    noEolParams["calibrationLut"] = lutNoEol.toStdString();
    runOp( "rs:sar_calibrate", noEolParams );
    const std::vector<float> sigma0NoEol = readBand( outNoEol );
    REQUIRE( sigma0NoEol.size() == 16 );
    for ( int row = 0; row < 4; ++row )
        for ( int col = 0; col < 4; ++col )
            REQUIRE( sigma0NoEol[static_cast<size_t>( row ) * 4 + col]
                     == Approx( expected[row] ).margin( 1e-5 ) );

    const std::vector<float> sigma0 = readBand( out );
    REQUIRE( sigma0.size() == 16 );
    for ( int row = 0; row < 4; ++row )
        for ( int col = 0; col < 4; ++col )
            REQUIRE( sigma0[static_cast<size_t>( row ) * 4 + col] == Approx( expected[row] ).margin( 1e-5 ) );
    REQUIRE( metaItem( out, sicnu::sar::kRadiometricStateKey ) == "sigma0" );
}

TEST_CASE( "calibrate LUT noise subtraction uses the same per-row constant",
           "[sar][radiometry][lut]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    // DN = 10, A = 2, noise = 50 → (100 − 50)/4 = 12.5.
    const QString in = tmp.filePath( "lut_noise_in.tif" );
    const QString lut = tmp.filePath( "lut_noise.txt" );
    const QString out = tmp.filePath( "lut_noise_out.tif" );
    REQUIRE( writeRasterEx( in, std::vector<float>( 16, 10.0f ), 4, 4, {} ) );
    REQUIRE( writeTextFile( lut, QStringLiteral( "2\n2\n2\n2\n" ) ) );
    Json::Value params = baseParams( in, out );
    params["calibrationA"] = 2.0;
    params["noiseLinear"] = 50.0;
    params["calibrationLut"] = lut.toStdString();
    runOp( "rs:sar_calibrate", params );
    for ( float v : readBand( out ) )
        REQUIRE( v == Approx( 12.5f ).margin( 1e-5 ) );
}

TEST_CASE( "calibrate refuses missing, malformed or mismatched LUTs",
           "[sar][radiometry][lut]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString in = tmp.filePath( "bad_lut_in.tif" );
    REQUIRE( writeRasterEx( in, std::vector<float>( 16, 10.0f ), 4, 4, {} ) );

    const QString missing = tmp.filePath( "does_not_exist.txt" );
    const QString wrongCount = tmp.filePath( "wrong_count.txt" );
    const QString garbage = tmp.filePath( "garbage.txt" );
    const QString nonPositive = tmp.filePath( "nonpositive.txt" );
    const QString empty = tmp.filePath( "empty.txt" );
    REQUIRE( writeTextFile( wrongCount, QStringLiteral( "2\n2\n2\n" ) ) );
    REQUIRE( writeTextFile( garbage, QStringLiteral( "2\nabc\n2\n2\n" ) ) );
    REQUIRE( writeTextFile( nonPositive, QStringLiteral( "2\n0\n2\n2\n" ) ) );
    REQUIRE( writeTextFile( empty, QStringLiteral( "\n" ) ) );

    int caseIndex = 0;
    for ( const QString &lut : { missing, wrongCount, garbage, nonPositive, empty } )
    {
        const QString out = tmp.filePath( QString( "bad_lut_out_%1.tif" ).arg( caseIndex++ ) );
        Json::Value params = baseParams( in, out );
        params["calibrationA"] = 2.0;
        params["calibrationLut"] = lut.toStdString();
        REQUIRE_THROWS_AS( runOp( "rs:sar_calibrate", params ), RSOperatorError );
        REQUIRE( !QFileInfo::exists( out ) );
    }
}

TEST_CASE( "calibrate reads a declared LUT path and refuses when it is missing",
           "[sar][radiometry][lut]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString in = tmp.filePath( "declared_lut_in.tif" );
    const QString lut = tmp.filePath( "declared_lut.txt" );
    const QString out = tmp.filePath( "declared_lut_out.tif" );
    REQUIRE( writeTextFile( lut, QStringLiteral( "2\n2\n2\n2\n" ) ) );
    REQUIRE( writeRasterEx( in, std::vector<float>( 16, 10.0f ), 4, 4,
                            { { "SICNU_SAR_CALIBRATION_LUT", "declared_lut.txt" } } ) );
    Json::Value params = baseParams( in, out );
    params["calibrationA"] = 2.0;
    runOp( "rs:sar_calibrate", params );
    for ( float v : readBand( out ) )
        REQUIRE( v == Approx( 25.0f ).margin( 1e-5 ) );

    // Declared but unreadable: typed refusal, no constant-A fallback.
    const QString broken = tmp.filePath( "declared_lut_broken_in.tif" );
    const QString brokenOut = tmp.filePath( "declared_lut_broken_out.tif" );
    REQUIRE( writeRasterEx( broken, std::vector<float>( 16, 10.0f ), 4, 4,
                            { { "SICNU_SAR_CALIBRATION_LUT", "vanished.txt" } } ) );
    Json::Value brokenParams = baseParams( broken, brokenOut );
    brokenParams["calibrationA"] = 2.0;
    REQUIRE_THROWS_AS( runOp( "rs:sar_calibrate", brokenParams ), RSOperatorError );
    REQUIRE( !QFileInfo::exists( brokenOut ) );
}

TEST_CASE( "calibrate LUT resolution: precedence, containment and count direction",
           "[sar][radiometry][lut]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    // A subdirectory keeps the absolute-declared-path case inside the raster's
    // directory (containment is against traversal, not against subdirectories).
    REQUIRE( QDir( tmp.path() ).mkdir( "sidecar" ) );
    const QString subLut = tmp.filePath( "sidecar/abs_lut.txt" );
    REQUIRE( writeTextFile( subLut, QStringLiteral( "2\n2\n2\n2\n" ) ) );

    // Declared ABSOLUTE path inside the raster directory resolves.
    {
        const QString in = tmp.filePath( "abs_in.tif" );
        const QString out = tmp.filePath( "abs_out.tif" );
        REQUIRE( writeRasterEx( in, std::vector<float>( 16, 10.0f ), 4, 4,
                                { { "SICNU_SAR_CALIBRATION_LUT", subLut.toStdString() } } ) );
        Json::Value params = baseParams( in, out );
        runOp( "rs:sar_calibrate", params );
        for ( float v : readBand( out ) )
            REQUIRE( v == Approx( 25.0f ).margin( 1e-5 ) );
    }

    // The explicit parameter overrides the declared sidecar.
    {
        const QString in = tmp.filePath( "prec_in.tif" );
        const QString out = tmp.filePath( "prec_out.tif" );
        const QString paramLut = tmp.filePath( "prec_lut.txt" );
        REQUIRE( writeTextFile( paramLut, QStringLiteral( "5\n5\n5\n5\n" ) ) );
        // A declared sidecar with a DIFFERENT constant: if the parameter did
        // not win, the pixels would be 100/4 = 25 instead of 100/25 = 4.
        REQUIRE( writeRasterEx( in, std::vector<float>( 16, 10.0f ), 4, 4,
                                { { "SICNU_SAR_CALIBRATION_LUT", "declared_lut.txt" } } ) );
        Json::Value params = baseParams( in, out );
        params["calibrationLut"] = paramLut.toStdString();
        runOp( "rs:sar_calibrate", params );
        for ( float v : readBand( out ) )
            REQUIRE( v == Approx( 4.0f ).margin( 1e-5 ) );
    }

    // Declared path escaping the raster's directory is refused (traversal).
    // The raster lives in a subdirectory and the outside LUT EXISTS with a
    // valid row-exact content, so without the containment check the run would
    // succeed — the refusal can only come from the containment rule.
    {
        REQUIRE( QDir( tmp.path() ).mkdir( "product" ) );
        const QString outsideLut = tmp.filePath( "outside_lut.txt" );
        REQUIRE( writeTextFile( outsideLut, QStringLiteral( "2\n2\n2\n2\n" ) ) );
        const QString in = tmp.filePath( "product/escape_in.tif" );
        const QString out = tmp.filePath( "product/escape_out.tif" );
        REQUIRE( writeRasterEx( in, std::vector<float>( 16, 10.0f ), 4, 4,
                                { { "SICNU_SAR_CALIBRATION_LUT",
                                    "../outside_lut.txt" } } ) );
        Json::Value params = baseParams( in, out );
        REQUIRE_THROWS_AS( runOp( "rs:sar_calibrate", params ), RSOperatorError );
        REQUIRE( !QFileInfo::exists( out ) );
    }

    // #1164: a symlink INSIDE the raster's directory pointing at an outside
    // LUT is lexically contained but canonically outside — the containment
    // rule must refuse it (the pre-fix || accepted the traversal).
    {
        REQUIRE( QDir( tmp.path() ).mkdir( "symlinked" ) );
        const QString outsideLut = tmp.filePath( "symlink_target_lut.txt" );
        REQUIRE( writeTextFile( outsideLut, QStringLiteral( "2\n2\n2\n2\n" ) ) );
        const QString in = tmp.filePath( "symlinked/sym_in.tif" );
        const QString out = tmp.filePath( "symlinked/sym_out.tif" );
        REQUIRE( writeRasterEx( in, std::vector<float>( 16, 10.0f ), 4, 4,
                                { { "SICNU_SAR_CALIBRATION_LUT", "lut_link.txt" } } ) );
        REQUIRE( QFile::link( outsideLut, tmp.filePath( "symlinked/lut_link.txt" ) ) );
        Json::Value params = baseParams( in, out );
        REQUIRE_THROWS_AS( runOp( "rs:sar_calibrate", params ), RSOperatorError );
        REQUIRE( !QFileInfo::exists( out ) );
    }

    // More values than rows is refused (no silent truncation).
    {
        const QString in = tmp.filePath( "more_in.tif" );
        const QString out = tmp.filePath( "more_out.tif" );
        const QString lut = tmp.filePath( "more_lut.txt" );
        REQUIRE( writeTextFile( lut, QStringLiteral( "2\n2\n2\n2\n2\n" ) ) );
        REQUIRE( writeRasterEx( in, std::vector<float>( 16, 10.0f ), 4, 4, {} ) );
        Json::Value params = baseParams( in, out );
        params["calibrationLut"] = lut.toStdString();
        REQUIRE_THROWS_AS( runOp( "rs:sar_calibrate", params ), RSOperatorError );
        REQUIRE( !QFileInfo::exists( out ) );
    }

    // A 0-byte LUT is refused (row count 0 != raster rows).
    {
        const QString in = tmp.filePath( "zero_in.tif" );
        const QString out = tmp.filePath( "zero_out.tif" );
        const QString lut = tmp.filePath( "zero_lut.txt" );
        REQUIRE( writeTextFile( lut, QString() ) );
        REQUIRE( writeRasterEx( in, std::vector<float>( 16, 10.0f ), 4, 4, {} ) );
        Json::Value params = baseParams( in, out );
        params["calibrationLut"] = lut.toStdString();
        REQUIRE_THROWS_AS( runOp( "rs:sar_calibrate", params ), RSOperatorError );
        REQUIRE( !QFileInfo::exists( out ) );
    }
}

// ---------------------------------------------------------------------------
// O7 — end-to-end chain provenance: import → calibrate → speckle → terrain →
// geocode → derived, with state transitions, refusals and readback.
// ---------------------------------------------------------------------------

TEST_CASE( "end-to-end SAR chain preserves and transitions radiometric state",
           "[sar][radiometry][e2e]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    // ── import: two DN scenes, 4x4, undeclared (legacy DN products) ──
    std::vector<float> dnA( 16, 0.0f );
    std::vector<float> dnB( 16, 0.0f );
    for ( int row = 0; row < 4; ++row )
        for ( int col = 0; col < 4; ++col )
        {
            dnA[static_cast<size_t>( row ) * 4 + col] = 0.10f + 0.01f * row + 0.005f * col;
            dnB[static_cast<size_t>( row ) * 4 + col] = 0.20f + 0.02f * row;
        }
    const QString importA = tmp.filePath( "import_a.tif" );
    const QString importB = tmp.filePath( "import_b.tif" );
    REQUIRE( writeRasterEx( importA, dnA, 4, 4, {} ) );
    REQUIRE( writeRasterEx( importB, dnB, 4, 4, {} ) );

    // ── calibrate: DN → sigma0 (constant A on both scenes) ──
    auto calibrateScene = [&]( const QString &in, const QString &out ) {
        Json::Value params = baseParams( in, out );
        params["calibrationA"] = 3.0;
        runOp( "rs:sar_calibrate", params );
    };
    const QString sigmaA = tmp.filePath( "chain_sigma_a.tif" );
    const QString sigmaB = tmp.filePath( "chain_sigma_b.tif" );
    calibrateScene( importA, sigmaA );
    calibrateScene( importB, sigmaB );
    REQUIRE( metaItem( sigmaA, sicnu::sar::kCalibrationKey ) == "sigma0" );
    REQUIRE( metaItem( sigmaA, sicnu::sar::kRadiometricStateKey ) == "sigma0" );

    // Double calibration fails closed on the chain product.
    const QString doubleCal = tmp.filePath( "chain_double_calibrate.tif" );
    REQUIRE_THROWS_AS( calibrateScene( sigmaA, doubleCal ), RSOperatorError );
    REQUIRE( !QFileInfo::exists( doubleCal ) );

    // ── speckle: state propagates through the filter ──
    const QString speckleA = tmp.filePath( "chain_speckle_a.tif" );
    {
        Json::Value params = baseParams( sigmaA, speckleA );
        params["method"] = "lee";
        params["kernelSize"] = 3;
        params["noiseVariance"] = 0.25;
        runOp( "rs:sar_speckle", params );
    }
    REQUIRE( metaItem( speckleA, sicnu::sar::kRadiometricStateKey ) == "sigma0" );

    // ── terrain: sigma0 → gamma0 (flat DEM: identity factor) ──
    auto flattenScene = [&]( const QString &in, const QString &out ) {
        const QString dem = out + ".dem.tif";
        REQUIRE( writeRasterEx( dem, std::vector<float>( 16, 100.0f ), 4, 4, {} ) );
        Json::Value params = baseParams( in, out );
        params["dem"] = dem.toStdString();
        params["incidenceDeg"] = 35.0;
        runOp( "rs:sar_terrain_flatten", params );
    };
    const QString gammaA = tmp.filePath( "chain_gamma_a.tif" );
    const QString gammaB = tmp.filePath( "chain_gamma_b.tif" );
    flattenScene( speckleA, gammaA );
    flattenScene( sigmaB, gammaB );
    REQUIRE( metaItem( gammaA, sicnu::sar::kRadiometricStateKey ) == "gamma0" );

    // geocode on the gamma0 product is a typed refusal: its gamma0 band
    // applies sin(thetaL)/sin(theta0) to sigma0, so a declared gamma0 input
    // would receive a second terrain factor. (rs:sar_backscatter's gamma0 is a
    // different product — a pure geometric normalization — and must be
    // converted back with gamma0ToSigma0 before geocoding; the refusal is
    // fail-closed for every gamma0 flavor.)
    {
        const QString dem = tmp.filePath( "chain_geo_dem.tif" );
        REQUIRE( writeGeoDem( dem ) );
        const QString in8 = tmp.filePath( "chain_gamma_8.tif" );
        REQUIRE( writeSarScene( in8, std::vector<float>( 64, 0.4f ),
                                { { sicnu::sar::kCalibrationKey, "gamma0" } } ) );
        const QString out8 = tmp.filePath( "chain_gamma_8_geo.tif" );
        Json::Value params( Json::objectValue );
        params["input"] = in8.toStdString();
        params["dem"] = dem.toStdString();
        params["output"] = out8.toStdString();
        REQUIRE_THROWS_AS( runOp( "rs:sar_geocode", params ), RSOperatorError );
        REQUIRE( !QFileInfo::exists( out8 ) );
    }

    // ── geocode branch: the sigma0 product geocodes with per-band states ──
    {
        const QString dem = tmp.filePath( "chain_geo_dem2.tif" );
        REQUIRE( writeGeoDem( dem ) );
        const QString in8 = tmp.filePath( "chain_sigma_8.tif" );
        REQUIRE( writeSarScene( in8, std::vector<float>( 64, 0.4f ),
                                { { sicnu::sar::kCalibrationKey, "sigma0" },
                                  { sicnu::sar::kDomainKey, "linear_power" } } ) );
        const QString out8 = tmp.filePath( "chain_sigma_8_geo.tif" );
        Json::Value params( Json::objectValue );
        params["input"] = in8.toStdString();
        params["dem"] = dem.toStdString();
        params["output"] = out8.toStdString();
        runOp( "rs:sar_geocode", params );
        REQUIRE( metaItem( out8, sicnu::sar::kRadiometricStateKey ) == "sigma0" );
        REQUIRE( metaItem( out8, "SICNU_SAR_GEOCODE_BAND_STATES" )
                 == "sigma0,gamma0,incidence_deg,local_incidence_deg,mask_class" );
    }

    // ── derived: ratio over the two gamma0 scenes declares the derived token ──
    const QString pairMetric = tmp.filePath( "chain_pair_metric.tif" );
    {
        Json::Value params( Json::objectValue );
        params["inputA"] = gammaA.toStdString();
        params["inputB"] = gammaB.toStdString();
        params["output"] = pairMetric.toStdString();
        runOp( "rs:sar_ratio", params );
    }
    REQUIRE( metaItem( pairMetric, sicnu::sar::kRadiometricStateKey ) == "sar_pair_metric" );

    // ── readback: every chain output reopens with its state intact ──
    struct Expected
    {
        const char *path;
        const char *state;
    };
    const Expected expected[] = {
        { "chain_sigma_a.tif", "sigma0" },     { "chain_sigma_b.tif", "sigma0" },
        { "chain_speckle_a.tif", "sigma0" },   { "chain_gamma_a.tif", "gamma0" },
        { "chain_gamma_b.tif", "gamma0" },     { "chain_sigma_8_geo.tif", "sigma0" },
        { "chain_pair_metric.tif", "sar_pair_metric" },
    };
    for ( const Expected &e : expected )
    {
        const QString path = tmp.filePath( QString::fromLatin1( e.path ) );
        INFO( "readback " << e.path );
        REQUIRE( QFileInfo::exists( path ) );
        REQUIRE( metaItem( path, sicnu::sar::kRadiometricStateKey ) == e.state );
        REQUIRE( metaItem( path, sicnu::sar::kCalibrationKey ) == e.state );
    }
}

TEST_CASE( "calibrate and backscatter refuse a declared SICNU_SAR_DOMAIN=db input",
           "[sar][radiometry][domain]" )
{
    // #1147: the two entry-point operators of the radiometric chain used to
    // ignore the declared numeric domain — a dB product sailed through the
    // linear-power kernels with an exponentially wrong, confidently declared
    // linear_power output. The declared db must be a typed refusal exactly
    // like the six downstream family operators.
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    // Produce a genuine declared-dB product through the chain's own writer:
    // calibrate with outputDomain=db stamps SICNU_SAR_DOMAIN=db.
    const QString dn = tmp.filePath( "db_dn.tif" );
    REQUIRE( writeRasterEx( dn, std::vector<float>( 16, 100.0f ), 4, 4, {} ) );
    const QString dbProduct = tmp.filePath( "cal_db.tif" );
    {
        Json::Value params = baseParams( dn, dbProduct );
        params["calibrationA"] = 2.0;
        params["outputDomain"] = "db";
        runOp( "rs:sar_calibrate", params );
    }
    REQUIRE( metaItem( dbProduct, sicnu::sar::kDomainKey ) == "db" );
    REQUIRE( metaItem( dbProduct, sicnu::sar::kCalibrationKey ) == "sigma0" );

    // backscatter sigma0→gamma0 with the DEFAULT inputDomain=linear_power:
    // the declared db must refuse before any output exists.
    const QString outBs = tmp.filePath( "bs_from_db.tif" );
    {
        Json::Value params = baseParams( dbProduct, outBs );
        params["fromCalibration"] = "sigma0";
        params["toCalibration"] = "gamma0";
        params["incidenceDeg"] = 30.0;
        REQUIRE_THROWS_WITH( runOp( "rs:sar_backscatter", params ),
                             Catch::Matchers::ContainsSubstring( "SICNU_SAR_DOMAIN=db" ) );
    }
    REQUIRE( !QFileInfo::exists( outBs ) );

    // calibrate on the declared-dB product: the DN formula is linear-only.
    const QString outCal = tmp.filePath( "cal_from_db.tif" );
    {
        Json::Value params = baseParams( dbProduct, outCal );
        params["calibrationA"] = 2.0;
        REQUIRE_THROWS_WITH( runOp( "rs:sar_calibrate", params ),
                             Catch::Matchers::ContainsSubstring( "SICNU_SAR_DOMAIN=db" ) );
    }
    REQUIRE( !QFileInfo::exists( outCal ) );

    // The lawful path stays open: inputDomain=db + same-state conversion is
    // the pure numeric dB→linear step (declared db agrees with the param).
    const QString outLinear = tmp.filePath( "linear_from_db.tif" );
    {
        Json::Value params = baseParams( dbProduct, outLinear );
        params["fromCalibration"] = "sigma0";
        params["toCalibration"] = "sigma0";
        params["inputDomain"] = "db";
        runOp( "rs:sar_backscatter", params );
    }
    REQUIRE( QFileInfo::exists( outLinear ) );
    REQUIRE( metaItem( outLinear, sicnu::sar::kDomainKey ) == "linear_power" );
}


TEST_CASE( "rs:sar_ratio consumes the terrain family's assumption provenance (#1165)",
           "[sar][radiometry][assumed]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString dem = tmp.filePath( "assume_dem.tif" );
    REQUIRE( writeRasterEx( dem, std::vector<float>( 16, 100.0f ), 4, 4, {} ) );

    // Legacy undeclared raster through flatten: the output carries BOTH the
    // positive gamma0 declaration and the assumption provenance.
    const QString flatIn = tmp.filePath( "assume_in.tif" );
    const QString flatOut = tmp.filePath( "assume_flat.tif" );
    REQUIRE( writeRasterEx( flatIn, std::vector<float>( 16, 0.4f ), 4, 4, {} ) );
    {
        Json::Value params = baseParams( flatIn, flatOut );
        params["dem"] = dem.toStdString();
        params["incidenceDeg"] = 35.0;
        runOp( "rs:sar_terrain_flatten", params );
    }
    REQUIRE( metaItem( flatOut, sicnu::sar::kRadiometricStateKey ) == "gamma0" );
    REQUIRE( metaItem( flatOut, "SICNU_SAR_STATE_ASSUMED" ) == "sigma0_legacy_undeclared" );
    REQUIRE( metaItem( flatOut, "SICNU_SAR_DOMAIN_ASSUMED" ) == "linear_power" );

    // A declared gamma0 partner: same recognized state, the ratio RUNS —
    // and inherits + propagates the assumption (the key finally has a
    // reader; the warning travels with the artifact).
    const QString partner = tmp.filePath( "assume_partner.tif" );
    REQUIRE( writeRasterEx( partner, std::vector<float>( 16, 0.4f ), 4, 4,
                            { { sicnu::sar::kCalibrationKey, "gamma0" } } ) );
    const QString ratioOut = tmp.filePath( "assume_ratio.tif" );
    {
        Json::Value params( Json::objectValue );
        params["inputA"] = flatOut.toStdString();
        params["inputB"] = partner.toStdString();
        params["output"] = ratioOut.toStdString();
        runOp( "rs:sar_ratio", params );
    }
    REQUIRE( QFileInfo::exists( ratioOut ) );
    REQUIRE_FALSE( metaItem( ratioOut, "SICNU_SAR_STATE_ASSUMED" ).empty() );
    REQUIRE( metaItem( ratioOut, "SICNU_SAR_STATE_ASSUMED" ).find( "sigma0_legacy_undeclared" )
             != std::string::npos );
    REQUIRE( metaItem( ratioOut, "SICNU_SAR_STATE_ASSUMED" ).find( "linear_power" )
             != std::string::npos );
}
