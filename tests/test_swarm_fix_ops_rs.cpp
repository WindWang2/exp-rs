// tests/test_swarm_fix_ops_rs.cpp — regression anchors for the 2026-11-04
// swarm review fixes owned by src/operators/rs/**. One TEST_CASE per finding;
// each pins the POST-fix behavior (the pre-fix code was UB, swallowed a flush
// failure, or silently narrowed/declared nothing).
//
// Covered findings:
//   OPS-R2-1 [P0] rs:sar_network_inversion velocity-only call wrote out of
//                 bounds into the (unallocated) epoch-plane vector.
//   OPS-R1-1 [P2] rs:rx_anomaly / rs:sam_classify finalized outputs with a
//                 plain close() (flush failures swallowed).
//   OPS-R1-2 [P2] rs:apply_mask / rs:post_classification_change: dead
//                 CPLGetLastErrorType gate + no partial-output cleanup.
//   OPS-R1-3 [P3] CN import calibration scaled declared-NoData sentinels.
//   OPS-R1-4 [P3] change_sam/irmad/log_ratio/cva_angle skipped the grid +
//                 radiometric-state pre-flight.
//   OPS-R1-5 [P3] rs:register_images maxDim silently became the delivery
//                 resolution (now declared).
//   OPS-R1-6 [P3] rs:apply_mask dtype decision looked at band 1 only.
//   OPS-RS-2     rs:post_classification_change llround() on ±inf (UB).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextStream>

#include <json/json.h>

#include <gdal.h>
#include <gdal_priv.h>
#include <ogr_srs_api.h>
#include <cpl_conv.h>

#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/algorithms/satellite_products.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "runtime/observability/fault_registry.h"

using Catch::Approx;
using namespace sicnu::operators;

namespace {

int &swarmArgc()
{
    static int argc = 1;
    return argc;
}
char swarmArgv0[] = "test_swarm_fix_ops_rs";
char *swarmArgv[] = { swarmArgv0, nullptr };

struct SwarmAppInit
{
    SwarmAppInit()
    {
        if ( !QCoreApplication::instance() )
            new QCoreApplication( swarmArgc(), swarmArgv );
    }
};

/// Hard-disarms every armed fault on scope exit (mirrors test_fault_matrix).
struct FaultScope
{
    FaultScope() = default;
    ~FaultScope() { sicnu::runtime::observability::fault::disarmAllFaults(); }
    FaultScope( const FaultScope & ) = delete;
    FaultScope &operator=( const FaultScope & ) = delete;
};

void armCloseFlushFault()
{
    sicnu::runtime::observability::fault::armFault(
        { "gdal_wrapper.close_flush",
          sicnu::runtime::observability::fault::Mode::NextN, 1, {} } );
}

Json::Value runOp( const std::string &id, const Json::Value &params )
{
    auto op = RSOperatorRegistry::instance().create( id );
    REQUIRE( op != nullptr );
    RSOperatorContext context;
    return op->run( params, context );
}

/// WGS84 grid identical to r4fixtures::setGrid (same gt/projection on every
/// fixture keeps the change operators' grid pre-flight quiet).
void setSwarmGrid( GDALDatasetH ds, int height )
{
    double gt[6] = { 0.0, 1.0, 0.0, static_cast<double>( height ), 0.0, -1.0 };
    REQUIRE( GDALSetGeoTransform( ds, gt ) == CE_None );
    REQUIRE( GDALSetProjection( ds,
                                "GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\","
                                "SPHEROID[\"WGS 84\",6378137,298.257223563]],"
                                "PRIMEM[\"Greenwich\",0],UNIT[\"degree\","
                                "0.0174532925199433]]" )
             == CE_None );
}

QString writeFloatRaster( const QString &path, int w, int h,
                          const std::vector<std::vector<float>> &bands,
                          bool hasNoData = false, double nodata = 0.0 )
{
    ensureGdalInit();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), w, h,
                                  static_cast<int>( bands.size() ), GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    setSwarmGrid( ds, h );
    for ( size_t b = 0; b < bands.size(); ++b )
    {
        GDALRasterBandH band = GDALGetRasterBand( ds, static_cast<int>( b ) + 1 );
        if ( hasNoData )
            GDALSetRasterNoDataValue( band, nodata );
        REQUIRE( GDALRasterIO( band, GF_Write, 0, 0, w, h,
                               const_cast<float *>( bands[b].data() ), w, h, GDT_Float32,
                               0, 0 ) == CE_None );
    }
    GDALClose( ds );
    return path;
}

