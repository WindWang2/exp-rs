// tests/test_r33_t04_spectral_claim_contract.cpp — R33-T04 / WP10.
//
// The rs:spectral_index published-claim verifier proved geometry + identity
// stamp only — a foreign artifact with the SAME geometry carrying a COPIED
// identity stamp but a different dtype / CRS / affine could still be served
// as this run's product (zero-work success over bytes this run never
// wrote). The claim now proves the full data contract this run would
// produce: Float32, the input's CRS, and the complete affine (rotation and
// pixel-size terms included).
#include "support/http_range_server.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QString>
#include <QTemporaryDir>
#include <QUrl>

#include <gdal_priv.h>
#include <json/json.h>

#include <limits>
#include <string>
#include <vector>

#include "data/execution_identity_resolver.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/rs/rs_spectral_index_operator.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "runtime/observability/execution_telemetry.h"

using sicnu::operators::rs::RsSpectralIndexOperator;

namespace
{

constexpr int kWidth = 64;
constexpr int kHeight = 48;

/// The input's CRS (and, for the affine-only forgeries, the forged
/// artifact's): the claim verifier compares the artifact's projection
/// against THIS run's input, so a forge that must reach the affine check
/// has to carry the very same CRS.
const char *kInputProjection =
    "GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563]],"
    "PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]]";
/// A CRS the input never had — only the CRS check can reject it.
const char *kForeignProjection =
    "PROJCS[\"WGS 84 / UTM zone 33N\",GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\","
    "SPHEROID[\"WGS 84\",6378137,298.257223563]],PRIMEM[\"Greenwich\",0],"
    "UNIT[\"degree\",0.0174532925199433]],PROJECTION[\"Transverse_Mercator\"],"
    "PARAMETER[\"latitude_of_origin\",0],PARAMETER[\"central_meridian\",15],"
    "PARAMETER[\"scale_factor\",0.9996],PARAMETER[\"false_easting\",500000],"
    "PARAMETER[\"false_northing\",0],UNIT[\"metre\",1]]";

/// Records the published-claim demotion reasons the verifier emits (they
/// surface only through the CacheMiss telemetry event) and restores the
/// global telemetry state on scope exit.
struct DemotionReasonGuard
{
    sicnu::runtime::observability::ExecutionTelemetry &telemetry =
        sicnu::runtime::observability::ExecutionTelemetry::instance();
    const bool wasEnabled;

    DemotionReasonGuard()
      : wasEnabled( telemetry.isEnabled() )
    {
        telemetry.setEnabled( true );
        telemetry.clearEvents();
    }
    ~DemotionReasonGuard()
    {
        telemetry.setEnabled( wasEnabled );
        telemetry.clearEvents();
    }

