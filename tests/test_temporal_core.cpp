// tests/test_temporal_core.cpp — temporal core: time semantics, collection
// ordering/persistence, scientific preflight, streaming reader (goal §42–§48).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QTemporaryDir>

#include <json/json.h>

#include <gdal_priv.h>

#include <cmath>
#include <limits>

#include "operators/framework/rs_operator_context.h"
#include "processing/algorithms/temporal/temporal_band_roles.h"
#include "processing/algorithms/temporal/temporal_collection.h"
#include "processing/algorithms/temporal/temporal_preflight.h"
#include "processing/algorithms/temporal/temporal_stats.h"
#include "processing/algorithms/temporal/temporal_stream.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

using Catch::Approx;
using namespace sicnu::temporal;

namespace
{

int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_temporal_core";
char *appArgv[] = {appArgv0, nullptr};

void ensureApp()
{
    if ( !QCoreApplication::instance() )
        new QCoreApplication( appArgc(), appArgv );
}

constexpr float kNan = std::numeric_limits<float>::quiet_NaN();

/// Synthetic scene writer: Float32 raster + acquisition/radiometric/role
/// metadata + optional band scale/offset and NoData.
struct SceneSpec
{
    QString path;
    std::vector<float> values; // width*height, band 1
    int width = 4;
    int height = 4;
    std::array<double, 6> gt = { 500000, 30, 0, 4500000, 0, -30 };
    QString crs = "EPSG:32648";
    QString acquisitionDate;   // SICNU_ACQUISITION_DATE
    QString radiometricState;  // SICNU_RADIOMETRIC_STATE
    QByteArray bandRole;       // SICNU_BAND_ROLE of band 1
    bool declareNodata = false;
    double nodata = -9999.0;
    bool declareScale = false;
    double scale = 1.0;
    double offset = 0.0;
};

bool writeScene( const SceneSpec &spec )
{
    ensureGdalInit();
    const QString proj = spec.crs.isEmpty() ? QString() : spec.crs;
    QString wkt = proj;
    if ( proj.startsWith( QLatin1String( "EPSG:" ) ) )
    {
        OGRSpatialReference srs;
        if ( srs.importFromEPSG( proj.mid( 5 ).toInt() ) != OGRERR_NONE )
            return false;
        char *wktOut = nullptr;
        srs.exportToWkt( &wktOut );
        wkt = QString::fromUtf8( wktOut );
        CPLFree( wktOut );
    }

    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
        return false;
    GDALDatasetH ds = GDALCreate( driver, spec.path.toUtf8().constData(), spec.width, spec.height,
                                  1, GDT_Float32, nullptr );
    if ( !ds )
        return false;
    GDALSetGeoTransform( ds, const_cast<double *>( spec.gt.data() ) );
    if ( !wkt.isEmpty() )
        GDALSetProjection( ds, wkt.toUtf8().constData() );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    if ( spec.declareNodata )
        GDALSetRasterNoDataValue( band, spec.nodata );
    if ( spec.declareScale )
    {
        GDALSetRasterScale( band, spec.scale );
        GDALSetRasterOffset( band, spec.offset );
    }
    if ( !spec.bandRole.isEmpty() )
        GDALSetMetadataItem( band, "SICNU_BAND_ROLE", spec.bandRole.constData(), nullptr );
    if ( !spec.acquisitionDate.isEmpty() )
        GDALSetMetadataItem( ds, "SICNU_ACQUISITION_DATE",
                             spec.acquisitionDate.toUtf8().constData(), nullptr );
    if ( !spec.radiometricState.isEmpty() )
        GDALSetMetadataItem( ds, "SICNU_RADIOMETRIC_STATE",
                             spec.radiometricState.toUtf8().constData(), nullptr );
    const bool ok = GDALRasterIO( band, GF_Write, 0, 0, spec.width, spec.height,
                                  const_cast<float *>( spec.values.data() ), spec.width,
                                  spec.height, GDT_Float32, 0, 0 ) == CE_None;
    GDALClose( ds );
    return ok;
}

SceneSpec makeScene( const QString &path, const QString &date, float value,
                     int width = 4, int height = 4 )
{
    SceneSpec spec;
    spec.path = path;
    spec.width = width;
    spec.height = height;
    spec.values.assign( static_cast<size_t>( width ) * height, value );
    spec.acquisitionDate = date;
    spec.declareNodata = true;
    spec.bandRole = "red";
    return spec;
}

bool hasIssue( const TemporalPreflightReport &report, const QString &code, bool blocking )
{
    for ( const auto &issue : report.issues )
        if ( issue.code == code && issue.blocking == blocking )
            return true;
    return false;
}

} // namespace