QString writeU16Raster( const QString &path, int w, int h, const std::vector<uint16_t> &values )
{
    ensureGdalInit();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), w, h, 1, GDT_UInt16,
                                  nullptr );
    REQUIRE( ds != nullptr );
    setSwarmGrid( ds, h );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    REQUIRE( GDALRasterIO( band, GF_Write, 0, 0, w, h,
                           const_cast<uint16_t *>( values.data() ), w, h, GDT_UInt16, 0, 0 )
             == CE_None );
    GDALClose( ds );
    return path;
}

QString writeByteMask( const QString &path, int w, int h, const std::vector<uint8_t> &values )
{
    ensureGdalInit();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), w, h, 1, GDT_Byte,
                                  nullptr );
    REQUIRE( ds != nullptr );
    setSwarmGrid( ds, h );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    REQUIRE( GDALRasterIO( band, GF_Write, 0, 0, w, h,
                           const_cast<uint8_t *>( values.data() ), w, h, GDT_Byte, 0, 0 )
             == CE_None );
    GDALClose( ds );
    return path;
}

std::vector<double> readBandAsDouble( const QString &path, int band1Based )
{
    GDALAllRegister();
    GDALDatasetH ds = GDALOpen( path.toUtf8().constData(), GA_ReadOnly );
    REQUIRE( ds != nullptr );
    GDALRasterBandH band = GDALGetRasterBand( ds, band1Based );
    REQUIRE( band != nullptr );
    const int w = GDALGetRasterXSize( ds );
    const int h = GDALGetRasterYSize( ds );
    std::vector<double> values( static_cast<size_t>( w ) * h, 0.0 );
    REQUIRE( GDALRasterIO( band, GF_Read, 0, 0, w, h, values.data(), w, h, GDT_Float64,
                           0, 0 ) == CE_None );
    GDALClose( ds );
    return values;
}

std::uint32_t hashGrain( int x, int y )
{
    std::uint32_t h = static_cast<std::uint32_t>( x ) * 374761393u
                      + static_cast<std::uint32_t>( y ) * 668265263u;
    h = ( h ^ ( h >> 13 ) ) * 1274126177u;
    h ^= h >> 16;
    return h;
}

float texture( int x, int y )
{
    const double fx = x, fy = y;
    return static_cast<float>( 50.0 + 20.0 * std::sin( fx / 9.3 ) * std::sin( fy / 7.7 )
                               + 10.0 * std::sin( ( fx + fy ) / 5.1 )
                               + 3.0 * ( static_cast<double>( hashGrain( x, y ) & 0xFFFF )
                                         / 32767.5 - 1.0 ) );
}

void writeRegistrationTiff( const QString &path, int dim,
                            const std::function<float( int, int )> &pixel )
{
    GDALAllRegister();
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDataset *ds = driver->Create( path.toUtf8().constData(), dim, dim, 1, GDT_Float32,
                                      nullptr );
    REQUIRE( ds != nullptr );
    double gt[6] = { 450000.0, 10.0, 0.0, 5000000.0, 0.0, -10.0 };
    ds->SetGeoTransform( gt );
    ds->SetProjection( "EPSG:32633" );
    std::vector<float> buffer( static_cast<std::size_t>( dim ) * dim, 0.0f );
    for ( int y = 0; y < dim; ++y )
        for ( int x = 0; x < dim; ++x )
            buffer[static_cast<std::size_t>( y ) * dim + x] = pixel( x, y );
    REQUIRE( ds->GetRasterBand( 1 )->RasterIO( GF_Write, 0, 0, dim, dim, buffer.data(), dim,
                                               dim, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( ds );
}

/// Minimal CRESDA-style GF-1 PMS sidecar; extraTags are spliced before
/// </MetaInfo> (mirrors tests/test_cn_products.cpp, MSS-only).
QString writeCresdaXml( const QString &path, const QString &productId,
                        const QStringList &extraTags = {} )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Text ) );
    QTextStream out( &file );
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<MetaInfo>\n";
    out << "  <ProductID>" << productId << "</ProductID>\n";
    out << "  <SatelliteID>GF1</SatelliteID>\n";
    out << "  <SensorID>PMS1</SensorID>\n";
    out << "  <ModeID>MSS</ModeID>\n";
    out << "  <ReceiveDate>2024-04-15</ReceiveDate>\n";
    out << "  <ReceiveTime>11:23:45.123</ReceiveTime>\n";
    out << "  <StopDate>2024-04-15</StopDate>\n";
    out << "  <StopTime>11:24:02.800</StopTime>\n";
    out << "  <Width>8</Width>\n  <Height>8</Height>\n";
    out << "  <PixelSizeX>8</PixelSizeX>\n  <PixelSizeY>8</PixelSizeY>\n";
    for ( int b = 1; b <= 4; ++b )
        out << "  <BandID>B" << b << "</BandID>\n";
    for ( const QString &line : extraTags )
        out << "  " << line << "\n";
    out << "</MetaInfo>\n";
    file.close();
    return path;
}