    /// True when a published-claim demotion was recorded whose reason
    /// mentions @p needle ("affine", "CRS", ...).
    bool mentions( const std::string &needle ) const
    {
        for ( const auto &event : telemetry.events() )
        {
            if ( event.kind != sicnu::runtime::observability::EventKind::CacheMiss )
                continue;
            if ( event.subject.find( "published claim demoted to rebuild" )
                 == std::string::npos )
                continue;
            if ( event.subject.find( needle ) != std::string::npos )
                return true;
        }
        return false;
    }
};

void ensureGdal()
{
    GDALAllRegister();
}

void writeTwoBandRaster( const QString &path )
{
    ensureGdal();
    GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( drv != nullptr );
    GDALDataset *ds = drv->Create( path.toUtf8().constData(), kWidth, kHeight, 2,
                                   GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    std::vector<float> band( static_cast<std::size_t>( kWidth ) * kHeight );
    for ( int b = 1; b <= 2; ++b )
    {
        for ( int r = 0; r < kHeight; ++r )
            for ( int c = 0; c < kWidth; ++c )
                band[static_cast<std::size_t>( r ) * kWidth + c] =
                    b == 1 ? 2000.f + 40.f * ( ( r + c ) % 64 )
                           : 1000.f + 30.f * ( ( r * 2 + c ) % 64 );
        GDALRasterBand *rb = ds->GetRasterBand( b );
        rb->RasterIO( GF_Write, 0, 0, kWidth, kHeight, band.data(), kWidth, kHeight,
                      GDT_Float32, 0, 0 );
    }
    double gt[6] = { 0, 1, 0, static_cast<double>( kHeight ), 0, -1 };
    ds->SetGeoTransform( gt );
    ds->SetProjection( kInputProjection );
    GDALClose( ds );
}

Json::Value runNdvi( const QString &input, const QString &output, const QString &workDir,
                     const char *mode = nullptr )
{
    RsSpectralIndexOperator op;
    sicnu::operators::RSOperatorContext ctx( workDir.toStdString() );
    Json::Value params( Json::objectValue );
    params["input"] = input.toStdString();
    params["output"] = output.toStdString();
    params["index"] = "NDVI";
    params["nir"] = 1;
    params["red"] = 2;
    if ( mode )
        params["executionMode"] = mode;
    return op.run( params, ctx );
}

std::string identityStamp( const QString &path )
{
    GDALDatasetH ds = GDALOpen( path.toUtf8().constData(), GA_ReadOnly );
    if ( !ds )
        return {};
    const char *stamp = GDALGetMetadataItem( ds, "SICNU_SPECTRAL_IDENTITY", nullptr );
    const std::string out = stamp ? stamp : "";
    GDALClose( ds );
    return out;
}

/// Forge a same-geometry artifact that carries the REAL run's identity
/// stamp but a DIFFERENT data contract (Byte dtype / foreign CRS / foreign
/// affine). A Float32 forge carries the NaN NoData convention the operator
/// writes, so the CRS and affine checks are actually reached instead of
/// being masked by an earlier NoData mismatch (#2212); pass @p projection
/// (nullptr = none) to place the forge under a chosen CRS.
void forgeForeignArtifact( const QString &path, const std::string &stamp,
                           GDALDataType dtype, const double *gt,
                           const char *projection )
{
    ensureGdal();
    GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( drv != nullptr );
    GDALDataset *ds = drv->Create( path.toUtf8().constData(), kWidth, kHeight, 1, dtype,
                                   nullptr );
    REQUIRE( ds != nullptr );
    if ( gt )
        ds->SetGeoTransform( const_cast<double *>( gt ) );
    if ( projection )
        ds->SetProjection( projection );
    const std::size_t pixelBytes =
        static_cast<std::size_t>( GDALGetDataTypeSizeBytes( dtype ) );
    std::vector<unsigned char> bytes(
        static_cast<std::size_t>( kWidth ) * kHeight * pixelBytes, 7 );
    // Keep master's CE_None discipline on the pixel write (Fill no longer
    // applies: the forge writes a real buffer so dtype/NoData/CRS checks are
    // reachable — #2212).
    const CPLErr written = ds->GetRasterBand( 1 )->RasterIO(
        GF_Write, 0, 0, kWidth, kHeight, bytes.data(), kWidth, kHeight, dtype, 0, 0 );
    if ( dtype == GDT_Float32 )
        ds->GetRasterBand( 1 )->SetNoDataValue( std::numeric_limits<double>::quiet_NaN() );
    GDALSetMetadataItem( ds, "SICNU_SPECTRAL_IDENTITY", stamp.c_str(), nullptr );
    GDALClose( ds );
    REQUIRE( written == CE_None );
}
} // namespace

TEST_CASE( "NDVI rereads changed remote pixels even when the host still vouches for an older revision",
           "[operators][spectral][r33t04]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString fixture = dir.filePath( "input.tif" );
    writeTwoBandRaster( fixture );
    const auto payload = [&] {
        QFile file( fixture );
        REQUIRE( file.open( QIODevice::ReadOnly ) );
        const auto bytes = file.readAll();
        return std::vector<unsigned char>( bytes.begin(), bytes.end() );
    };
    sicnu::geo::testsupport::HttpRangeServer server( payload() );
    REQUIRE( server.port() > 0 );
    server.setEtag( "\"first\"" );
    const QString input = QString::fromStdString( "/vsicurl/" + server.url() );
    const QString output = dir.filePath( "ndvi.tif" );
    const auto *installed = sicnu::data::executionIdentityResolver();
    const auto previous = installed ? *installed : sicnu::data::InputIdentityResolver{};
    const auto restore = qScopeGuard( [&] { sicnu::data::setExecutionIdentityResolver( previous ); } );
    sicnu::data::setExecutionIdentityResolver( []( const QString & ) { return QStringLiteral( "first" ); } );
    const auto first = runNdvi( input, output, dir.path() );
    CHECK_FALSE( first["already_published"].asBool() );
    CHECK( identityStamp( output ).empty() );
    CHECK( server.rangedResponses() > 0 );
    GdalDatasetWrapper result;
    REQUIRE( result.open( output ) );
    float pixel = -1;
    REQUIRE( result.readBandWindow( 1, 0, 0, 1, 1, &pixel ) );
    CHECK( pixel == Catch::Approx( 1. / 3. ) );
    result.close();

    GDALDataset *dataset = static_cast<GDALDataset *>( GDALOpen( fixture.toUtf8().constData(), GA_Update ) );
    REQUIRE( dataset );
    const auto filled = dataset->GetRasterBand( 1 )->Fill( 1000 );
    GDALClose( dataset );
    REQUIRE( filled == CE_None );
    server.replacePayload( payload(), "\"second\"", {} );
    const auto second = runNdvi( input, output, dir.path() );
    CHECK( second["tiles_computed"].asUInt64() > 0 );
    CHECK_FALSE( second["already_published"].asBool() );
    REQUIRE( result.open( output ) );
    REQUIRE( result.readBandWindow( 1, 0, 0, 1, 1, &pixel ) );
    CHECK( pixel == 0.f ); // The replaced NIR sample now equals red.
    result.close();

    try {
        runNdvi( input, output, dir.path(), "resumable" );
        FAIL( "An unversioned input cannot certify a checkpoint" );
    } catch ( const sicnu::operators::RSOperatorError &error ) {
        CHECK( error.code() == sicnu::operators::ErrorCode::InvalidInputData );
    }

}

TEST_CASE( "NDVI resumes an unchanged local raster opened through a percent-encoded file URL",
           "[operators][spectral][r33t04]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString input = dir.filePath( QStringLiteral( "测试 影像.tif" ) );
    writeTwoBandRaster( input );
    const QString url = QString::fromLatin1( QUrl::fromLocalFile( input ).toEncoded() );
    const QString output = dir.filePath( "ndvi.tif" );
    CHECK_FALSE( runNdvi( url, output, dir.path(), "resumable" )["already_published"].asBool() );
    CHECK( runNdvi( url, output, dir.path(), "resumable" )["already_published"].asBool() );
}

TEST_CASE( "a forged same-geometry artifact with a copied stamp never claims zero-work",
           "[operators][spectral][r33t04]" )
{
    // The verifier's refusal reasons are only observable through the
    // CacheMiss telemetry event — record them for the reason assertions
    // below (#2212).
    DemotionReasonGuard reasons;

    QTemporaryDir dir;
    const QString input = dir.filePath( "in.tif" );
    const QString output = dir.filePath( "ndvi.tif" );
    const QString workDir = dir.filePath( "scratch" );
    writeTwoBandRaster( input );

    // Baseline: the run publishes with its identity stamp.
    {
        const Json::Value first = runNdvi( input, output, workDir );
        REQUIRE( first["already_published"].asBool() == false );
        REQUIRE( !identityStamp( output ).empty() );
        // Untouched rerun is the honest zero-work hit.
        const Json::Value second = runNdvi( input, output, workDir );
        REQUIRE( second["already_published"].asBool() == true );
    }

    SECTION( "foreign dtype with a copied stamp is rebuilt" )
    {
        const std::string stamp = identityStamp( output );
        REQUIRE( QFile::remove( output ) );
        forgeForeignArtifact( output, stamp, GDT_Byte, nullptr, kInputProjection );

        const Json::Value res = runNdvi( input, output, workDir );
        REQUIRE( res["already_published"].asBool() == false ); // rebuilt
        CHECK( reasons.mentions( "dtype" ) );
        // The rebuilt artifact is this run's REAL product again.
        GDALDatasetH ds = GDALOpen( output.toUtf8().constData(), GA_ReadOnly );
        REQUIRE( ds != nullptr );
        CHECK( GDALGetRasterDataType( GDALGetRasterBand( ds, 1 ) ) == GDT_Float32 );
        GDALClose( ds );
    }

    SECTION( "rotated/foreign affine with a copied stamp is rebuilt" )
    {
        const std::string stamp = identityStamp( output );
        REQUIRE( QFile::remove( output ) );
        const double rotated[6] = { 0, 0.5, 0.25, static_cast<double>( kHeight ), 0.1, -0.5 };
        // Same CRS and the NaN NoData convention: dtype/CRS/NoData all pass,
        // so ONLY the affine check can refuse this forge.
        forgeForeignArtifact( output, stamp, GDT_Float32, rotated, kInputProjection );

        const Json::Value res = runNdvi( input, output, workDir );
        REQUIRE( res["already_published"].asBool() == false ); // rebuilt
        CHECK( reasons.mentions( "affine" ) );
        GDALDatasetH ds = GDALOpen( output.toUtf8().constData(), GA_ReadOnly );
        REQUIRE( ds != nullptr );
        double gt[6] = { 0, 0, 0, 0, 0, 0 };
        GDALGetGeoTransform( ds, gt );
        CHECK( gt[2] == 0.0 ); // rotation term restored to the input's axis-aligned grid
        GDALClose( ds );
    }

    SECTION( "rotation-only affine change with a copied stamp is rebuilt" )
    {
        // Only the rotation terms move (gt[2]/gt[4]); the pixel size stays
        // the input's — an affine check that compared pixel size alone
        // would accept this forge.
        const std::string stamp = identityStamp( output );
        REQUIRE( QFile::remove( output ) );
        const double rotatedOnly[6] = { 0, 1, 0.25, static_cast<double>( kHeight ), 0.1, -1 };
        forgeForeignArtifact( output, stamp, GDT_Float32, rotatedOnly, kInputProjection );

        const Json::Value res = runNdvi( input, output, workDir );
        REQUIRE( res["already_published"].asBool() == false ); // rebuilt
        CHECK( reasons.mentions( "affine" ) );
    }

    SECTION( "foreign CRS with a copied stamp is rebuilt" )
    {
        const std::string stamp = identityStamp( output );
        REQUIRE( QFile::remove( output ) );
        const double aligned[6] = { 0, 1, 0, static_cast<double>( kHeight ), 0, -1 };
        // Float32 + NaN NoData + the input's affine: only the CRS differs.
        forgeForeignArtifact( output, stamp, GDT_Float32, aligned, kForeignProjection );

        const Json::Value res = runNdvi( input, output, workDir );
        REQUIRE( res["already_published"].asBool() == false ); // rebuilt
        CHECK( reasons.mentions( "CRS" ) );
    }
}