// ---------------------------------------------------------------- time ----

TEST_CASE( "Temporal time parsing keeps date/datetime precision", "[temporal][time]" )
{
    ensureApp();

    SECTION( "date-only stays Date precision (never a fake overpass instant)" )
    {
        const auto t = parseAcquisitionTime( QStringLiteral( "2026-08-01" ) );
        REQUIRE( t.valid );
        REQUIRE( t.precision == TimePrecision::Date );
        REQUIRE( t.iso == QStringLiteral( "2026-08-01" ) );
    }
    SECTION( "UTC datetime" )
    {
        const auto t = parseAcquisitionTime( QStringLiteral( "2026-08-01T10:30:00Z" ) );
        REQUIRE( t.valid );
        REQUIRE( t.precision == TimePrecision::DateTime );
    }
    SECTION( "garbage is invalid, never guessed" )
    {
        REQUIRE( !parseAcquisitionTime( QStringLiteral( "not-a-date" ) ).valid );
        REQUIRE( !parseAcquisitionTime( QString() ).valid );
        REQUIRE( !parseAcquisitionTime( QStringLiteral( "2026-13-99" ) ).valid );
    }
    SECTION( "real day offsets (goal §22)" )
    {
        const auto a = parseAcquisitionTime( QStringLiteral( "2025-01-01" ) );
        const auto b = parseAcquisitionTime( QStringLiteral( "2025-01-03" ) );
        const auto c = parseAcquisitionTime( QStringLiteral( "2025-01-11" ) );
        REQUIRE( b.daysSince( a ) == Approx( 2.0 ) );
        REQUIRE( c.daysSince( a ) == Approx( 10.0 ) );
        REQUIRE( a.daysSince( c ) == Approx( -10.0 ) );
    }
}

TEST_CASE( "Temporal filename time extraction", "[temporal][time]" )
{
    ensureApp();
    SECTION( "Sentinel-2 product id yields datetime" )
    {
        const auto t = timeFromFilename(
            QStringLiteral( "S2A_MSIL2A_20250403T101031_N0210_R022_T33UUU_20250403T120000.SAFE" ) );
        REQUIRE( t.valid );
        REQUIRE( t.precision == TimePrecision::DateTime );
        REQUIRE( t.iso.startsWith( QStringLiteral( "2025-04-03T10:10:31" ) ) );
    }
    SECTION( "Landsat scene id yields date" )
    {
        const auto t = timeFromFilename(
            QStringLiteral( "LC08_L1TP_044033_20190501_20190502_01_T1_B4.tif" ) );
        REQUIRE( t.valid );
        REQUIRE( t.precision == TimePrecision::Date );
        REQUIRE( t.iso == QStringLiteral( "2019-05-01" ) );
    }
    SECTION( "MODIS DOY" )
    {
        const auto t = timeFromFilename( QStringLiteral( "MOD09A1.A2025001.h12v04.061.2025010.hdf" ) );
        REQUIRE( t.valid );
        REQUIRE( t.iso == QStringLiteral( "2025-01-01" ) );
    }
    SECTION( "no date at all" )
    {
        REQUIRE( !timeFromFilename( QStringLiteral( "pretty_picture.tif" ) ).valid );
    }
}