/// GF-1 MSS product directory with per-band declared calibration; band 4
/// declares -9999 NoData and carries one sentinel pixel.
QString makeCnGaofenDir( const QDir &root, QString *msTiffOut )
{
    const QString productId = QStringLiteral( "GF1_PMS1_E117.0_N40.0_20240415_L1A0001234567" );
    const QString dir = root.filePath( productId + QStringLiteral( "-MSS1" ) );
    REQUIRE( QDir().mkpath( dir ) );

    constexpr int kN = 8;
    std::vector<std::vector<float>> bands( 4, std::vector<float>(
                                                     static_cast<size_t>( kN ) * kN, 0.0f ) );
    for ( int y = 0; y < kN; ++y )
        for ( int x = 0; x < kN; ++x )
        {
            bands[0][static_cast<size_t>( y ) * kN + x] = 400.0f;
            bands[1][static_cast<size_t>( y ) * kN + x] = 500.0f;
            bands[2][static_cast<size_t>( y ) * kN + x] = 350.0f;
            bands[3][static_cast<size_t>( y ) * kN + x] = 2400.0f;
        }
    // One declared-NoData sentinel pixel on band 4 (pre-fix it was scaled
    // into a fabricated radiance: -9999 * 0.8 - 4 = -8003.2).
    bands[3][static_cast<size_t>( kN - 1 ) * kN + ( kN - 1 )] = -9999.0f;

    const QString tiff = dir + "/" + productId + "-MSS1.tiff";
    ensureGdalInit();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDatasetH ds = GDALCreate( driver, tiff.toUtf8().constData(), kN, kN, 4, GDT_Float32,
                                  nullptr );
    REQUIRE( ds != nullptr );
    OGRSpatialReferenceH srs = OSRNewSpatialReference( nullptr );
    REQUIRE( OSRImportFromEPSG( srs, 32649 ) == OGRERR_NONE );
    char *wkt = nullptr;
    REQUIRE( OSRExportToWkt( srs, &wkt ) == OGRERR_NONE );
    double gt[6] = { 500000.0, 8.0, 0.0, 4400000.0, 0.0, -8.0 };
    REQUIRE( GDALSetGeoTransform( ds, gt ) == CE_None );
    REQUIRE( GDALSetProjection( ds, wkt ) == CE_None );
    CPLFree( wkt );
    OSRDestroySpatialReference( srs );
    for ( int b = 0; b < 4; ++b )
    {
        GDALRasterBandH band = GDALGetRasterBand( ds, b + 1 );
        if ( b == 3 )
            GDALSetRasterNoDataValue( band, -9999.0 );
        REQUIRE( GDALRasterIO( band, GF_Write, 0, 0, kN, kN,
                               const_cast<float *>( bands[static_cast<size_t>( b )].data() ),
                               kN, kN, GDT_Float32, 0, 0 ) == CE_None );
    }
    GDALClose( ds );

    writeCresdaXml( dir + "/" + productId + "-MSS1.xml", productId,
                    { QStringLiteral( "<GainVal>0.5,0.6,0.7,0.8</GainVal>" ),
                      QStringLiteral( "<OffsetVal>-1.0,-2.0,-3.0,-4.0</OffsetVal>" ) } );
    *msTiffOut = tiff;
    return dir;
}

} // namespace

