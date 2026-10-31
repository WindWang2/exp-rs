// test_r25_a_optical_chain_e2e.cpp — R25-A W7: the optical preprocessing
// chain from the task book §6.W7, end to end through the REAL operator
// entry points (registry → GDAL product → next operator):
//
//   product (4 bands: B,G,R,NIR + SCL QA band with a cloud pixel)
//     → rs:qa_mask (SCL cloud screening)
//     → rs:apply_mask (NoData stamped over the cloud pixel; WAVELENGTH_UNITS
//       band metadata must survive the seam — DEF-6)
//     → rs:spectral_index NDVI (NoData must PROPAGATE: the masked pixel
//       reports NoData, not a fabricated index value)
//
// Oracle: 2x2 grid, reflectance-scale values.
//   pixels: (0,0) healthy veg NIR=0.5 R=0.1 → NDVI=0.667
//           (1,0) bare soil NIR=0.25 R=0.2 → NDVI=0.111
//           (0,1)/(1,1) water-ish NIR=0.02 R=0.03 → NDVI=-0.2
//   SCL band marks (0,1) as cloud-medium (8) → standard masks it, relaxed
//   keeps it; after masking, NDVI at (0,1) = NoData, others unchanged.

#include "synthetic_raster_builder.h"

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QString>
#include <QTemporaryDir>

#include <gdal.h>
#include <json/json.h>

#include <cmath>
#include <vector>

using namespace sicnu::testing;
using namespace sicnu::operators;

namespace
{

struct RunResult
{
    Json::Value json;
    bool ok = false;
    RSOperatorError error = RSOperatorError( ErrorCode::Unknown, "" );
};

RunResult runOperator( const std::string &id, const Json::Value &params )
{
    auto op = RSOperatorRegistry::instance().create( id );
    RunResult run;
    if ( op == nullptr )
    {
        run.error = RSOperatorError( ErrorCode::InvalidParameter, "operator not registered" );
        return run;
    }
    RSOperatorContext context;
    try
    {
        run.json = op->run( params, context );
        run.ok = true;
    }
    catch ( const RSOperatorError &e )
    {
        run.error = e;
    }
    return run;
}

/// 2x2, 5-band product: B, G, R, NIR reflectance + SCL QA (band 5,
/// role-tagged scene_classification). Wavelengths stamped in μm on the
/// reflectance bands to verify metadata survives the masking seam.
QString writeProduct( const QTemporaryDir &dir )
{
    RsSyntheticRasterBuilder b( 2, 2, 5, GDT_Float32 );
    b.withCrs( "EPSG:32650" );
    b.withGeoTransform( 500000.0, 10.0, 2000000.0, -10.0 );
    b.withNoData( -9999.0 ); // apply_mask requires declared per-band NoData
    // band 1 blue / 2 green / 3 red / 4 nir
    const float red[4] = { 0.10f, 0.20f, 0.03f, 0.04f };
    const float nir[4] = { 0.50f, 0.25f, 0.02f, 0.03f };
    for ( int i = 0; i < 4; ++i )
    {
        const int x = i % 2;
        const int y = i / 2;
        b.withPixel( 1, x, y, 0.05f );
        b.withPixel( 2, x, y, 0.08f );
        b.withPixel( 3, x, y, red[i] );
        b.withPixel( 4, x, y, nir[i] );
        // SCL: vegetation(4) except pixel (0,1) = cloud medium probability.
        b.withPixel( 5, x, y, ( x == 0 && y == 1 ) ? 8.0f : 4.0f );
    }
    return b.writeToDisk( dir.filePath( "product.tif" ) );
}

void stampProductMetadata( const QString &path )
{
    GDALDatasetH ds = GDALOpen( path.toUtf8().constData(), GA_Update );
    REQUIRE( ds != nullptr );
    GDALRasterBandH scl = GDALGetRasterBand( ds, 5 );
    REQUIRE( scl != nullptr );
    GDALSetMetadataItem( scl, "SICNU_BAND_ROLE", "scene_classification", nullptr );
    // Reflectance bands: role-tagged + μm wavelengths (units must survive
    // rs:apply_mask — DEF-6).
    struct Role { int band; const char *role; const char *wl; };
    const Role roles[4] = { { 1, "blue", "0.49" },  { 2, "green", "0.56" },
                            { 3, "red", "0.665" }, { 4, "nir", "0.84" } };
    for ( const Role &r : roles )
    {
        GDALRasterBandH band = GDALGetRasterBand( ds, r.band );
        REQUIRE( band != nullptr );
        GDALSetMetadataItem( band, "SICNU_BAND_ROLE", r.role, nullptr );
        GDALSetMetadataItem( band, "WAVELENGTH", r.wl, nullptr );
        GDALSetMetadataItem( band, "WAVELENGTH_UNITS", "Micrometers", nullptr );
    }
    GDALClose( ds );
}

} // namespace