TEST_CASE( "Welford and online regression numerics", "[temporal][stats]" )
{
    SECTION( "hand-checkable mean/stddev (§42)" )
    {
        stats::WelfordAccumulator acc;
        for ( double v : { 10.0, 20.0, 30.0 } )
            acc.add( v );
        REQUIRE( acc.n == 3 );
        REQUIRE( acc.mean == Approx( 20.0 ) );
        REQUIRE( acc.sampleVariance() == Approx( 100.0 ) );
        REQUIRE( acc.populationVariance() == Approx( 200.0 / 3.0 ) );
    }
    SECTION( "large baseline, tiny variance stays stable (§15)" )
    {
        stats::WelfordAccumulator acc;
        const double base = 1e9;
        for ( int i = 0; i < 10000; ++i )
            acc.add( base + ( i % 2 ? 0.001 : -0.001 ) );
        // naive sum(x^2)-sum(x)^2/n would lose this completely (errors ~1e5);
        // Welford stays within double-precision rounding at this baseline.
        REQUIRE( acc.mean == Approx( base ).epsilon( 1e-10 ) );
        REQUIRE( acc.sampleStddev() == Approx( 0.001 ).epsilon( 1e-3 ) );
    }
    SECTION( "regression: perfect line through irregular times (§43)" )
    {
        stats::OnlineRegression reg;
        // y = 1 + 0.5 t with t = 0, 2, 10 (real day offsets, NOT indices)
        reg.add( 0.0, 1.0 );
        reg.add( 2.0, 2.0 );
        reg.add( 10.0, 6.0 );
        REQUIRE( reg.solvable() );
        REQUIRE( reg.slope() == Approx( 0.5 ) );
        REQUIRE( reg.intercept() == Approx( 1.0 ) );
        REQUIRE( reg.r2() == Approx( 1.0 ) );
        REQUIRE( reg.rmse() == Approx( 0.0 ).margin( 1e-12 ) );
    }
    SECTION( "regression: index-as-time would give a different wrong slope" )
    {
        // y values at t=0,2,10: slope 0.5/day. If an implementation wrongly
        // used indices 0,1,2 it would compute slope 5/2=2.5. The test data
        // catches that class of bug.
        stats::OnlineRegression reg;
        reg.add( 0.0, 1.0 );
        reg.add( 2.0, 2.0 );
        reg.add( 10.0, 6.0 );
        REQUIRE( reg.slope() != Approx( 2.5 ) );
    }
    SECTION( "zero-variance series: R² is 1 by contract" )
    {
        stats::OnlineRegression reg;
        reg.add( 0.0, 5.0 );
        reg.add( 1.0, 5.0 );
        reg.add( 3.0, 5.0 );
        REQUIRE( reg.slope() == Approx( 0.0 ) );
        REQUIRE( reg.r2() == Approx( 1.0 ) );
    }
    SECTION( "n<2 unsolvable" )
    {
        stats::OnlineRegression reg;
        reg.add( 1.0, 2.0 );
        REQUIRE( !reg.solvable() );
        REQUIRE( std::isnan( reg.slope() ) );
    }
}

// ---------------------------------------------------------- collection ----

TEST_CASE( "Temporal collection ordering and duplicates", "[temporal][collection]" )
{
    ensureApp();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QStringList paths = {
        dir.filePath( QStringLiteral( "s3_2025-03-01.tif" ) ),
        dir.filePath( QStringLiteral( "s1_2025-01-01.tif" ) ),
        dir.filePath( QStringLiteral( "s2_2025-02-01.tif" ) ),
    };
    for ( int i = 0; i < 3; ++i )
        REQUIRE( writeScene( makeScene( paths.at( i ),
                                        QStringLiteral( "2025-%1-01" ).arg( i + 1, 2, 10,
                                                                              QChar( '0' ) ),
                                        10.0f + i ) ) );

    SECTION( "sorted chronologically regardless of input order" )
    {
        auto collection = TemporalCollection::fromScenePaths( paths );
        REQUIRE( collection.sceneCount() == 3 );
        REQUIRE( collection.scenes().at( 0 ).time.dateString() == QStringLiteral( "2025-01-01" ) );
        REQUIRE( collection.scenes().at( 2 ).time.dateString() == QStringLiteral( "2025-03-01" ) );
        REQUIRE( collection.timeRangeStartIso() == QStringLiteral( "2025-01-01" ) );
        REQUIRE( collection.timeRangeEndIso() == QStringLiteral( "2025-03-01" ) );
    }
    SECTION( "duplicate policy keep_all is deterministic (input order ties)" )
    {
        auto collection = TemporalCollection::fromScenePaths(
            { paths.at( 0 ), paths.at( 0 ) } );
        REQUIRE( collection.sceneCount() == 2 );
        collection.setDuplicatePolicy( DuplicatePolicy::KeepAll );
        REQUIRE( collection.applyDuplicatePolicy( DuplicatePolicy::KeepAll ) == 0 );
        REQUIRE( collection.sceneCount() == 2 );
    }
    SECTION( "duplicate policy reject drops later duplicates" )
    {
        auto collection = TemporalCollection::fromScenePaths( { paths.at( 0 ), paths.at( 0 ) } );
        QStringList dropped;
        REQUIRE( collection.applyDuplicatePolicy( DuplicatePolicy::Reject, &dropped ) == 1 );
        REQUIRE( collection.sceneCount() == 1 );
        REQUIRE( dropped.size() == 1 );
    }
    SECTION( "descriptor JSON round-trip preserves ordering and times (§30)" )
    {
        auto collection = TemporalCollection::fromScenePaths( paths );
        collection.setName( QStringLiteral( "test" ) );
        const QString jsonPath = dir.filePath( QStringLiteral( "collection.json" ) );
        REQUIRE( collection.save( jsonPath ) );

        TemporalCollection reloaded;
        QString err;
        REQUIRE( TemporalCollection::load( jsonPath, &reloaded, &err ) );
        REQUIRE( err.isEmpty() );
        REQUIRE( reloaded.sceneCount() == 3 );
        REQUIRE( reloaded.name() == QStringLiteral( "test" ) );
        for ( int i = 0; i < 3; ++i )
        {
            REQUIRE( reloaded.scenes().at( i ).path == paths.at( i ) );
            REQUIRE( reloaded.scenes().at( i ).time.iso == collection.scenes().at( i ).time.iso );
        }
    }
}