// ---------------------------------------------------------------------------
// OPS-R2-1 [P0]: velocity-only call must not write into the unallocated
// epoch-plane vector (the fill loop was missing the stackPath guard).
// ---------------------------------------------------------------------------
TEST_CASE( "swarm fix OPS-R2-1: sar_network_inversion velocity-only call "
           "completes without the displacement stack",
           "[swarm][ops_rs][sar][insar11][inversion]" )
{
    const SwarmAppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    constexpr int kN = 6;
    // Truth: u = [0, 1, 2, 3] mm per epoch on a [0,1,2,3] year base; pairs
    // (1,0) (2,0) (3,1) (3,2) — the same construction as
    // tests/test_sar_platform11.cpp.
    const double u[] = { 0.0, 1e-3, 2e-3, 3e-3 };
    const int pairMaster[] = { 1, 2, 3, 3 };
    const int pairSlave[] = { 0, 0, 1, 2 };

    Json::Value inputs( Json::arrayValue );
    for ( int p = 0; p < 4; ++p )
    {
        std::vector<float> plane( static_cast<size_t>( kN ) * kN,
                                  static_cast<float>( u[pairMaster[p]] - u[pairSlave[p]] ) );
        inputs.append( writeFloatRaster( tmp.filePath( QString( "d_%1.tif" ).arg( p ) ), kN,
                                         kN, { plane } )
                           .toStdString() );
    }

    Json::Value pairsJson( Json::arrayValue );
    for ( int p = 0; p < 4; ++p )
    {
        Json::Value pair( Json::arrayValue );
        pair.append( pairMaster[p] );
        pair.append( pairSlave[p] );
        pairsJson.append( pair );
    }
    Json::Value temporal( Json::arrayValue );
    for ( const double t : { 0.0, 1.0, 2.0, 3.0 } )
        temporal.append( t );

    Json::Value params( Json::objectValue );
    params["displacementInputs"] = inputs;
    params["pairs"] = pairsJson;
    params["epochTemporalYears"] = temporal;
    params["velocityOutput"] = tmp.filePath( "velocity_only.tif" ).toStdString();
    // NO displacementOutput, NO rmsOutput: the minimal legal call. Pre-fix
    // the per-pixel fill loop wrote epochPlanes[e][idx] on an empty vector.

    Json::Value result;
    REQUIRE_NOTHROW( result = runOp( "rs:sar_network_inversion", params ) );
    REQUIRE( result["solvedPixels"].asInt64() == kN * kN );
    REQUIRE_FALSE( result.isMember( "displacementOutput" ) );
    REQUIRE_FALSE( result.isMember( "rmsOutput" ) );

    const std::vector<double> velocity = readBandAsDouble( tmp.filePath( "velocity_only.tif" ), 1 );
    REQUIRE( velocity.size() == static_cast<size_t>( kN ) * kN );
    for ( const double v : velocity )
        REQUIRE( v == Approx( 1e-3 ).margin( 1e-9 ) );
}

// ---------------------------------------------------------------------------
// OPS-R1-1 [P2]: the closeWithError tails of rs:rx_anomaly / rs:sam_classify
// must keep the happy path honest (the fault-injection seam only exists on
// the GdalDatasetWrapper path covered by OPS-R1-2 below).
// ---------------------------------------------------------------------------
TEST_CASE( "swarm fix OPS-R1-1: rx_anomaly and sam_classify still finalize "
           "their outputs through closeWithError",
           "[swarm][ops_rs][spectral]" )
{
    const SwarmAppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    constexpr int kN = 8;
    std::vector<std::vector<float>> bands( 2, std::vector<float>(
                                                  static_cast<size_t>( kN ) * kN, 0.0f ) );
    for ( int y = 0; y < kN; ++y )
        for ( int x = 0; x < kN; ++x )
        {
            const size_t i = static_cast<size_t>( y ) * kN + x;
            bands[0][i] = 100.0f + static_cast<float>( x );
            bands[1][i] = 200.0f - static_cast<float>( y );
        }
    const QString input = writeFloatRaster( tmp.filePath( "spectral.tif" ), kN, kN, bands );

    {
        Json::Value params( Json::objectValue );
        params["input"] = input.toStdString();
        params["output"] = tmp.filePath( "rx.tif" ).toStdString();
        Json::Value result;
        REQUIRE_NOTHROW( result = runOp( "rs:rx_anomaly", params ) );
        REQUIRE( QFile::exists( tmp.filePath( "rx.tif" ) ) );
        REQUIRE( result["max"].asDouble() >= 0.0 );
    }
    {
        Json::Value params( Json::objectValue );
        params["input"] = input.toStdString();
        params["output"] = tmp.filePath( "sam.tif" ).toStdString();
        Json::Value refs( Json::arrayValue );
        Json::Value refA( Json::arrayValue );
        refA.append( 100.0 );
        refA.append( 200.0 );
        Json::Value refB( Json::arrayValue );
        refB.append( 105.0 );
        refB.append( 195.0 );
        refs.append( refA );
        refs.append( refB );
        params["refs"] = refs;
        Json::Value result;
        REQUIRE_NOTHROW( result = runOp( "rs:sam_classify", params ) );
        REQUIRE( QFile::exists( tmp.filePath( "sam.tif" ) ) );
        REQUIRE( result["classes"].asInt() == 2 );
    }
}