TEST_CASE( "optical chain: qa_mask -> apply_mask -> NDVI NoData propagation",
           "[operators][optical][chain][r25a][e2e]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString product = writeProduct( dir );
    REQUIRE( !product.isEmpty() );
    stampProductMetadata( product );

    // 1) QA mask: standard cloud screening masks the SCL-8 pixel.
    Json::Value qaParams( Json::objectValue );
    qaParams["input"] = product.toStdString();
    qaParams["output"] = dir.filePath( "cloudmask.tif" ).toStdString();
    qaParams["mask"] = "cloud";
    const RunResult qa = runOperator( "rs:qa_mask", qaParams );
    REQUIRE( qa.ok );
    REQUIRE( qa.json["source"].asString() == "sentinel2_scl" );
    CHECK( qa.json["maskedPixels"].asUInt64() == 1 );
    CHECK( qa.json["sclClassHistogram"]["8"].asUInt64() == 1 );
    CHECK( qa.json["sclClassHistogram"]["4"].asUInt64() == 3 );

    GdalDatasetWrapper maskDs;
    REQUIRE( maskDs.open( dir.filePath( "cloudmask.tif" ) ) );
    std::vector<uint8_t> mask( 4 );
    REQUIRE( maskDs.readBandWindowNative( 1, 0, 0, 2, 2, mask.data() ) );
    CHECK( mask == std::vector<uint8_t>{ 0, 0, 1, 0 } ); // only (0,1)

    // 2) apply_mask: the cloud pixel becomes NoData on ALL reflectance
    //    bands; μm wavelength metadata survives the seam.
    Json::Value maskParams( Json::objectValue );
    maskParams["input"] = product.toStdString();
    maskParams["mask"] = dir.filePath( "cloudmask.tif" ).toStdString();
    maskParams["output"] = dir.filePath( "masked.tif" ).toStdString();
    const RunResult masked = runOperator( "rs:apply_mask", maskParams );
    REQUIRE( masked.ok );

    GdalDatasetWrapper maskedDs;
    REQUIRE( maskedDs.open( dir.filePath( "masked.tif" ) ) );
    bool hasNd = false;
    const double nd = maskedDs.bandNoDataValue( 4, &hasNd );
    REQUIRE( hasNd );
    std::vector<float> nirBand( 4 );
    REQUIRE( maskedDs.readBandData( 4, nirBand.data(), 2, 2 ) );
    CHECK( ( std::isnan( nirBand[2] ) || nirBand[2] == static_cast<float>( nd ) ) );
    CHECK( nirBand[0] == Catch::Approx( 0.50f ) );

    GDALDatasetH maskedH = GDALOpen( dir.filePath( "masked.tif" ).toUtf8().constData(),
                                     GA_ReadOnly );
    REQUIRE( maskedH != nullptr );
    GDALRasterBandH nirBandH = GDALGetRasterBand( maskedH, 4 );
    const char *units = GDALGetMetadataItem( nirBandH, "WAVELENGTH_UNITS", nullptr );
    CHECK( units != nullptr ); // DEF-6: units travel with the values
    if ( units )
        CHECK( QString::fromUtf8( units ).compare( QStringLiteral( "Micrometers" ),
                                                   Qt::CaseInsensitive ) == 0 );
    GDALClose( maskedH );

    // 3) NDVI on the masked product: the cloud pixel must be NoData, the
    //    healthy-vegetation pixel 0.667, bare soil 0.111 — the mask
    //    PROPAGATES through the index instead of fabricating a value from
    //    NoData-contaminated bands.
    Json::Value ndviParams( Json::objectValue );
    ndviParams["input"] = dir.filePath( "masked.tif" ).toStdString();
    ndviParams["output"] = dir.filePath( "ndvi.tif" ).toStdString();
    ndviParams["index"] = "NDVI";
    ndviParams["red"] = 3;
    ndviParams["nir"] = 4;
    const RunResult ndvi = runOperator( "rs:spectral_index", ndviParams );
    REQUIRE( ndvi.ok );

    GdalDatasetWrapper ndviDs;
    REQUIRE( ndviDs.open( dir.filePath( "ndvi.tif" ) ) );
    std::vector<float> values( 4 );
    REQUIRE( ndviDs.readBandData( 1, values.data(), 2, 2 ) );
    CHECK( std::isnan( values[2] ) ); // masked cloud pixel → NoData
    CHECK( values[0] == Catch::Approx( ( 0.5f - 0.1f ) / ( 0.5f + 0.1f ) ).epsilon( 1e-5 ) );
    CHECK( values[1] == Catch::Approx( ( 0.25f - 0.2f ) / ( 0.25f + 0.2f ) ).epsilon( 1e-5 ) );
    CHECK( values[3] == Catch::Approx( ( 0.03f - 0.04f ) / ( 0.03f + 0.04f ) ).epsilon( 1e-4 ) );

    // 4) Chain determinism: a second full run of steps 1-3 is bit-identical
    //    (same fixture, same outputs read back equal).
    const RunResult qa2 = runOperator( "rs:qa_mask", qaParams );
    REQUIRE( qa2.ok );
    CHECK( qa2.json["maskedPixels"].asUInt64() == qa.json["maskedPixels"].asUInt64() );
    const RunResult ndvi2 = runOperator( "rs:spectral_index", ndviParams );
    REQUIRE( ndvi2.ok );
    GdalDatasetWrapper ndviDs2;
    REQUIRE( ndviDs2.open( dir.filePath( "ndvi.tif" ) ) );
    std::vector<float> values2( 4 );
    REQUIRE( ndviDs2.readBandData( 1, values2.data(), 2, 2 ) );
    for ( int i = 0; i < 4; ++i )
    {
        CHECK( std::isnan( values2[i] ) == std::isnan( values[i] ) );
        if ( !std::isnan( values[i] ) )
            CHECK( values2[i] == values[i] );
    }
}