// ----------------------------------------------------------- preflight ----

TEST_CASE( "Temporal preflight catches scientific blockers", "[temporal][preflight]" )
{
    ensureApp();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    auto twoScenePaths = [&]( const QString &suffixA, const QString &suffixB ) {
        const QStringList paths = { dir.filePath( suffixA ), dir.filePath( suffixB ) };
        return paths;
    };

    SECTION( "clean homogeneous collection passes" )
    {
        const auto paths = twoScenePaths( QStringLiteral( "a.tif" ), QStringLiteral( "b.tif" ) );
        REQUIRE( writeScene( makeScene( paths.at( 0 ), QStringLiteral( "2025-01-01" ), 1.0f ) ) );
        REQUIRE( writeScene( makeScene( paths.at( 1 ), QStringLiteral( "2025-02-01" ), 2.0f ) ) );
        auto collection = TemporalCollection::fromScenePaths( paths );
        const auto report = runPreflight( collection, PreflightOptions{} );
        INFO( "issues: " << report.toJson().toStyledString() );
        REQUIRE( report.ok() );
        REQUIRE( report.gridCompatible );
        REQUIRE( report.scenesWithTime == 2 );
    }
    SECTION( "missing acquisition time blocks (never guessed)" )
    {
        const auto paths = twoScenePaths( QStringLiteral( "a.tif" ), QStringLiteral( "b.tif" ) );
        SceneSpec a = makeScene( paths.at( 0 ), QString(), 1.0f ); // no date, no filename date
        REQUIRE( writeScene( a ) );
        REQUIRE( writeScene( makeScene( paths.at( 1 ), QStringLiteral( "2025-02-01" ), 2.0f ) ) );
        auto collection = TemporalCollection::fromScenePaths( paths );
        const auto report = runPreflight( collection, PreflightOptions{} );
        REQUIRE( !report.ok() );
        REQUIRE( hasIssue( report, QStringLiteral( "temporal.missing_time" ), true ) );
    }
    SECTION( "sub-pixel origin mismatch detected — not just width/height (§46)" )
    {
        const auto paths = twoScenePaths( QStringLiteral( "a.tif" ), QStringLiteral( "b.tif" ) );
        SceneSpec a = makeScene( paths.at( 0 ), QStringLiteral( "2025-01-01" ), 1.0f );
        SceneSpec b = makeScene( paths.at( 1 ), QStringLiteral( "2025-02-01" ), 2.0f );
        b.gt[0] = 500000.0 + 10.0; // SAME resolution, shifted origin (10 m = 1/3 pixel)
        REQUIRE( writeScene( a ) );
        REQUIRE( writeScene( b ) );
        auto collection = TemporalCollection::fromScenePaths( paths );
        const auto report = runPreflight( collection, PreflightOptions{} );
        REQUIRE( !report.ok() );
        REQUIRE( hasIssue( report, QStringLiteral( "temporal.grid_mismatch" ), true ) );
    }
    SECTION( "different resolutions detected" )
    {
        const auto paths = twoScenePaths( QStringLiteral( "a.tif" ), QStringLiteral( "b.tif" ) );
        SceneSpec a = makeScene( paths.at( 0 ), QStringLiteral( "2025-01-01" ), 1.0f );
        SceneSpec b = makeScene( paths.at( 1 ), QStringLiteral( "2025-02-01" ), 2.0f );
        b.gt[1] = 60; // 60 m instead of 30 m
        REQUIRE( writeScene( a ) );
        REQUIRE( writeScene( b ) );
        auto collection = TemporalCollection::fromScenePaths( paths );
        REQUIRE( !runPreflight( collection, PreflightOptions{} ).ok() );
    }
    SECTION( "radiometric state mixing rejected (§10)" )
    {
        const auto paths = twoScenePaths( QStringLiteral( "a.tif" ), QStringLiteral( "b.tif" ) );
        SceneSpec a = makeScene( paths.at( 0 ), QStringLiteral( "2025-01-01" ), 0.1f );
        a.radiometricState = "surface_reflectance";
        SceneSpec b = makeScene( paths.at( 1 ), QStringLiteral( "2025-02-01" ), 1000.0f );
        b.radiometricState = "digital_number";
        REQUIRE( writeScene( a ) );
        REQUIRE( writeScene( b ) );
        auto collection = TemporalCollection::fromScenePaths( paths );
        const auto report = runPreflight( collection, PreflightOptions{} );
        REQUIRE( !report.ok() );
        REQUIRE( hasIssue( report, QStringLiteral( "temporal.radiometric_mismatch" ), true ) );
    }
    SECTION( "uniform scale/offset accepted and recorded (§45)" )
    {
        const auto paths = twoScenePaths( QStringLiteral( "a.tif" ), QStringLiteral( "b.tif" ) );
        SceneSpec a = makeScene( paths.at( 0 ), QStringLiteral( "2025-01-01" ), 1000.0f );
        a.declareScale = true;
        a.scale = 0.0001;
        SceneSpec b = makeScene( paths.at( 1 ), QStringLiteral( "2025-02-01" ), 2000.0f );
        b.declareScale = true;
        b.scale = 0.0001;
        REQUIRE( writeScene( a ) );
        REQUIRE( writeScene( b ) );
        auto collection = TemporalCollection::fromScenePaths( paths );
        const auto report = runPreflight( collection, PreflightOptions{} );
        REQUIRE( report.ok() );
        REQUIRE( report.scaleOffsetDeclared );
        REQUIRE( report.uniformScaleOffset );
        REQUIRE( report.uniformScale == Approx( 0.0001 ) );
    }
    SECTION( "mixed declared/undeclared scale rejected" )
    {
        const auto paths = twoScenePaths( QStringLiteral( "a.tif" ), QStringLiteral( "b.tif" ) );
        SceneSpec a = makeScene( paths.at( 0 ), QStringLiteral( "2025-01-01" ), 1000.0f );
        a.declareScale = true;
        a.scale = 0.0001;
        SceneSpec b = makeScene( paths.at( 1 ), QStringLiteral( "2025-02-01" ), 0.2f );
        // b declares nothing
        REQUIRE( writeScene( a ) );
        REQUIRE( writeScene( b ) );
        auto collection = TemporalCollection::fromScenePaths( paths );
        const auto report = runPreflight( collection, PreflightOptions{} );
        REQUIRE( !report.ok() );
        REQUIRE( hasIssue( report, QStringLiteral( "temporal.scale_offset_mismatch" ), true ) );
    }
    SECTION( "missing required band role blocks (§9 spectral)" )
    {
        const auto paths = twoScenePaths( QStringLiteral( "a.tif" ), QStringLiteral( "b.tif" ) );
        SceneSpec a = makeScene( paths.at( 0 ), QStringLiteral( "2025-01-01" ), 1.0f );
        a.bandRole = "red";
        SceneSpec b = makeScene( paths.at( 1 ), QStringLiteral( "2025-02-01" ), 2.0f );
        b.bandRole = ""; // no role anywhere in b — nir can resolve only positionally
        REQUIRE( writeScene( a ) );
        REQUIRE( writeScene( b ) );
        auto collection = TemporalCollection::fromScenePaths( paths );
        PreflightOptions options;
        options.requiredBandRoles.push_back( QStringLiteral( "nir" ) );
        const auto report = runPreflight( collection, options );
        // positional fallback resolves nir->band 4, but the raster has 1 band:
        // preflight must flag it instead of letting the read fail later.
        REQUIRE( hasIssue( report, QStringLiteral( "temporal.band_role_missing" ), true ) );
    }
}