// ---------------------------------------------------------------------------
// OPS-R1-2 [P2]: a close-time flush failure must fail the run truthfully and
// remove the partial output (the old CPLGetLastErrorType gate was dead and
// the CE_Warning comparison killed good products).
// ---------------------------------------------------------------------------
TEST_CASE( "swarm fix OPS-R1-2: apply_mask fails truthfully on a close-time "
           "flush failure and removes the partial output",
           "[swarm][ops_rs][mask][fault]" )
{
    const SwarmAppInit app;
    const FaultScope faults;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    constexpr int kN = 8;
    const QString input = writeFloatRaster( tmp.filePath( "input.tif" ), kN, kN,
                                            { std::vector<float>( kN * kN, 100.0f ) }, true,
                                            -9999.0 );
    const QString mask = writeByteMask( tmp.filePath( "mask.tif" ), kN, kN,
                                        std::vector<uint8_t>( kN * kN, 0 ) );

    // Healthy control: the run succeeds and the output survives.
    const QString healthyOut = tmp.filePath( "healthy.tif" );
    {
        Json::Value params( Json::objectValue );
        params["input"] = input.toStdString();
        params["mask"] = mask.toStdString();
        params["output"] = healthyOut.toStdString();
        Json::Value result;
        REQUIRE_NOTHROW( result = runOp( "rs:apply_mask", params ) );
        REQUIRE( QFile::exists( healthyOut ) );
    }

    // Armed close-flush fault: typed refusal AND no partial file left behind.
    const QString armedOut = tmp.filePath( "armed.tif" );
    {
        Json::Value params( Json::objectValue );
        params["input"] = input.toStdString();
        params["mask"] = mask.toStdString();
        params["output"] = armedOut.toStdString();
        armCloseFlushFault();
        bool threw = false;
        try
        {
            ( void )runOp( "rs:apply_mask", params );
        }
        catch ( const RSOperatorError &error )
        {
            threw = true;
            REQUIRE( error.code() == ErrorCode::GdalError );
        }
        REQUIRE( threw );
        REQUIRE_FALSE( QFile::exists( armedOut ) );
    }
}

TEST_CASE( "swarm fix OPS-R1-2: post_classification_change fails truthfully on "
           "a close-time flush failure and removes the partial output",
           "[swarm][ops_rs][change][fault]" )
{
    const SwarmAppInit app;
    const FaultScope faults;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    constexpr int kN = 8;
    const QString before = writeU16Raster(
        tmp.filePath( "before.tif" ), kN, kN,
        std::vector<uint16_t>( static_cast<size_t>( kN ) * kN, 1 ) );
    const QString after = writeU16Raster(
        tmp.filePath( "after.tif" ), kN, kN,
        std::vector<uint16_t>( static_cast<size_t>( kN ) * kN, 2 ) );

    const QString healthyOut = tmp.filePath( "healthy_change.tif" );
    {
        Json::Value params( Json::objectValue );
        params["before"] = before.toStdString();
        params["after"] = after.toStdString();
        params["output"] = healthyOut.toStdString();
        Json::Value result;
        REQUIRE_NOTHROW( result = runOp( "rs:post_classification_change", params ) );
        REQUIRE( QFile::exists( healthyOut ) );
        REQUIRE( result["classCount"].asInt() == 3 );
    }

    const QString armedOut = tmp.filePath( "armed_change.tif" );
    {
        Json::Value params( Json::objectValue );
        params["before"] = before.toStdString();
        params["after"] = after.toStdString();
        params["output"] = armedOut.toStdString();
        armCloseFlushFault();
        bool threw = false;
        try
        {
            ( void )runOp( "rs:post_classification_change", params );
        }
        catch ( const RSOperatorError &error )
        {
            threw = true;
            REQUIRE( error.code() == ErrorCode::GdalError );
        }
        REQUIRE( threw );
        REQUIRE_FALSE( QFile::exists( armedOut ) );
    }
}

// ---------------------------------------------------------------------------
// OPS-RS-2: llround() on ±inf is UB — a non-finite class sample must be
// treated as invalid (NoData code), not fed to the rounding intrinsic.
// ---------------------------------------------------------------------------
TEST_CASE( "swarm fix OPS-RS-2: post_classification_change treats ±inf class "
           "samples as NoData instead of rounding them",
           "[swarm][ops_rs][change][numeric]" )
{
    const SwarmAppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    constexpr int kN = 4;
    constexpr float kInf = std::numeric_limits<float>::infinity();
    std::vector<std::vector<float>> beforeBands( 1, std::vector<float>(
                                                       static_cast<size_t>( kN ) * kN, 0.0f ) );
    std::vector<std::vector<float>> afterBands( 1, std::vector<float>(
                                                      static_cast<size_t>( kN ) * kN, 0.0f ) );
    // Pixel 0 carries +inf on the before date; pixel 1 is a real transition
    // (class 1 -> class 2); the rest stay at class 0.
    beforeBands[0][1] = 1.0f;
    afterBands[0][1] = 2.0f;
    beforeBands[0][0] = kInf;
    const QString before = writeFloatRaster( tmp.filePath( "inf_before.tif" ), kN, kN,
                                             beforeBands );
    const QString after = writeFloatRaster( tmp.filePath( "inf_after.tif" ), kN, kN,
                                            afterBands );

    Json::Value params( Json::objectValue );
    params["before"] = before.toStdString();
    params["after"] = after.toStdString();
    params["output"] = tmp.filePath( "inf_change.tif" ).toStdString();

    Json::Value result;
    REQUIRE_NOTHROW( result = runOp( "rs:post_classification_change", params ) );
    // maxSeen over the VALID pixels is 2 → classCount 3; the inf pixel is
    // excluded from the statistics and the map.
    REQUIRE( result["classCount"].asInt() == 3 );
    REQUIRE( result["totalPixels"].asUInt64() == static_cast<size_t>( kN ) * kN - 1 );

    GDALAllRegister();
    GDALDatasetH ds = GDALOpen( tmp.filePath( "inf_change.tif" ).toUtf8().constData(),
                                GA_ReadOnly );
    REQUIRE( ds != nullptr );
    std::vector<uint16_t> codes( static_cast<size_t>( kN ) * kN, 0 );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Read, 0, 0, kN, kN, codes.data(),
                           kN, kN, GDT_UInt16, 0, 0 ) == CE_None );
    GDALClose( ds );
    REQUIRE( codes[0] == 65535 ); // ±inf sample → NoData sentinel, never a code
    REQUIRE( codes[1] == 1 * 3 + 2 );
    REQUIRE( codes[2] == 0 );
}

// ---------------------------------------------------------------------------
// OPS-R1-4 [P3]: the standalone atoms must run the facade's pre-flight —
// radiometric-state comparability and grid consistency.
// ---------------------------------------------------------------------------
TEST_CASE( "swarm fix OPS-R1-4: change atoms refuse mismatched radiometric "
           "states and off-grid pairs",
           "[swarm][ops_rs][change][preflight]" )
{
    const SwarmAppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    constexpr int kN = 8;
    const std::vector<float> plane( static_cast<size_t>( kN ) * kN, 10.0f );
    const QString before =
        writeFloatRaster( tmp.filePath( "atom_before.tif" ), kN, kN, { plane } );
    const QString after =
        writeFloatRaster( tmp.filePath( "atom_after.tif" ), kN, kN, { plane } );

    // Healthy control: same grid, no declared states → the atom runs.
    {
        Json::Value params( Json::objectValue );
        params["before"] = before.toStdString();
        params["after"] = after.toStdString();
        params["output"] = tmp.filePath( "atom_ok.tif" ).toStdString();
        Json::Value result;
        REQUIRE_NOTHROW( result = runOp( "rs:change_log_ratio", params ) );
        REQUIRE( QFile::exists( tmp.filePath( "atom_ok.tif" ) ) );
    }

    // Radiometric-state mismatch: typed refusal naming both states.
    {
        QString err;
        REQUIRE( SatelliteProducts::setRadiometricState(
                     before, SatelliteProducts::kRadiometricStateDigitalNumber, &err ) );
        REQUIRE( SatelliteProducts::setRadiometricState(
                     after, SatelliteProducts::kRadiometricStateRadiance, &err ) );
        Json::Value params( Json::objectValue );
        params["before"] = before.toStdString();
        params["after"] = after.toStdString();
        params["output"] = tmp.filePath( "atom_state.tif" ).toStdString();
        bool threw = false;
        try
        {
            ( void )runOp( "rs:change_log_ratio", params );
        }
        catch ( const RSOperatorError &error )
        {
            threw = true;
            REQUIRE( error.code() == ErrorCode::InvalidInputData );
            const std::string what = error.message();
            REQUIRE( what.find( "radiometric states" ) != std::string::npos );
        }
        REQUIRE( threw );
        REQUIRE_FALSE( QFile::exists( tmp.filePath( "atom_state.tif" ) ) );
    }

    // Grid mismatch: different pixel size on the same origin → blocking
    // grid-compat issue, typed refusal (previously only dimensions were
    // checked, and only by the facade-routed metrics).
    {
        const QString offGrid = tmp.filePath( "atom_offgrid.tif" );
        ensureGdalInit();
        GDALDriverH driver = GDALGetDriverByName( "GTiff" );
        REQUIRE( driver != nullptr );
        GDALDatasetH ds = GDALCreate( driver, offGrid.toUtf8().constData(), kN, kN, 1,
                                      GDT_Float32, nullptr );
        REQUIRE( ds != nullptr );
        double gt[6] = { 0.0, 2.0, 0.0, static_cast<double>( kN ), 0.0, -2.0 };
        REQUIRE( GDALSetGeoTransform( ds, gt ) == CE_None );
        REQUIRE( GDALSetProjection( ds,
                                    "GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\","
                                    "SPHEROID[\"WGS 84\",6378137,298.257223563]],"
                                    "PRIMEM[\"Greenwich\",0],UNIT[\"degree\","
                                    "0.0174532925199433]]" )
                 == CE_None );
        REQUIRE( GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Write, 0, 0, kN, kN,
                               const_cast<float *>( plane.data() ), kN, kN, GDT_Float32,
                               0, 0 ) == CE_None );
        GDALClose( ds );

        Json::Value params( Json::objectValue );
        params["before"] = before.toStdString();
        params["after"] = offGrid.toStdString();
        params["output"] = tmp.filePath( "atom_grid.tif" ).toStdString();
        bool threw = false;
        try
        {
            ( void )runOp( "rs:change_sam", params );
        }
        catch ( const RSOperatorError &error )
        {
            threw = true;
            REQUIRE( error.code() == ErrorCode::InvalidInputData );
        }
        REQUIRE( threw );
        REQUIRE_FALSE( QFile::exists( tmp.filePath( "atom_grid.tif" ) ) );
    }
}