// ------------------------------------------------------------- reader ----

TEST_CASE( "Temporal streaming reader: validity contract and scale", "[temporal][stream]" )
{
    ensureApp();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    SECTION( "declared NoData and NaN become NaN; scale applied (§27/§45)" )
    {
        SceneSpec spec;
        spec.path = dir.filePath( QStringLiteral( "scaled.tif" ) );
        spec.width = 2;
        spec.height = 1;
        spec.values = { 1000.0f, -9999.0f };
        spec.acquisitionDate = QStringLiteral( "2025-01-01" );
        spec.declareNodata = true;
        spec.nodata = -9999;
        spec.declareScale = true;
        spec.scale = 0.0001;
        spec.bandRole = "red";
        REQUIRE( writeScene( spec ) );

        auto collection = TemporalCollection::fromScenePaths( { spec.path } );
        const auto report = runPreflight( collection, PreflightOptions{} );
        REQUIRE( report.ok() );
        TemporalStreamOptions options;
        options.tileWidth = 2;
        options.tileHeight = 1;
        TemporalTileReader reader( collection, report, options, nullptr );
        REQUIRE( reader.width() == 2 );
        std::vector<float> tile( 2 );
        REQUIRE( reader.readSceneBandTile( 0, 1, 0, tile.data() ) );
        REQUIRE( tile[0] == Approx( 0.1f ) ); // 1000 * 1e-4 — explicit declared scale
        REQUIRE( std::isnan( tile[1] ) );     // NoData -> NaN
    }
    SECTION( "memory bounded: peak accounting independent of date count (§48)" )
    {
        // 20 / 50 / 100 dates: the reader holds ONE scratch tile + mask
        // buffers; the peak slot count must not scale with dates.
        std::uint64_t peak20 = 0, peak100 = 0;
        for ( int dates : { 20, 100 } )
        {
            QDir d( dir.path() );
            const QString sub = QStringLiteral( "dates%1" ).arg( dates );
            d.mkdir( sub );
            QStringList paths;
            for ( int i = 0; i < dates; ++i )
            {
                const QString p = dir.filePath( sub + QStringLiteral( "/s%1.tif" ).arg( i ) );
                const QString date = QDate( 2025, 1, 1 ).addDays( i ).toString( Qt::ISODate );
                REQUIRE( writeScene( makeScene( p, date, 1.0f, 8, 8 ) ) );
                paths << p;
            }
            auto collection = TemporalCollection::fromScenePaths( paths );
            const auto report = runPreflight( collection, PreflightOptions{} );
            REQUIRE( report.ok() );
            TemporalStreamOptions options;
            options.tileWidth = 256;
            options.tileHeight = 256;
            TemporalTileReader reader( collection, report, options, nullptr );
            std::vector<float> tile( 64 );
            for ( int s = 0; s < collection.sceneCount(); ++s )
                REQUIRE( reader.readSceneBandTile( s, 1, 0, tile.data() ) );
            if ( dates == 20 )
                peak20 = reader.peakSlots();
            else
                peak100 = reader.peakSlots();
        }
        // One 8x8 scene = 64 pixels; reader scratch = 64 + 48 = 112 slots for
        // BOTH date counts. Growing with dates would indicate a hidden cube.
        REQUIRE( peak20 == peak100 );
        REQUIRE( peak100 <= static_cast<std::uint64_t>( 256 * 256 * 2 ) );
    }
}