// ---------------------------------------------------------------------------
// OPS-R1-5 [P3]: maxDim is a processing cap — the delivered grid must be
// declared when the cap decimates the reference.
// ---------------------------------------------------------------------------
TEST_CASE( "swarm fix OPS-R1-5: register_images declares the decimated "
           "delivered grid when maxDim caps the reference",
           "[swarm][ops_rs][registration]" )
{
    const SwarmAppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    constexpr int kDim = 256;
    writeRegistrationTiff( tmp.filePath( "reg_src.tif" ), kDim, texture );
    writeRegistrationTiff( tmp.filePath( "reg_ref.tif" ), kDim,
                           []( int x, int y ) { return texture( x - 4, y - 6 ); } );

    const auto run = [&]( const QString &outName, int maxDim ) {
        Json::Value params( Json::objectValue );
        params["source"] = tmp.filePath( "reg_src.tif" ).toStdString();
        params["reference"] = tmp.filePath( "reg_ref.tif" ).toStdString();
        params["output"] = tmp.filePath( outName ).toStdString();
        params["metric"] = "phase_correlation";
        params["maxDim"] = maxDim;
        return runOp( "rs:register_images", params );
    };

    // Uncapped control: the delivered grid equals the reference grid.
    Json::Value full;
    REQUIRE_NOTHROW( full = run( "reg_full.tif", kDim ) );
    REQUIRE( full["outputGridDecimated"].asBool() == false );
    REQUIRE( full["outputWidth"].asInt() == kDim );
    REQUIRE( full["outputHeight"].asInt() == kDim );
    REQUIRE( full["referenceWidth"].asInt() == kDim );
    REQUIRE( full["maxDim"].asInt() == kDim );

    // Capped run: the declaration must say the delivered grid is decimated.
    Json::Value capped;
    REQUIRE_NOTHROW( capped = run( "reg_capped.tif", kDim / 2 ) );
    REQUIRE( capped["outputGridDecimated"].asBool() == true );
    REQUIRE( capped["outputWidth"].asInt() == kDim / 2 );
    REQUIRE( capped["outputHeight"].asInt() == kDim / 2 );
    REQUIRE( capped["referenceWidth"].asInt() == kDim );
    REQUIRE( capped["maxDim"].asInt() == kDim / 2 );

    GDALAllRegister();
    GDALDataset *out = static_cast<GDALDataset *>(
        GDALOpen( tmp.filePath( "reg_capped.tif" ).toUtf8().constData(), GA_ReadOnly ) );
    REQUIRE( out != nullptr );
    REQUIRE( out->GetRasterXSize() == kDim / 2 );
    REQUIRE( out->GetRasterYSize() == kDim / 2 );
    GDALClose( out );
}

// ---------------------------------------------------------------------------
// OPS-R1-6 [P3]: the native-dtype decision must scan EVERY band — a
// mixed-dtype stack's Float64 band must not round-trip through float32.
// ---------------------------------------------------------------------------
TEST_CASE( "swarm fix OPS-R1-6: apply_mask keeps a Float64 band exact inside "
           "a mixed-dtype stack",
           "[swarm][ops_rs][mask][dtype]" )
{
    const SwarmAppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    constexpr int kN = 4;
    // 2^24 + 1 is exactly representable in Float64 but NOT in Float32 (the
    // float32 round-trip yields 2^24).
    const double kNotFloat32Exact = 16777217.0;

    const QString input = tmp.filePath( "mixed.tif" );
    ensureGdalInit();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDatasetH ds = GDALCreate( driver, input.toUtf8().constData(), kN, kN, 1,
                                  GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    double gt[6] = { 0.0, 1.0, 0.0, static_cast<double>( kN ), 0.0, -1.0 };
    REQUIRE( GDALSetGeoTransform( ds, gt ) == CE_None );
    REQUIRE( GDALSetProjection( ds,
                                "GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\","
                                "SPHEROID[\"WGS 84\",6378137,298.257223563]],"
                                "PRIMEM[\"Greenwich\",0],UNIT[\"degree\","
                                "0.0174532925199433]]" )
             == CE_None );
    {
        GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
        GDALSetRasterNoDataValue( band, -9999.0 );
        const std::vector<float> plane( static_cast<size_t>( kN ) * kN, 1.5f );
        REQUIRE( GDALRasterIO( band, GF_Write, 0, 0, kN, kN,
                               const_cast<float *>( plane.data() ), kN, kN, GDT_Float32,
                               0, 0 ) == CE_None );
    }
    // Second band: Float64 (per-band dtype differs from band 1).
    REQUIRE( GDALAddBand( ds, GDT_Float64, nullptr ) == CE_None );
    {
        GDALRasterBandH band = GDALGetRasterBand( ds, 2 );
        GDALSetRasterNoDataValue( band, -9999.0 );
        const std::vector<double> plane( static_cast<size_t>( kN ) * kN, kNotFloat32Exact );
        REQUIRE( GDALRasterIO( band, GF_Write, 0, 0, kN, kN,
                               const_cast<double *>( plane.data() ), kN, kN, GDT_Float64,
                               0, 0 ) == CE_None );
    }
    GDALClose( ds );

    const QString mask = writeByteMask( tmp.filePath( "mixed_mask.tif" ), kN, kN,
                                        std::vector<uint8_t>( kN * kN, 0 ) );
    const QString output = tmp.filePath( "mixed_out.tif" );

    Json::Value params( Json::objectValue );
    params["input"] = input.toStdString();
    params["mask"] = mask.toStdString();
    params["output"] = output.toStdString();
    Json::Value result;
    REQUIRE_NOTHROW( result = runOp( "rs:apply_mask", params ) );

    GDALAllRegister();
    GDALDatasetH outDs = GDALOpen( output.toUtf8().constData(), GA_ReadOnly );
    REQUIRE( outDs != nullptr );
    REQUIRE( GDALGetRasterCount( outDs ) == 2 );
    // The Float64 band forces the native path and the output dtype.
    REQUIRE( GDALGetRasterDataType( GDALGetRasterBand( outDs, 2 ) ) == GDT_Float64 );
    const std::vector<double> wide =
        readBandAsDouble( output, 2 );
    for ( const double v : wide )
        REQUIRE( v == kNotFloat32Exact );
    const std::vector<double> narrow = readBandAsDouble( output, 1 );
    for ( const double v : narrow )
        REQUIRE( v == Approx( 1.5 ).margin( 1e-12 ) );
    GDALClose( outDs );
}

// ---------------------------------------------------------------------------
// OPS-R1-3 [P3]: CN-import calibration must not scale declared-NoData
// sentinel pixels into fabricated radiance.
// ---------------------------------------------------------------------------
TEST_CASE( "swarm fix OPS-R1-3: CN import calibration leaves declared-NoData "
           "sentinel pixels untouched",
           "[swarm][ops_rs][cn][calibration]" )
{
    const SwarmAppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    // Defensive (plan11 pattern): resolve the in-tree sensor-profile registry,
    // never a leaked override from another suite.
    qunsetenv( "SICNU_DATA_DIR" );

    QString msTiff;
    const QString dir = makeCnGaofenDir( QDir( tmp.path() ), &msTiff );
    ( void )msTiff;

    const QString output = tmp.path() + "/cn_calibrated.tif";
    Json::Value params( Json::objectValue );
    params["input"] = dir.toStdString();
    params["output"] = output.toStdString();
    params["apply_calibration"] = true;

    Json::Value result;
    REQUIRE_NOTHROW( result = runOp( "rs:cn_product_import", params ) );
    REQUIRE( result["calibration"]["applied"].asBool() == true );
    REQUIRE( result["radiometricState"].asString()
             == SatelliteProducts::kRadiometricStateRadiance );

    // Band 4: DN 2400 -> 2400 * 0.8 - 4 = 1916, but the declared sentinel
    // pixel stays -9999 (pre-fix it became -9999 * 0.8 - 4 = -8003.2).
    const std::vector<double> band4 = readBandAsDouble( output, 4 );
    constexpr int kN = 8;
    REQUIRE( band4.size() == static_cast<size_t>( kN ) * kN );
    REQUIRE( band4.front() == Approx( 2400.0 * 0.8 - 4.0 ).margin( 1e-9 ) );
    REQUIRE( band4.back() == -9999.0 );

    // The declaration survives the calibration pass as well.
    GDALAllRegister();
    GDALDatasetH ds = GDALOpen( output.toUtf8().constData(), GA_ReadOnly );
    REQUIRE( ds != nullptr );
    int hasNoData = 0;
    const double nd = GDALGetRasterNoDataValue( GDALGetRasterBand( ds, 4 ), &hasNoData );
    REQUIRE( hasNoData != 0 );
    REQUIRE( nd == -9999.0 );
    GDALClose( ds );
}
