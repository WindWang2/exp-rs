// R27-T01 science oracles — RED→GREEN regression suite for the numeric
// correctness / calibration semantics / verifiable output quality defects
// fixed by this track (.planning/r27-t01/ORACLES.md; issues #1738 #1739
// #1740 #1752–#1759 #1801).
//
// Every expected value is derived from an INDEPENDENT reference (manual
// arithmetic against published formulas, or sibling-kernel semantics), never
// from the production implementation under test.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <gdal_priv.h>
#include <gdalwarper.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <vector>

#include "processing/algorithms/atmospheric_correction.h"
#include "processing/algorithms/change_detection.h"
#include "processing/algorithms/glcm_texture.h"
#include "processing/algorithms/image_enhancement_streaming.h"
#include "processing/algorithms/image_fusion.h"
#include "processing/algorithms/radiometric_calibration.h"
#include "processing/algorithms/sar/sar_metadata.h"
#include "processing/algorithms/satellite_products.h"
#include "processing/algorithms/terrain_analysis.h"
#include "processing/algorithms/terrain_flow.h"

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_registry.h"

namespace
{

constexpr float kNodata = -9999.0f;
constexpr float kInf = std::numeric_limits<float>::infinity();
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

Catch::Approx approx( double v )
{
    return Catch::Approx( v ).epsilon( 1e-6 );
}

void ensureGdal()
{
    GDALAllRegister();
}

// ---------------------------------------------------------------------------
// Tiny GeoTIFF helpers (deterministic fixtures).
// ---------------------------------------------------------------------------

bool writeTif( const QString &path, GDALDataType type, int width, int height,
               int bands, const std::vector<std::vector<float>> &data,
               const std::vector<std::pair<std::string, std::string>> &meta = {},
               bool declareNodata = false, double nodataValue = 0.0 )
{
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
        return false;
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), width, height,
                                  bands, type, nullptr );
    if ( !ds )
        return false;
    const std::array<double, 6> gt = { 116.0, 0.0001, 0.0, 40.0, 0.0, -0.0001 };
    GDALSetGeoTransform( ds, const_cast<double *>( gt.data() ) );
    GDALSetProjection( ds, "GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563]],PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]]" );
    for ( const auto &kv : meta )
        GDALSetMetadataItem( ds, kv.first.c_str(), kv.second.c_str(), nullptr );
    for ( int b = 1; b <= bands; ++b ) {
        GDALRasterBandH band = GDALGetRasterBand( ds, b );
        if ( declareNodata )
            GDALSetRasterNoDataValue( band, nodataValue );
        // The fixture buffer is always float; let GDAL convert it to the
        // band's type (passing `type` here would reinterprets the float bits
        // as integers).
        const int e = GDALRasterIO( band, GF_Write, 0, 0, width, height,
                                    const_cast<float *>( data[static_cast<size_t>( b - 1 )].data() ),
                                    width, height, GDT_Float32, 0, 0 );
        if ( e != CE_None ) {
            GDALClose( ds );
            return false;
        }
    }
    GDALClose( ds );
    return true;
}

std::vector<float> readBand1( const QString &path, int *w = nullptr, int *h = nullptr )
{
    GDALDatasetH ds = GDALOpen( path.toUtf8().constData(), GA_ReadOnly );
    if ( !ds )
        return {};
    const int width = GDALGetRasterXSize( ds );
    const int height = GDALGetRasterYSize( ds );
    std::vector<float> out( static_cast<size_t>( width ) * height );
    GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Read, 0, 0, width, height,
                  out.data(), width, height, GDT_Float32, 0, 0 );
    if ( w )
        *w = width;
    if ( h )
        *h = height;
    GDALClose( ds );
    return out;
}

// ---------------------------------------------------------------------------
// Synthetic Sentinel-2 L2A SAFE fixture (PB>=04.00 carries BOA_ADD_OFFSET).
// ---------------------------------------------------------------------------

void writeTinyS2Band( const QString &path, float value )
{
    const int W = 4, H = 4;
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), W, H, 1,
                                  GDT_UInt16, nullptr );
    REQUIRE( ds != nullptr );
    std::vector<uint16_t> data( static_cast<size_t>( W ) * H,
                                static_cast<uint16_t>( value ) );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Write, 0, 0, W, H,
                           data.data(), W, H, GDT_UInt16, 0, 0 ) == CE_None );
    GDALClose( ds );
}

// PB>=04.00 numbers: DN = rho*10000 - 1000  =>  rho = (DN + 1000)/10000.
// Hand reference with B02=1800 B03=2500 B04=1500 B08=5500:
//   rho_blue=0.28 rho_green=0.35 rho_red=0.25 rho_nir=0.65
//   NDVI = (0.65-0.25)/(0.65+0.25) = 0.4444444…
//   EVI  = 2.5*(0.40)/(0.65+6*0.25-7.5*0.28+1) = 1.0/1.05 = 0.9523810…
//   SAVI(L=0.5) = (0.40*1.5)/(0.65+0.25+0.5) = 0.6/1.4 = 0.4285714…
//   NDWI = (0.35-0.65)/(0.35+0.65) = -0.3
QString makeS2Safe( const QTemporaryDir &tmp, const char *boaOffsetTag,
                     const QString &name, const char *offsetForm = "plain" )
{
    const QString safe = tmp.path() + "/" + name + ".SAFE";
    const QString img = safe + "/GRANULE/L2A_T32TQQ/IMG_DATA/R10m";
    QDir().mkpath( img );
    // Real ESA MTDs carry attributes on the quantification tag and write the
    // offset PER BAND — either attributed direct entries or a nested list
    // (see radiometric_calibration.cpp's parser); "plain" is the legacy
    // fixture form.
    QString radiometric =
        "<BOA_QUANTIFICATION_VALUE unit=\"none\">10000</BOA_QUANTIFICATION_VALUE>";
    if ( boaOffsetTag ) {
        if ( offsetForm == std::string( "bandid" ) )
            radiometric += QString( "<BOA_ADD_OFFSET band_id=\"1\">" ) + boaOffsetTag
                           + "</BOA_ADD_OFFSET>"
                           + "<BOA_ADD_OFFSET band_id=\"2\">" + boaOffsetTag
                           + "</BOA_ADD_OFFSET>";
        else if ( offsetForm == std::string( "list" ) )
            radiometric += QString( "<BOA_ADD_OFFSET><BOA_LIST_TO_VALUES>" )
                           + "<BOA_LIST_VALUE band_id=\"1\">" + boaOffsetTag
                           + "</BOA_LIST_VALUE>"
                           + "<BOA_LIST_VALUE band_id=\"2\">" + boaOffsetTag
                           + "</BOA_LIST_VALUE>"
                           + "</BOA_LIST_TO_VALUES></BOA_ADD_OFFSET>";
        else if ( offsetForm == std::string( "disagree" ) )
            radiometric += QString( "<BOA_ADD_OFFSET band_id=\"1\">" ) + boaOffsetTag
                           + "</BOA_ADD_OFFSET>"
                           + "<BOA_ADD_OFFSET band_id=\"2\">-500</BOA_ADD_OFFSET>";
        else
            radiometric += QString( "<BOA_ADD_OFFSET>" ) + boaOffsetTag + "</BOA_ADD_OFFSET>";
    }
    QFile mtd( safe + "/MTD_MSIL2A.xml" );
    REQUIRE( mtd.open( QIODevice::WriteOnly | QIODevice::Text ) );
    mtd.write( ( "<n1:Level-2A_User_Product>"
                 "<General_Info><Product_Image_Characteristics><Radiometric_Info>"
                 + radiometric +
                 "</Radiometric_Info></Product_Image_Characteristics></General_Info>"
                 "</n1:Level-2A_User_Product>" )
                   .toUtf8() );
    mtd.close();
    Q_UNUSED( boaOffsetTag );
    writeTinyS2Band( img + "/T32TQQ_20240615T000000_B02_10m.tif", 1800.f );
    writeTinyS2Band( img + "/T32TQQ_20240615T000000_B03_10m.tif", 2500.f );
    writeTinyS2Band( img + "/T32TQQ_20240615T000000_B04_10m.tif", 1500.f );
    writeTinyS2Band( img + "/T32TQQ_20240615T000000_B08_10m.tif", 5500.f );
    return safe;
}

// ---------------------------------------------------------------------------
// Synthetic Landsat C2 L2 scene (MTL + one band file per reflectance band).
// rho = DN*REFLECTANCE_MULT_BAND_n + REFLECTANCE_ADD_BAND_n.
// Hand reference (MULT=2.5e-5, ADD=-0.2):
//   B2 DN=8800 -> 0.02, B3 DN=11200 -> 0.08, B4 DN=10400 -> 0.06, B5 DN=16800 -> 0.22
//   NDVI = (0.22-0.06)/(0.22+0.06) = 0.5714286…
//   EVI  = 2.5*0.16/(0.22+0.36-0.15+1) = 0.4/1.43 = 0.2797203…
//   SAVI = (0.16*1.5)/(0.22+0.06+0.5) = 0.3076923…
// ---------------------------------------------------------------------------

void writeTinyLandsatBand( const QString &path, const std::vector<uint16_t> &data, int W, int H )
{
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), W, H, 1, GDT_UInt16, nullptr );
    REQUIRE( ds != nullptr );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Write, 0, 0, W, H,
                           const_cast<uint16_t *>( data.data() ), W, H, GDT_UInt16, 0, 0 ) == CE_None );
    GDALClose( ds );
}

QString makeLandsatScene( const QTemporaryDir &tmp, bool withDn0Fill = false )
{
    const QString dir = tmp.path() + "/LC08_L2SP_123032_20240615";
    QDir().mkpath( dir );
    constexpr int W = 4, H = 4;
    auto flat = [&]( uint16_t v ) {
        return std::vector<uint16_t>( W * H, v );
    };
    // B2 carries USGS C2 DN0 fill pixels; the other bands stay fill-free so
    // the "no spurious stamp" assertion has a clean control band.
    std::vector<uint16_t> b2 = flat( 8800 );
    if ( withDn0Fill ) {
        for ( int r = 0; r < H; ++r )
            b2[static_cast<size_t>( r * W )] = 0;
    }
    writeTinyLandsatBand( dir + "/LC08_L2SP_123032_20240615_02_T1_B2.TIF", b2, W, H );
    writeTinyLandsatBand( dir + "/LC08_L2SP_123032_20240615_02_T1_B3.TIF", flat( 11200 ), W, H );
    writeTinyLandsatBand( dir + "/LC08_L2SP_123032_20240615_02_T1_B4.TIF", flat( 10400 ), W, H );
    writeTinyLandsatBand( dir + "/LC08_L2SP_123032_20240615_02_T1_B5.TIF", flat( 16800 ), W, H );
    QFile mtl( dir + "/LC08_L2SP_123032_20240615_02_MTL.txt" );
    REQUIRE( mtl.open( QIODevice::WriteOnly | QIODevice::Text ) );
    mtl.write(
        "GROUP = LANDSAT_METADATA_FILE\n"
        "  LANDSAT_PRODUCT_ID = \"LC08_L2SP_123032_20240615_02_T1\"\n"
        "  SPACECRAFT_ID = \"LANDSAT_8\"\n"
        "  COLLECTION_NUMBER = 02\n"
        "  COLLECTION_CATEGORY = \"T1\"\n"
        "  PROCESSING_LEVEL = \"L2SP\"\n"
        "  DATE_ACQUIRED = 2024-06-15\n"
        "  SCENE_CENTER_TIME = \"02:30:00Z\"\n"
        "  GROUP = IMAGE_ATTRIBUTES\n"
        "    SUN_ELEVATION = 60.0\n"
        "    CLOUD_COVER = 0.0\n"
        "  END_GROUP = IMAGE_ATTRIBUTES\n"
        "  GROUP = LEVEL1_PROCESSING_RECORD\n"
        "    FILE_NAME_BAND_2 = \"LC08_L2SP_123032_20240615_02_T1_B2.TIF\"\n"
        "    FILE_NAME_BAND_3 = \"LC08_L2SP_123032_20240615_02_T1_B3.TIF\"\n"
        "    FILE_NAME_BAND_4 = \"LC08_L2SP_123032_20240615_02_T1_B4.TIF\"\n"
        "    FILE_NAME_BAND_5 = \"LC08_L2SP_123032_20240615_02_T1_B5.TIF\"\n"
        "    FILE_NAME_BAND_QA_PIXEL = \"LC08_L2SP_123032_20240615_02_T1_QA_PIXEL.TIF\"\n"
        "  END_GROUP = LEVEL1_PROCESSING_RECORD\n"
        "  GROUP = LEVEL2_SURFACE_REFLECTANCE_PARAMETERS\n"
        "    REFLECTANCE_MULT_BAND_1 = 2.5e-05\n"
        "    REFLECTANCE_ADD_BAND_1 = -0.2\n"
        "    REFLECTANCE_MULT_BAND_2 = 2.5e-05\n"
        "    REFLECTANCE_ADD_BAND_2 = -0.2\n"
        "    REFLECTANCE_MULT_BAND_3 = 2.5e-05\n"
        "    REFLECTANCE_ADD_BAND_3 = -0.2\n"
        "    REFLECTANCE_MULT_BAND_4 = 2.5e-05\n"
        "    REFLECTANCE_ADD_BAND_4 = -0.2\n"
        "    REFLECTANCE_MULT_BAND_5 = 2.5e-05\n"
        "    REFLECTANCE_ADD_BAND_5 = -0.2\n"
        "    REFLECTANCE_MULT_BAND_6 = 2.5e-05\n"
        "    REFLECTANCE_ADD_BAND_6 = -0.2\n"
        "    REFLECTANCE_MULT_BAND_7 = 2.5e-05\n"
        "    REFLECTANCE_ADD_BAND_7 = -0.2\n"
        "  END_GROUP = LEVEL2_SURFACE_REFLECTANCE_PARAMETERS\n"
        "END_GROUP = LANDSAT_METADATA_FILE\n"
        "END\n" );
    mtl.close();
    return dir + "/LC08_L2SP_123032_20240615_02_MTL.txt";
}

} // namespace

// ===========================================================================
// A1 (#1738): ±Inf DEM pixels must behave exactly like NaN/NoData across the
// terrain family — never enter a Horn window as an elevation.
// ===========================================================================

TEST_CASE( "R27-A1 terrain slope treats Inf like NoData", "[r27][terrain][inf]" )
{
    // Parabolic surface z = r*r + c, cellSize 1.
    const auto demValue = []( int r, int c ) { return static_cast<float>( r * r + c ); };
    constexpr int W = 5, H = 5;

    SECTION( "Inf centre -> nodata output" )
    {
        std::vector<float> dem( W * H ), out( W * H, 0 );
        for ( int r = 0; r < H; ++r )
            for ( int c = 0; c < W; ++c )
                dem[static_cast<size_t>( r * W + c )] = demValue( r, c );
        dem[2 * W + 2] = kInf;
        REQUIRE( TerrainAnalysis::slope( dem.data(), out.data(), W, H, 1.0f, kNodata ) );
        REQUIRE( out[2 * W + 2] == kNodata );
        // -Inf too
        dem[2 * W + 2] = -kInf;
        REQUIRE( TerrainAnalysis::slope( dem.data(), out.data(), W, H, 1.0f, kNodata ) );
        REQUIRE( out[2 * W + 2] == kNodata );
    }

    SECTION( "Inf neighbour -> partial-derivative estimate, never 90 degrees" )
    {
        std::vector<float> dem( W * H ), out( W * H, 0 );
        for ( int r = 0; r < H; ++r )
            for ( int c = 0; c < W; ++c )
                dem[static_cast<size_t>( r * W + c )] = demValue( r, c );
        // East neighbour of centre (2,3) is Inf. Expected (invalid-neighbour
        // fallback): dzdx uses (z-d) only: z(2,2)=6, d(2,1)=5 -> dzdx=1;
        // dzdy=(h-b)/2=(11-3)/2=4 -> slope=atan(sqrt(1+16)) deg.
        dem[2 * W + 3] = kInf;
        REQUIRE( TerrainAnalysis::slope( dem.data(), out.data(), W, H, 1.0f, kNodata ) );
        const double expected =
            std::atan( std::sqrt( 1.0 * 1.0 + 4.0 * 4.0 ) ) * 180.0 / M_PI;
        REQUIRE( out[2 * W + 2] == approx( expected ) );
        // The broken isnan-only path produced exactly 90 (atan(inf)).
        REQUIRE( out[2 * W + 2] < 89.9 );
    }

    SECTION( "Inf neighbour identical to NaN neighbour" )
    {
        std::vector<float> demA( W * H ), demB( W * H ), outA( W * H ), outB( W * H );
        for ( int r = 0; r < H; ++r )
            for ( int c = 0; c < W; ++c ) {
                demA[static_cast<size_t>( r * W + c )] = demValue( r, c );
                demB[static_cast<size_t>( r * W + c )] = demValue( r, c );
            }
        demA[1 * W + 1] = kInf;  // diagonal neighbour of centre
        demB[1 * W + 1] = kNaN;  // same position as NaN
        REQUIRE( TerrainAnalysis::slope( demA.data(), outA.data(), W, H, 1.0f, kNodata ) );
        REQUIRE( TerrainAnalysis::slope( demB.data(), outB.data(), W, H, 1.0f, kNodata ) );
        for ( size_t i = 0; i < outA.size(); ++i )
            REQUIRE( outA[i] == outB[i] );
    }

    SECTION( "all-Inf DEM -> all nodata, no finite leak" )
    {
        std::vector<float> dem( W * H, kInf ), out( W * H, 0 );
        REQUIRE( TerrainAnalysis::slope( dem.data(), out.data(), W, H, 1.0f, kNodata ) );
        for ( float v : out )
            REQUIRE( v == kNodata );
    }
}

TEST_CASE( "R27-A1 terrain family Inf parity (aspect/hillshade/stats/curvature)",
           "[r27][terrain][inf]" )
{
    constexpr int W = 5, H = 5;
    const auto demValue = []( int r, int c ) { return static_cast<float>( 100 + 10 * r + c ); };
    std::vector<float> dem( W * H ), out( W * H );
    for ( int r = 0; r < H; ++r )
        for ( int c = 0; c < W; ++c )
            dem[static_cast<size_t>( r * W + c )] = demValue( r, c );

    SECTION( "Inf centre -> nodata for aspect/hillshade/roughness/tri/tpi" )
    {
        dem[2 * W + 2] = kInf;
        REQUIRE( TerrainAnalysis::aspect( dem.data(), out.data(), W, H, 1.0f, kNodata ) );
        REQUIRE( out[2 * W + 2] == kNodata );
        REQUIRE( TerrainAnalysis::hillshade( dem.data(), out.data(), W, H, 1.0f, kNodata,
                                             315.0f, 45.0f ) );
        REQUIRE( out[2 * W + 2] == kNodata );
        REQUIRE( TerrainAnalysis::roughness( dem.data(), out.data(), W, H, kNodata ) );
        REQUIRE( out[2 * W + 2] == kNodata );
        REQUIRE( TerrainAnalysis::tri( dem.data(), out.data(), W, H, kNodata ) );
        REQUIRE( out[2 * W + 2] == kNodata );
        REQUIRE( TerrainAnalysis::tpi( dem.data(), out.data(), W, H, kNodata ) );
        REQUIRE( out[2 * W + 2] == kNodata );
        REQUIRE( TerrainAnalysis::curvatureProfile( dem.data(), out.data(), W, H, 1.0f, 1.0f,
                                                    kNodata ) );
        REQUIRE( out[2 * W + 2] == kNodata );
    }

    SECTION( "Inf neighbour skipped in TRI like a NaN neighbour" )
    {
        std::vector<float> triInf( W * H ), triNan( W * H ), roughInf( W * H );
        dem[2 * W + 3] = kInf;
        REQUIRE( TerrainAnalysis::tri( dem.data(), triInf.data(), W, H, kNodata ) );
        REQUIRE( TerrainAnalysis::roughness( dem.data(), roughInf.data(), W, H, kNodata ) );
        dem[2 * W + 3] = kNaN;
        REQUIRE( TerrainAnalysis::tri( dem.data(), triNan.data(), W, H, kNodata ) );
        const std::vector<float> &outInf = triInf;
        const std::vector<float> &outNan = triNan;
        for ( size_t i = 0; i < outInf.size(); ++i ) {
            REQUIRE( outInf[i] == outNan[i] );
            // TRI over the planar ramp with one neighbour removed stays finite.
            REQUIRE( std::isfinite( outInf[i] ) );
        }
    }

    SECTION( "flow: Inf cells are NoData barriers, not infinite sinks" )
    {
        std::vector<float> demF( W * H ), filled( W * H, 0 ), dir( W * H, 0 );
        for ( int r = 0; r < H; ++r )
            for ( int c = 0; c < W; ++c )
                demF[static_cast<size_t>( r * W + c )] = 50.0f - 5.0f * r - c;
        demF[2 * W + 2] = kInf; // a wall of Inf must not be routed across
        REQUIRE( TerrainFlow::fillDepressions( demF.data(), filled.data(), W, H, kNodata ) );
        // The Inf cell itself must not appear as a finite filled elevation.
        REQUIRE( !std::isfinite( filled[2 * W + 2] ) );
        // Finite cells stay finite (no Inf leak into the filled surface).
        for ( size_t i = 0; i < filled.size(); ++i )
            if ( i != static_cast<size_t>( 2 * W + 2 ) )
                REQUIRE( std::isfinite( filled[i] ) );
        (void)dir;
    }
}

// ===========================================================================
// A2 (#1739): DEM unit resolution must fail closed on unknown names and map
// the common non-metre aliases to their true metre scales.
// ===========================================================================

TEST_CASE( "R27-A2 demUnitScaleFromName maps aliases and reports unknown",
           "[r27][sar][units]" )
{
    using sicnu::sar::demUnitScaleFromName;
    QString applied;

    // Documented aliases (issue: declared cm/ft/in silently scaled as metres).
    CHECK( demUnitScaleFromName( QStringLiteral( "cm" ), &applied ) == approx( 0.01 ) );
    CHECK( applied == QStringLiteral( "centimeters" ) );
    CHECK( demUnitScaleFromName( QStringLiteral( "Centimeters" ), &applied ) == approx( 0.01 ) );
    CHECK( demUnitScaleFromName( QStringLiteral( "centimetres" ), &applied ) == approx( 0.01 ) );
    CHECK( demUnitScaleFromName( QStringLiteral( "mm" ), &applied ) == approx( 0.001 ) );
    CHECK( demUnitScaleFromName( QStringLiteral( "km" ), &applied ) == approx( 1000.0 ) );
    CHECK( demUnitScaleFromName( QStringLiteral( "ft" ), &applied ) == approx( 0.3048 ) );
    CHECK( demUnitScaleFromName( QStringLiteral( "feet" ), &applied ) == approx( 0.3048 ) );
    CHECK( demUnitScaleFromName( QStringLiteral( "foot" ), &applied ) == approx( 0.3048 ) );
    CHECK( demUnitScaleFromName( QStringLiteral( "in" ), &applied ) == approx( 0.0254 ) );
    CHECK( demUnitScaleFromName( QStringLiteral( "inch" ), &applied ) == approx( 0.0254 ) );
    CHECK( demUnitScaleFromName( QStringLiteral( "inches" ), &applied ) == approx( 0.0254 ) );
    CHECK( demUnitScaleFromName( QStringLiteral( "decimeters" ), &applied ) == approx( 0.1 ) );
    CHECK( demUnitScaleFromName( QStringLiteral( "dm" ), &applied ) == approx( 0.1 ) );
    CHECK( demUnitScaleFromName( QStringLiteral( "meters" ), &applied ) == approx( 1.0 ) );
    CHECK( demUnitScaleFromName( QStringLiteral( "metres" ), &applied ) == approx( 1.0 ) );
    CHECK( demUnitScaleFromName( QStringLiteral( "m" ), &applied ) == approx( 1.0 ) );
    // Whitespace-tolerant ("  Feet " behaves like "feet").
    CHECK( demUnitScaleFromName( QStringLiteral( "  Feet  " ), &applied ) == approx( 0.3048 ) );

    // Empty/undeclared keeps the historical metres default.
    CHECK( demUnitScaleFromName( QString(), &applied ) == approx( 1.0 ) );
    CHECK( applied == QStringLiteral( "meters" ) );
    CHECK( demUnitScaleFromName( QStringLiteral( "  " ), &applied ) == approx( 1.0 ) );

    // Unknown non-empty tokens must be REPORTED as unknown via the applied
    // marker (the fail-closed contract the operators enforce).
    const double scale = demUnitScaleFromName( QStringLiteral( "cubits" ), &applied );
    CHECK( applied.contains( QStringLiteral( "assumed" ) ) );
    CHECK( applied.contains( QStringLiteral( "cubits" ) ) );
    // The helper itself stays total (returns 1.0); refusal is enforced by
    // the operators so a legacy direct caller cannot crash.
    CHECK( scale == approx( 1.0 ) );
}

TEST_CASE( "R27-A2 SAR terrain correction refuses unknown declared DEM unit",
           "[r27][sar][units][e2e]" )
{
    ensureGdal();
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    const std::vector<float> sigma0( 16, 0.5f );
    const std::vector<float> dem( 16, 100.0f );
    const QString sarPath = tmp.path() + "/sigma0.tif";
    const QString demPath = tmp.path() + "/dem_cubits.tif";
    REQUIRE( writeTif( sarPath, GDT_Float32, 4, 4, 1, { sigma0 } ) );
    REQUIRE( writeTif( demPath, GDT_Float32, 4, 4, 1, { dem },
                       { { "SICNU_DEM_UNIT", "cubits" } } ) );

    auto &registry = sicnu::operators::RSOperatorRegistry::instance();
    auto op = registry.create( "rs:sar_terrain_correction" );
    REQUIRE( op != nullptr );

    sicnu::operators::RSOperatorContext ctx;
    Json::Value params( Json::objectValue );
    params["input"] = sarPath.toStdString();
    params["output"] = ( tmp.path() + "/out.tif" ).toStdString();
    params["dem"] = demPath.toStdString();
    REQUIRE_THROWS_AS( op->run( params, ctx ), sicnu::operators::RSOperatorError );
    // The refusal must name the offending unit (no silent assume-metres).
    bool namedUnit = false;
    try {
        op->run( params, ctx );
    } catch ( const sicnu::operators::RSOperatorError &e ) {
        namedUnit = std::string( e.what() ).find( "cubits" ) != std::string::npos;
    }
    REQUIRE( namedUnit );
    REQUIRE( !QFile::exists( tmp.path() + "/out.tif" ) );
}

// ===========================================================================
// A3 (#1740) + B4 (#1755): the SICNU_NUMERIC_OFFSET contract.
//   physical = (stored - SICNU_NUMERIC_OFFSET) / SICNU_NUMERIC_SCALE
// ===========================================================================

TEST_CASE( "R27-A3 Sentinel-2 L2A PB>=04.00 stamps and applies BOA_ADD_OFFSET",
           "[r27][s2][calibration][e2e]" )
{
    ensureGdal();
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    // --- Import: the stacked product must carry the offset stamp. ---------
    const QString safe = makeS2Safe( tmp, "-1000",
                                     "S2A_MSIL2A_20240615T000000_N9999_R000_T32TQQ_20240615T000000" );
    SatelliteProducts::ProductInfo info;
    QString err;
    REQUIRE( SatelliteProducts::discoverSentinel2( safe, &info, QStringLiteral( "10m" ), &err ) );
    const QString stacked = tmp.path() + "/s2_stack.tif";
    REQUIRE( SatelliteProducts::stackToGeoTiff(
        info, { QStringLiteral( "B02" ), QStringLiteral( "B03" ),
                QStringLiteral( "B04" ), QStringLiteral( "B08" ) },
        stacked, &err ) );

    GDALDatasetH ds = GDALOpen( stacked.toUtf8().constData(), GA_ReadOnly );
    REQUIRE( ds != nullptr );
    const char *scale = GDALGetMetadataItem( ds, "SICNU_NUMERIC_SCALE", nullptr );
    REQUIRE( scale != nullptr );
    CHECK( QString::fromUtf8( scale ).toDouble() == approx( 10000.0 ) );
    const char *offset = GDALGetMetadataItem( ds, "SICNU_NUMERIC_OFFSET", nullptr );
    REQUIRE( offset != nullptr );
    CHECK( QString::fromUtf8( offset ).toDouble() == approx( -1000.0 ) );
    GDALClose( ds );

    // --- Consumption: hand-computed indices on the verbatim DN stack. -----
    auto &registry = sicnu::operators::RSOperatorRegistry::instance();
    auto op = registry.create( "rs:spectral_index" );
    REQUIRE( op != nullptr );

    const auto runIndex = [&]( const std::string &index, const std::string &out,
                               const Json::Value &extra = Json::Value() ) {
        sicnu::operators::RSOperatorContext ctx;
        Json::Value params( Json::objectValue );
        params["input"] = stacked.toStdString();
        params["output"] = ( tmp.path() + "/" + QString::fromStdString( out ) ).toStdString();
        params["index"] = index;
        params["blue"] = 1;   // B02
        params["green"] = 2;  // B03
        params["red"] = 3;    // B04
        params["nir"] = 4;    // B08
        for ( const auto &key : extra.getMemberNames() )
            params[key] = extra[key];
        return op->run( params, ctx );
    };

    // NDVI = 0.444444…: with the offset honoured, the ratio index computed on
    // offset-bearing DN equals the ratio of physical reflectances. The
    // broken path computed the ratio on raw DN: 4000/7000 = 0.5714.
    (void)runIndex( "NDVI", "ndvi.tif" );
    std::vector<float> ndvi = readBand1( tmp.path() + "/ndvi.tif" );
    REQUIRE( !ndvi.empty() );
    for ( float v : ndvi )
        CHECK( v == approx( 4.0 / 9.0 ) );

    // EVI = 2.5*(0.65-0.25)/(0.65+6*0.25-7.5*0.28+1) = 1.0/1.05.
    (void)runIndex( "EVI", "evi.tif" );
    std::vector<float> evi = readBand1( tmp.path() + "/evi.tif" );
    REQUIRE( !evi.empty() );
    for ( float v : evi )
        CHECK( v == approx( 1.0 / 1.05 ) );

    // SAVI(L=0.5) = (0.40*1.5)/(0.65+0.25+0.5) = 0.6/1.4.
    (void)runIndex( "SAVI", "savi.tif" );
    std::vector<float> savi = readBand1( tmp.path() + "/savi.tif" );
    REQUIRE( !savi.empty() );
    for ( float v : savi )
        CHECK( v == approx( 0.6 / 1.4 ) );

    // NDWI = (0.35-0.65)/(0.35+0.65) = -0.3 — and the run result must
    // publish the calibration record + output quality ratios (R27-T01 D).
    const Json::Value ndwiResult = runIndex( "NDWI", "ndwi.tif" );
    std::vector<float> ndwi = readBand1( tmp.path() + "/ndwi.tif" );
    REQUIRE( !ndwi.empty() );
    for ( float v : ndwi )
        CHECK( v == approx( -0.3 ) );
    REQUIRE( ndwiResult.isMember( "calibration" ) );
    CHECK( ndwiResult["calibration"]["offset"].asDouble() == approx( -1000.0 ) );
    CHECK( ndwiResult["calibration"]["offsetSource"].asString() ==
           "SICNU_NUMERIC_OFFSET metadata" );
    CHECK( ndwiResult["calibration"]["contract"].asString() !=
           std::string() );
    REQUIRE( ndwiResult.isMember( "valid_pixel_ratio" ) );
    CHECK( ndwiResult["valid_pixel_ratio"].asDouble() == approx( 1.0 ) );
    CHECK( ndwiResult["nodata_pixel_ratio"].asDouble() == approx( 0.0 ) );

    // --- Old baseline (no BOA_ADD_OFFSET tag): no offset stamp; indices ---
    // --- keep the plain /scale semantics (protocol distinction).        ---
    // --- Disagreeing per-band offsets must NOT stamp (fail-closed, R2 P3-1):
    // --- a single dataset-wide constant cannot describe them.
    {
        QTemporaryDir tmpD;
        REQUIRE( tmpD.isValid() );
        const QString safeD = makeS2Safe( tmpD, "-1000",
                                          "S2A_MSIL2A_20240615T000001_N9999_R000_T32TQQ_20240615T000001",
                                          "disagree" );
        SatelliteProducts::ProductInfo infoD;
        QString errD;
        REQUIRE( SatelliteProducts::discoverSentinel2( safeD, &infoD,
                                                       QStringLiteral( "10m" ), &errD ) );
        const QString stackedD = tmpD.path() + "/s2_stack_disagree.tif";
        REQUIRE( SatelliteProducts::stackToGeoTiff(
            infoD, { QStringLiteral( "B04" ), QStringLiteral( "B08" ) }, stackedD, &errD ) );
        GDALDatasetH dsD = GDALOpen( stackedD.toUtf8().constData(), GA_ReadOnly );
        REQUIRE( dsD != nullptr );
        CHECK( GDALGetMetadataItem( dsD, "SICNU_NUMERIC_OFFSET", nullptr ) == nullptr );
        CHECK( GDALGetMetadataItem( GDALGetRasterBand( dsD, 1 ),
                                    "SICNU_NUMERIC_OFFSET", nullptr ) == nullptr );
        GDALClose( dsD );
    }

    // --- Real ESA forms (PB>=04.00): attributed per-band entries and the
    // --- nested BOA_LIST_TO_VALUES list must both stamp the offset (S0-1).
    for ( const char *form : { "bandid", "list" } ) {
        QTemporaryDir tmp2;
        REQUIRE( tmp2.isValid() );
        const QString safe2 = makeS2Safe( tmp2, "-1000",
                                          "S2C_MSIL2A_20240615T000000_N9999_R000_T32TQQ_20240615T000000",
                                          form );
        SatelliteProducts::ProductInfo info2;
        QString err2;
        REQUIRE( SatelliteProducts::discoverSentinel2( safe2, &info2,
                                                       QStringLiteral( "10m" ), &err2 ) );
        const QString stacked2 = tmp2.path() + "/s2_stack_form.tif";
        REQUIRE( SatelliteProducts::stackToGeoTiff(
            info2, { QStringLiteral( "B04" ), QStringLiteral( "B08" ) }, stacked2, &err2 ) );
        GDALDatasetH ds2 = GDALOpen( stacked2.toUtf8().constData(), GA_ReadOnly );
        REQUIRE( ds2 != nullptr );
        const char *scale2 = GDALGetMetadataItem( ds2, "SICNU_NUMERIC_SCALE", nullptr );
        REQUIRE( scale2 != nullptr ); // attributed <... unit="none"> must match
        CHECK( QString::fromUtf8( scale2 ).toDouble() == approx( 10000.0 ) );
        const char *offset2 = GDALGetMetadataItem( ds2, "SICNU_NUMERIC_OFFSET", nullptr );
        REQUIRE( offset2 != nullptr );
        CHECK( QString::fromUtf8( offset2 ).toDouble() == approx( -1000.0 ) );
        // Per-band stamps cover the affine bands of a mixed stack.
        const char *bandOffset = GDALGetMetadataItem(
            GDALGetRasterBand( ds2, 1 ), "SICNU_NUMERIC_OFFSET", nullptr );
        REQUIRE( bandOffset != nullptr );
        CHECK( QString::fromUtf8( bandOffset ).toDouble() == approx( -1000.0 ) );
        GDALClose( ds2 );
    }

    const QString safeOld = makeS2Safe(
        tmp, nullptr, "S2B_MSIL2A_20170725T000000_N9999_R000_T32TQQ_20170725T000000" );
    SatelliteProducts::ProductInfo infoOld;
    REQUIRE( SatelliteProducts::discoverSentinel2( safeOld, &infoOld,
                                                   QStringLiteral( "10m" ), &err ) );
    const QString stackedOld = tmp.path() + "/s2_stack_old.tif";
    // Old-baseline DNs sit at rho*10000 directly: NDVI = 4000/7000.
    REQUIRE( SatelliteProducts::stackToGeoTiff(
        infoOld, { QStringLiteral( "B02" ), QStringLiteral( "B03" ),
                   QStringLiteral( "B04" ), QStringLiteral( "B08" ) },
        stackedOld, &err ) );
    GDALDatasetH dsOld = GDALOpen( stackedOld.toUtf8().constData(), GA_ReadOnly );
    REQUIRE( dsOld != nullptr );
    CHECK( GDALGetMetadataItem( dsOld, "SICNU_NUMERIC_OFFSET", nullptr ) == nullptr );
    GDALClose( dsOld );
    sicnu::operators::RSOperatorContext ctxOld;
    {
        Json::Value params( Json::objectValue );
        params["input"] = stackedOld.toStdString();
        params["output"] = ( tmp.path() + "/ndvi_old.tif" ).toStdString();
        params["index"] = "NDVI";
        params["red"] = 3;
        params["nir"] = 4;
        (void)op->run( params, ctxOld );
    }
    std::vector<float> ndviOld = readBand1( tmp.path() + "/ndvi_old.tif" );
    REQUIRE( !ndviOld.empty() );
    for ( float v : ndviOld )
        CHECK( v == approx( 4000.0 / 7000.0 ) );
}

TEST_CASE( "R27-A3 Landsat C2 L2 offset stamp and EVI oracle",
           "[r27][landsat][calibration][e2e]" )
{
    ensureGdal();
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    const QString mtl = makeLandsatScene( tmp );
    SatelliteProducts::ProductInfo info;
    QString err;
    REQUIRE( SatelliteProducts::discoverLandsat( mtl, &info ) );
    const QString stacked = tmp.path() + "/lc_stack.tif";
    REQUIRE( SatelliteProducts::stackToGeoTiff(
        info, { QStringLiteral( "B2" ), QStringLiteral( "B3" ),
                QStringLiteral( "B4" ), QStringLiteral( "B5" ) },
        stacked, &err ) );

    GDALDatasetH ds = GDALOpen( stacked.toUtf8().constData(), GA_ReadOnly );
    REQUIRE( ds != nullptr );
    // rho = (DN - offset)/scale with offset = -ADD/MULT = 0.2/2.5e-5 = 8000,
    // scale = 1/MULT = 40000: (DN - 8000)/40000 == DN*2.5e-5 - 0.2.
    const char *scale = GDALGetMetadataItem( ds, "SICNU_NUMERIC_SCALE", nullptr );
    REQUIRE( scale != nullptr );
    CHECK( QString::fromUtf8( scale ).toDouble() == approx( 40000.0 ) );
    const char *offset = GDALGetMetadataItem( ds, "SICNU_NUMERIC_OFFSET", nullptr );
    REQUIRE( offset != nullptr );
    CHECK( QString::fromUtf8( offset ).toDouble() == approx( 8000.0 ) );
    GDALClose( ds );

    auto &registry = sicnu::operators::RSOperatorRegistry::instance();
    auto op = registry.create( "rs:spectral_index" );
    REQUIRE( op != nullptr );
    sicnu::operators::RSOperatorContext ctx;
    Json::Value params( Json::objectValue );
    params["input"] = stacked.toStdString();
    params["output"] = ( tmp.path() + "/evi.tif" ).toStdString();
    params["index"] = "EVI";
    params["blue"] = 1;  // B2
    params["red"] = 3;   // B4
    params["nir"] = 4;   // B5
    (void)op->run( params, ctx );
    std::vector<float> evi = readBand1( tmp.path() + "/evi.tif" );
    REQUIRE( !evi.empty() );
    // EVI = 2.5*(0.22-0.06)/(0.22+6*0.06-7.5*0.02+1) = 0.4/1.43.
    for ( float v : evi )
        CHECK( v == approx( 0.4 / 1.43 ) );
}

TEST_CASE( "R27-B4 ratio index on offset DN equals ratio on physical reflectance",
           "[r27][spectral][offset][e2e]" )
{
    ensureGdal();
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    // Hand reference scene: band 1 = NIR reflectance 0.65, band 2 = red 0.25
    // -> true NDVI = 0.4/0.9 = 0.4444. Same scene in the offset DN encoding
    // DN = rho*10000 - 1000 (S2 PB>=04): nir 5500, red 1500. The offset-blind
    // path computes 4000/7000 = 0.5714 — the exact bias #1755 describes.
    const std::vector<float> physNir( 16, 0.65f );
    const std::vector<float> physRed( 16, 0.25f );
    const std::vector<float> dnNir( 16, 5500.0f );
    const std::vector<float> dnRed( 16, 1500.0f );

    const QString physPath = tmp.path() + "/physical.tif";
    const QString dnPath = tmp.path() + "/dn_offset.tif";
    REQUIRE( writeTif( physPath, GDT_Float32, 4, 4, 2, { physNir, physRed } ) );
    REQUIRE( writeTif( dnPath, GDT_Float32, 4, 4, 2, { dnNir, dnRed },
                       { { "SICNU_NUMERIC_SCALE", "10000" },
                         { "SICNU_NUMERIC_OFFSET", "-1000" } } ) );

    auto &registry = sicnu::operators::RSOperatorRegistry::instance();
    auto op = registry.create( "rs:spectral_index" );
    REQUIRE( op != nullptr );

    // Explicit offset param (stored-value space) wins over the stamp.
    const auto ndviOf = [&]( const QString &in, const QString &out,
                             const Json::Value &extra = Json::Value() ) {
        sicnu::operators::RSOperatorContext ctx;
        Json::Value params( Json::objectValue );
        params["input"] = in.toStdString();
        params["output"] = out.toStdString();
        params["index"] = "NDVI";
        params["red"] = 2;
        params["nir"] = 1;
        for ( const auto &key : extra.getMemberNames() )
            params[key] = extra[key];
        (void)op->run( params, ctx );
        return readBand1( out );
    };
    const std::vector<float> fromPhysical = ndviOf( physPath, tmp.path() + "/ndvi_phys.tif" );
    const std::vector<float> fromDn = ndviOf( dnPath, tmp.path() + "/ndvi_dn.tif" );
    // Offset param on the PHYSICAL raster (which carries no stamp) must NOT
    // perturb it (offset 0 param = no-op); offset param on a DN raster
    // without stamps reproduces the un-offset NDVI only when correct.
    {
        Json::Value extra( Json::objectValue );
        extra["offset"] = 0.0;
        const std::vector<float> neutral =
            ndviOf( physPath, tmp.path() + "/ndvi_phys_o0.tif", extra );
        for ( size_t i = 0; i < neutral.size(); ++i )
            CHECK( neutral[i] == approx( fromPhysical[i] ) );
    }
    REQUIRE( fromPhysical.size() == fromDn.size() );
    REQUIRE( !fromPhysical.empty() );
    for ( size_t i = 0; i < fromPhysical.size(); ++i )
        // Offset must be honoured (the broken path computed the ratio on
        // offset DN: 4000/7000 = 0.5714 instead of the true 0.4444).
        CHECK( fromDn[i] == approx( fromPhysical[i] ) );
    CHECK( fromPhysical[0] == approx( ( 0.65f - 0.25f ) / ( 0.65f + 0.25f ) ) );
}

// ===========================================================================
// B1 (#1752): changeMask must treat every non-finite diff as NoData.
// ===========================================================================

TEST_CASE( "R27-B1 changeMask maps non-finite diff to NoData", "[r27][change][inf]" )
{
    const std::vector<float> diff = { kInf, -kInf, kNaN, 0.5f, 2.0f };
    std::vector<uint8_t> mask( diff.size(), 0 );
    REQUIRE( ChangeDetection::changeMask( diff.data(), mask.data(), diff.size(), 1.0f ) );
    const std::array<uint8_t, 5> expected = { 255, 255, 255, 0, 1 };
    for ( size_t i = 0; i < expected.size(); ++i )
        CHECK( mask[i] == expected[i] );

    // All-NoData diff stays all-NoData; empty input refuses.
    const std::vector<float> allNan = { kNaN, kNaN };
    std::vector<uint8_t> mask2( allNan.size(), 7 );
    REQUIRE( ChangeDetection::changeMask( allNan.data(), mask2.data(), allNan.size(), 0.5f ) );
    for ( uint8_t v : mask2 )
        CHECK( v == 255 );
    CHECK_FALSE( ChangeDetection::changeMask( nullptr, mask2.data(), 0, 0.5f ) );
}

// ===========================================================================
// B2 (#1753): piecewise linear stretch must treat Inf as NoData.
// ===========================================================================

TEST_CASE( "R27-B2 piecewise stretch treats Inf as NoData", "[r27][enhance][inf]" )
{
    using ImageEnhancementStreaming::piecewiseLinearStretchTile;
    const std::vector<std::pair<float, float>> cps = { { 0.0f, 0.0f }, { 100.0f, 100.0f } };
    const std::vector<float> in = { kInf, -kInf, kNaN, 50.0f, kNodata };
    std::vector<float> out( in.size(), -1 );
    piecewiseLinearStretchTile( in.data(), out.data(), in.size(), cps, kNodata );
    // Inf must NOT be mapped into a stretch endpoint (broken path clamped
    // +Inf to the last control point's 100.0 as "valid data").
    CHECK( out[0] == kNodata );
    CHECK( out[1] == kNodata );
    CHECK( out[2] == kNodata );
    CHECK( out[3] == approx( 50.0f ) );
    CHECK( out[4] == kNodata );

    // Degenerate (<2 control points) passthrough path is Inf-safe too.
    const std::vector<std::pair<float, float>> one = { { 1.0f, 2.0f } };
    std::vector<float> out2( in.size(), -1 );
    piecewiseLinearStretchTile( in.data(), out2.data(), in.size(), one, kNodata );
    CHECK( out2[0] == kNodata );
    CHECK( out2[1] == kNodata );
    CHECK( out2[3] == approx( 50.0f ) );
}

// ===========================================================================
// B3 (#1754): fusion kernels must treat Inf as an invalid sample.
// ===========================================================================

TEST_CASE( "R27-B3 fusion kernels treat Inf as invalid", "[r27][fusion][inf]" )
{
    constexpr int W = 4, H = 4;
    constexpr float nd = kNodata;
    std::vector<float> r( W * H, 0.6f ), g( W * H, 0.5f ), b( W * H, 0.4f );
    std::vector<float> panOk( W * H, 0.9f );

    SECTION( "Brovey: Inf pan -> NoData output, finite elsewhere" )
    {
        std::vector<float> pan = panOk;
        pan[5] = kInf;
        QVector<QVector<float>> fused = ImageFusion::brovey(
            { r.data(), g.data(), b.data() }, 3, pan.data(), W, H, nd );
        REQUIRE( fused.size() == 3 );
        REQUIRE( ( std::isnan( fused[0][5] ) || fused[0][5] == nd ) );
        for ( int i = 0; i < W * H; ++i )
            if ( i != 5 )
                CHECK( std::isfinite( fused[0][static_cast<size_t>( i )] ) );
    }

    SECTION( "Brovey: Inf multispectral sample -> NoData output" )
    {
        r[10] = -kInf;
        QVector<QVector<float>> fused = ImageFusion::brovey(
            { r.data(), g.data(), b.data() }, 3, panOk.data(), W, H, nd );
        REQUIRE( fused.size() == 3 );
        for ( const auto &band : fused )
            REQUIRE( ( std::isnan( band[10] ) || band[10] == nd ) );
    }

    SECTION( "linearWeighted: Inf ms -> NoData output" )
    {
        g[3] = kInf;
        QVector<QVector<float>> fused = ImageFusion::linearWeighted(
            { r.data(), g.data(), b.data() }, 3, panOk.data(), W, H, nd );
        REQUIRE( fused.size() == 3 );
        REQUIRE( ( std::isnan( fused[1][3] ) || fused[1][3] == nd ) );
        CHECK( std::isfinite( fused[0][3] ) );
    }
}

// ===========================================================================
// B5 (#1756): streaming DOS must not take DN0 fill as the dark object when
// the band declares no NoData (USGS/ESA integer fill convention).
// ===========================================================================

TEST_CASE( "R27-B5 DOS excludes undeclared DN0 fill from the dark object",
           "[r27][dos][fill]" )
{
    ensureGdal();
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    constexpr int W = 16, H = 16;
    // Half the scene is DN=0 fill; the rest is a real dark scene 200..249.
    std::vector<float> dn( static_cast<size_t>( W ) * H );
    for ( int r = 0; r < H; ++r )
        for ( int c = 0; c < W; ++c ) {
            const size_t i = static_cast<size_t>( r * W + c );
            dn[i] = ( c < W / 2 ) ? 0.0f : 200.0f + static_cast<float>( ( r * 7 + c ) % 50 );
        }

    // Sentinel-2 conversion is rho = (DN + offset) / scale with the generic
    // scale/offset fields (not reflMult/reflAdd) — see toToaReflectance.
    // With scale=10000, offset=0: rho = DN * 1e-4.
    RadiometricCalibration::BandCoefficients coeffs;
    coeffs.scale = 10000.0;
    coeffs.offset = 0.0;
    coeffs.hasReflectance = true;

    const auto runDos = [&]( const QString &inPath, const QString &outPath,
                             bool declareZero ) {
        // UInt16 like the USGS band TIFFs the #1756/#1759 fill convention
        // describes (the undeclared-DN0 guard is integer-typed by design).
        const QString src = tmp.path() + "/" + inPath;
        REQUIRE( writeTif( src, GDT_UInt16, W, H, 1, { dn }, {}, declareZero, 0.0 ) );
        QString err;
        const bool ok = AtmosphericCorrection::processFileDos(
            src, tmp.path() + "/" + outPath, 1,
            AtmosphericCorrection::Dos1, coeffs,
            RadiometricCalibration::SensorType::Sentinel2, 90.0, 1.0f, &err );
        if ( !ok )
            FAIL( err.toStdString() );
        return readBand1( tmp.path() + "/" + outPath );
    };

    // Declared-NoData regression: DN0 masked today, dark level 0.02.
    const std::vector<float> declared =
        runDos( "dos_declared_src.tif", "dos_declared_out.tif", true );
    REQUIRE( declared.size() == dn.size() );
    // Pixel (0,10): dn=210 -> rho 0.021; dark=0.02(+bin) -> ~0.011.
    CHECK( declared[static_cast<size_t>( 10 )]
               == Catch::Approx( 0.011f ).margin( 1e-5 ) );
    // Fill pixels are NoData.
    CHECK( std::isnan( declared[0] ) );

    // Undeclared NoData (the #1756 case): the fill must be excluded from the
    // histogram (dark level 0.02 from the real scene, NOT 0.0) and fill
    // pixels must come out NoData, not fake valid reflectance.
    const std::vector<float> undeclared =
        runDos( "dos_undeclared_src.tif", "dos_undeclared_out.tif", false );
    REQUIRE( undeclared.size() == dn.size() );
    CHECK( std::isnan( undeclared[0] ) );                       // fill -> NoData
    CHECK( undeclared[static_cast<size_t>( 10 )]
               == Catch::Approx( 0.011f ).margin( 1e-5 ) ); // dark not the fill
    // darkLevel is the bin CENTRE of the lowest occupied histogram bin:
    // 1024 bins over [0.02, 0.0249] put it at 0.02 + ~2.4e-6, hence the
    // margin on the Chavez (rho - dark + 0.01) expectation.
    for ( int c = W / 2; c < W; ++c ) {
        const float rho = dn[static_cast<size_t>( c )] * 1.0e-4f;
        CHECK( undeclared[static_cast<size_t>( c )]
               == Catch::Approx( rho - 0.02f + 0.01f ).margin( 1e-5 ) );
    }
}

// ===========================================================================
// B6 (#1757 + #1758): gdal:reproject NoData contract + categorical guard.
// ===========================================================================

TEST_CASE( "R27-B6 reproject keeps classes and NoData honest",
           "[r27][reproject][categorical][e2e]" )
{
    ensureGdal();
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    auto &registry = sicnu::operators::RSOperatorRegistry::instance();
    REQUIRE( registry.create( "gdal:reproject" ) != nullptr );

    // 32x32 Byte classification checkerboard of classes {1, 5}.
    constexpr int W = 32, H = 32;
    std::vector<float> cls( static_cast<size_t>( W ) * H );
    for ( int r = 0; r < H; ++r )
        for ( int c = 0; c < W; ++c )
            cls[static_cast<size_t>( r * W + c )] =
                ( ( ( r / 4 ) + ( c / 4 ) ) % 2 == 0 ) ? 1.0f : 5.0f;

    // 64x64 EPSG:4326 canvas with the SAME pixel size but double the extent:
    // warping the 32x32 class map onto it leaves guaranteed outside-footprint
    // voids, which is what the NoData contract (#1757) is about.
    const auto makeReference = [&]( const QString &path ) {
        GDALDriverH driver = GDALGetDriverByName( "GTiff" );
        REQUIRE( driver != nullptr );
        constexpr int RW = 64, RH = 64;
        GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), RW, RH,
                                      1, GDT_Byte, nullptr );
        REQUIRE( ds != nullptr );
        const std::array<double, 6> rgt = { 115.0, 0.0001, 0.0, 41.0, 0.0, -0.0001 };
        GDALSetGeoTransform( ds, const_cast<double *>( rgt.data() ) );
        GDALSetProjection( ds, "GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563]],PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]]" );
        std::vector<uint8_t> zero( static_cast<size_t>( RW ) * RH, 0 );
        REQUIRE( GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Write, 0, 0, RW, RH,
                               zero.data(), RW, RH, GDT_Byte, 0, 0 ) == CE_None );
        GDALClose( ds );
    };

    const auto collectClasses = []( const QString &path ) {
        std::set<float> classes;
        const std::vector<float> v = readBand1( path );
        for ( float x : v )
            classes.insert( x );
        return std::make_pair( classes, v );
    };

    SECTION( "categorical Byte raster: no invented classes, footprint NoData" )
    {
        const QString src = tmp.path() + "/classes.tif";
        const QString ref = tmp.path() + "/reference.tif";
        REQUIRE( writeTif( src, GDT_Byte, W, H, 1, { cls } ) );
        makeReference( ref );
        auto op = registry.create( "gdal:reproject" );
        sicnu::operators::RSOperatorContext ctx;
        Json::Value params( Json::objectValue );
        params["input"] = src.toStdString();
        params["output"] = ( tmp.path() + "/classes_3857.tif" ).toStdString();
        params["reference"] = ref.toStdString();
        params["nodata"] = 255;
        const Json::Value res = op->run( params, ctx );
        REQUIRE( res.isMember( "output" ) );

        const auto [classes, pixels] = collectClasses( tmp.path() + "/classes_3857.tif" );
        // Bilinear on a {1,5} map invents 2/3/4…; nearest keeps {1,5,255}.
        for ( float c : classes ) {
            INFO( "invented class value: " << c );
            CHECK( ( c == 1.0f || c == 5.0f || c == 255.0f ) );
        }
        // The rotated footprint in 3857 leaves voids that must be 255
        // (declared), never silently zero "data".
        CHECK( classes.count( 255.0f ) == 1 );
        const size_t n255 = static_cast<size_t>(
            std::count( pixels.begin(), pixels.end(), 255.0f ) );
        CHECK( n255 > 0 );
    }

    SECTION( "undeclared Float32 source: voids come back NaN, not 0" )
    {
        std::vector<float> cont( static_cast<size_t>( W ) * H );
        for ( int r = 0; r < H; ++r )
            for ( int c = 0; c < W; ++c )
                cont[static_cast<size_t>( r * W + c )] =
                    100.0f + static_cast<float>( r ) + 0.5f * c;
        const QString src = tmp.path() + "/cont.tif";
        const QString ref = tmp.path() + "/reference_f.tif";
        REQUIRE( writeTif( src, GDT_Float32, W, H, 1, { cont } ) );
        makeReference( ref );
        auto op = registry.create( "gdal:reproject" );
        sicnu::operators::RSOperatorContext ctx;
        Json::Value params( Json::objectValue );
        params["input"] = src.toStdString();
        params["output"] = ( tmp.path() + "/cont_3857.tif" ).toStdString();
        params["reference"] = ref.toStdString();
        (void)op->run( params, ctx );
        const std::vector<float> v = readBand1( tmp.path() + "/cont_3857.tif" );
        REQUIRE( !v.empty() );
        const size_t nanCount =
            static_cast<size_t>( std::count_if( v.begin(), v.end(),
                                                []( float x ) { return std::isnan( x ); } ) );
        const size_t zeroCount =
            static_cast<size_t>( std::count( v.begin(), v.end(), 0.0f ) );
        // Voids must read back as NaN (declared dstnodata nan) — the broken
        // path zero-filled them and they came back as valid 0.0 data.
        CHECK( nanCount > 0 );
        CHECK( zeroCount == 0 );
    }

    SECTION( "explicit bilinear on a STRUCTURAL (palette) categorical raster is refused" )
    {
        // Byte map with a color table: structural categorical evidence.
        const QString src = tmp.path() + "/classes_palette.tif";
        REQUIRE( writeTif( src, GDT_Byte, W, H, 1, { cls } ) );
        {
            GDALDatasetH ds = GDALOpen( src.toUtf8().constData(), GA_Update );
            REQUIRE( ds != nullptr );
            GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
            GDALColorTableH ct = GDALCreateColorTable( GPI_RGB );
            for ( int ci = 0; ci <= 5; ++ci ) {
                GDALColorEntry e = { static_cast<short>( 40 * ci ), 0,
                                     static_cast<short>( 255 - 40 * ci ), 255 };
                GDALSetColorEntry( ct, ci, &e );
            }
            GDALSetRasterColorTable( band, ct );
            GDALDestroyColorTable( ct );
            GDALClose( ds );
        }
        auto op = registry.create( "gdal:reproject" );
        sicnu::operators::RSOperatorContext ctx;
        Json::Value params( Json::objectValue );
        params["input"] = src.toStdString();
        params["output"] = ( tmp.path() + "/refused.tif" ).toStdString();
        params["dstCrs"] = "EPSG:3857";
        params["resampling"] = "bilinear";
        REQUIRE_THROWS_AS( op->run( params, ctx ), sicnu::operators::RSOperatorError );
        REQUIRE( !QFile::exists( tmp.path() + "/refused.tif" ) );
    }

    SECTION( "explicit bilinear on a PROBE-only class map is overridden to nearest" )
    {
        // No palette/RAT — only the few-distinct-values data probe fires. A
        // coarse continuous raster could look the same, so the operator must
        // NOT refuse; it overrides to nearest and the class set is preserved.
        const QString src = tmp.path() + "/classes_probe.tif";
        const QString ref = tmp.path() + "/reference_probe.tif";
        REQUIRE( writeTif( src, GDT_Byte, W, H, 1, { cls } ) );
        makeReference( ref );
        auto op = registry.create( "gdal:reproject" );
        sicnu::operators::RSOperatorContext ctx;
        Json::Value params( Json::objectValue );
        params["input"] = src.toStdString();
        params["output"] = ( tmp.path() + "/probe_overridden.tif" ).toStdString();
        params["reference"] = ref.toStdString();
        params["resampling"] = "bilinear";
        params["nodata"] = 255;
        const Json::Value res = op->run( params, ctx );
        CHECK( res["resampling"].asString() == "nearest" );
        CHECK( res["categorical"].asBool() == true );
        const auto [classes, pixels] = collectClasses( tmp.path() + "/probe_overridden.tif" );
        (void)pixels;
        for ( float c : classes )
            CHECK( ( c == 1.0f || c == 5.0f || c == 255.0f ) );
    }

    SECTION( "uniform declared source NoData pins src+dst (import-path parity)" )
    {
        std::vector<float> cont( static_cast<size_t>( W ) * H, 17.5f );
        const QString src = tmp.path() + "/declared.tif";
        REQUIRE( writeTif( src, GDT_Float32, W, H, 1, { cont },
                           {}, /*declareNodata=*/true, -9999.0 ) );
        const QString ref = tmp.path() + "/reference_nd.tif";
        makeReference( ref );
        auto op = registry.create( "gdal:reproject" );
        sicnu::operators::RSOperatorContext ctx;
        Json::Value params( Json::objectValue );
        params["input"] = src.toStdString();
        params["output"] = ( tmp.path() + "/declared_out.tif" ).toStdString();
        params["reference"] = ref.toStdString();
        (void)op->run( params, ctx );
        // Outside the footprint the declared sentinel reads back exactly.
        GDALDatasetH ds = GDALOpen( ( tmp.path() + "/declared_out.tif" ).toUtf8().constData(),
                                    GA_ReadOnly );
        REQUIRE( ds != nullptr );
        int hasNd = 0;
        const double nd = GDALGetRasterNoDataValue( GDALGetRasterBand( ds, 1 ), &hasNd );
        GDALClose( ds );
        CHECK( hasNd == 1 );
        CHECK( nd == -9999.0 );
        const std::vector<float> v = readBand1( tmp.path() + "/declared_out.tif" );
        const size_t nNd = static_cast<size_t>(
            std::count( v.begin(), v.end(), -9999.0f ) );
        CHECK( nNd > 0 ); // voids carry the declared sentinel, not zeros
    }

    SECTION( "explicit near on a categorical raster is honoured" )
    {
        const QString src = tmp.path() + "/classes3.tif";
        const QString ref = tmp.path() + "/reference_n.tif";
        REQUIRE( writeTif( src, GDT_Byte, W, H, 1, { cls } ) );
        makeReference( ref );
        auto op = registry.create( "gdal:reproject" );
        sicnu::operators::RSOperatorContext ctx;
        Json::Value params( Json::objectValue );
        params["input"] = src.toStdString();
        params["output"] = ( tmp.path() + "/near_ok.tif" ).toStdString();
        params["reference"] = ref.toStdString();
        params["resampling"] = "nearest";
        params["nodata"] = 255;
        (void)op->run( params, ctx );
        const auto [classes, pixels] = collectClasses( tmp.path() + "/near_ok.tif" );
        (void)pixels;
        for ( float c : classes )
            CHECK( ( c == 1.0f || c == 5.0f || c == 255.0f ) );
    }
}

// ===========================================================================
// B7 (#1759): thermal calibration + import must not publish DN0 fill as
// plausible values.
// ===========================================================================

TEST_CASE( "R27-B7 brightness temperature hand reference (regression pin)",
           "[r27][thermal]" )
{
    using RadiometricCalibration::BandCoefficients;
    using RadiometricCalibration::toBrightnessTemperature;

    // Landsat-8 TIRS-like constants.
    BandCoefficients c;
    c.k1 = 607.76;
    c.k2 = 1260.56;
    c.radianceGain = 3.342e-4;
    c.radianceBias = 0.1;
    c.hasRadiance = true;

    const std::vector<float> dn = { 22000.0f, 15000.0f };
    std::vector<float> bt( dn.size(), -1 );
    REQUIRE( toBrightnessTemperature( dn.data(), bt.data(), dn.size(), c ) );
    // L(22000) = 7.4624 -> BT = K2/ln(K1/L + 1) = 1260.56/ln(82.403) ≈ 285.6 K
    CHECK( bt[0] == Catch::Approx( 285.6 ).margin( 0.1 ) );
    // L(15000) = 5.113 -> 607.76/5.113 = 118.85 -> +1 -> ln(119.85) = 4.786
    // BT = 1260.56/4.786 = 263.4 K
    CHECK( bt[1] == Catch::Approx( 263.4 ).margin( 0.1 ) );
}

TEST_CASE( "R27-B7 brightness temperature masks DN0 fill when the flag is set",
           "[r27][thermal][fill]" )
{
    using RadiometricCalibration::BandCoefficients;
    using RadiometricCalibration::toBrightnessTemperature;

    BandCoefficients c;
    c.k1 = 607.76;
    c.k2 = 1260.56;
    c.radianceGain = 3.342e-4;
    c.radianceBias = 0.1; // positive AL: DN0 used to yield a finite garbage K
    c.hasRadiance = true;

    SECTION( "dn0IsFill: DN0 -> NaN, never a plausible Kelvin value" )
    {
        c.dn0IsFill = true;
        const std::vector<float> dn = { 0.0f, 22000.0f };
        std::vector<float> bt( dn.size(), -1 );
        REQUIRE( toBrightnessTemperature( dn.data(), bt.data(), dn.size(), c ) );
        CHECK( std::isnan( bt[0] ) );
        CHECK( bt[1] == Catch::Approx( 285.6 ).margin( 0.1 ) );
    }

    SECTION( "flag off keeps the legacy finite behaviour (declared scenes)" )
    {
        const std::vector<float> dn = { 0.0f };
        std::vector<float> bt( dn.size(), -1 );
        REQUIRE( toBrightnessTemperature( dn.data(), bt.data(), dn.size(), c ) );
        CHECK( std::isfinite( bt[0] ) ); // L = 0.1 > 0 computes a value
    }
}

TEST_CASE( "R27-B7 import infers USGS DN0 fill on undeclared integer bands",
           "[r27][import][fill][e2e]" )
{
    ensureGdal();
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    // Landsat C2 L2 scene whose B2 tile carries DN0 fill pixels and no
    // NoData tag (the USGS shipping condition from #1759). The import seam
    // must declare 0 as the band NoData so no downstream consumer can
    // publish it as valid data.
    const QString mtl = makeLandsatScene( tmp, /*withDn0Fill=*/true );
    SatelliteProducts::ProductInfo info;
    QString err;
    REQUIRE( SatelliteProducts::discoverLandsat( mtl, &info ) );
    const QString stacked = tmp.path() + "/lc_fill_stack.tif";
    REQUIRE( SatelliteProducts::stackToGeoTiff(
        info, { QStringLiteral( "B2" ), QStringLiteral( "B3" ),
                QStringLiteral( "B4" ), QStringLiteral( "B5" ) },
        stacked, &err ) );

    GDALDatasetH ds = GDALOpen( stacked.toUtf8().constData(), GA_ReadOnly );
    REQUIRE( ds != nullptr );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );  // B2 with DN0 fill
    int hasNodata = 0;
    const double nd = GDALGetRasterNoDataValue( band, &hasNodata );
    GDALClose( ds );
    CHECK( hasNodata == 1 );
    CHECK( nd == 0.0 );
    // NOTE: the "band without DN0 keeps no stamp" control cannot be asserted
    // through GTiff — the format serializes ONE NoData tag per dataset, so a
    // band-1 stamp reads back on every band (#854). Band-level isolation is
    // verified by the DN0-scan guard itself (sawZeroFill is per selected
    // band and only set where the data actually contains a 0).
}

// ===========================================================================
// C1 (#1801 OS-6 pin): GLCM windows containing Inf must NaN-poison metrics.
// ===========================================================================

TEST_CASE( "R27-B7 DN0 inference exempts QA bands (0 is a valid class there)",
           "[r27][import][fill][qa]" )
{
    ensureGdal();
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    // Landsat scene whose QA_PIXEL tile is all-zero ("clear" everywhere) and
    // B2 is fill-free: stacking ONLY the QA band must not declare 0 NoData.
    const QString mtl = makeLandsatScene( tmp, /*withDn0Fill=*/false );
    const QString dir = QFileInfo( mtl ).absolutePath();
    {
        GDALDriverH driver = GDALGetDriverByName( "GTiff" );
        REQUIRE( driver != nullptr );
        GDALDatasetH ds = GDALCreate( driver,
                                      ( dir + "/LC08_L2SP_123032_20240615_02_T1_QA_PIXEL.TIF" )
                                          .toUtf8()
                                          .constData(),
                                      4, 4, 1, GDT_UInt16, nullptr );
        REQUIRE( ds != nullptr );
        std::vector<uint16_t> zeros( 16, 0 );
        REQUIRE( GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Write, 0, 0, 4, 4,
                               zeros.data(), 4, 4, GDT_UInt16, 0, 0 ) == CE_None );
        GDALClose( ds );
    }
    SatelliteProducts::ProductInfo info;
    QString err;
    REQUIRE( SatelliteProducts::discoverLandsat( mtl, &info ) );
    const QString stacked = tmp.path() + "/qa_only_stack.tif";
    REQUIRE( SatelliteProducts::stackToGeoTiff( info, { QStringLiteral( "QA_PIXEL" ) },
                                                stacked, &err ) );
    GDALDatasetH ds = GDALOpen( stacked.toUtf8().constData(), GA_ReadOnly );
    REQUIRE( ds != nullptr );
    int hasNodata = 0;
    (void)GDALGetRasterNoDataValue( GDALGetRasterBand( ds, 1 ), &hasNodata );
    GDALClose( ds );
    CHECK( hasNodata == 0 ); // DN0 is "clear" on QA bands, never fill
}

TEST_CASE( "R27-C1 GLCM Inf window poisons metrics (regression pin)",
           "[r27][glcm][inf]" )
{
    rs::processing::GlcmConfig config;
    config.quantLevels = 8;
    config.minVal = 0.0f;
    config.maxVal = 1.0f;
    const std::vector<float> win = { 0.1f, 0.2f, 0.3f, 0.4f,
                                     0.5f, kInf, 0.7f, 0.8f,
                                     0.9f, 0.15f, 0.25f, 0.35f,
                                     0.45f, 0.55f, 0.65f, 0.75f };
    const auto m = rs::processing::GlcmTextureCalculator::computeForWindow(
        win, 4, 4, config );
    CHECK( std::isnan( m.contrast ) );
    CHECK( std::isnan( m.energy ) );
}

// ===========================================================================
// E1: determinism / consistency — same input, same index, byte-identical.
// ===========================================================================

TEST_CASE( "R27-E1 spectral index outputs are deterministic across runs",
           "[r27][consistency][e2e]" )
{
    ensureGdal();
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    // 300x300 raster (spans 256-tiles) with an offset stamp.
    constexpr int W = 300, H = 300;
    std::vector<float> dnRed( static_cast<size_t>( W ) * H );
    std::vector<float> dnNir( static_cast<size_t>( W ) * H );
    for ( int r = 0; r < H; ++r )
        for ( int c = 0; c < W; ++c ) {
            const float rhoRed = 0.2f + 0.1f * std::sin( 0.05f * r );
            const float rhoNir = 0.6f + 0.05f * std::cos( 0.03f * c );
            dnRed[static_cast<size_t>( r * W + c )] = rhoRed * 10000.0f - 1000.0f;
            dnNir[static_cast<size_t>( r * W + c )] = rhoNir * 10000.0f - 1000.0f;
        }
    const QString src = tmp.path() + "/two_band.tif";
    REQUIRE( writeTif( src, GDT_Float32, W, H, 2, { dnRed, dnNir },
                       { { "SICNU_NUMERIC_SCALE", "10000" },
                         { "SICNU_NUMERIC_OFFSET", "-1000" } } ) );

    auto &registry = sicnu::operators::RSOperatorRegistry::instance();
    auto op = registry.create( "rs:spectral_index" );
    REQUIRE( op != nullptr );

    const auto runOnce = [&]( const QString &out ) {
        sicnu::operators::RSOperatorContext ctx;
        Json::Value params( Json::objectValue );
        params["input"] = src.toStdString();
        params["output"] = out.toStdString();
        params["index"] = "NDVI";
        params["red"] = 1;
        params["nir"] = 2;
        (void)op->run( params, ctx );
        return readBand1( out );
    };

    const std::vector<float> runA = runOnce( tmp.path() + "/ndvi_a.tif" );
    const std::vector<float> runB = runOnce( tmp.path() + "/ndvi_b.tif" );
    REQUIRE( runA.size() == runB.size() );
    REQUIRE( runA.size() == static_cast<size_t>( W ) * H );
    for ( size_t i = 0; i < runA.size(); ++i )
        CHECK( runA[i] == runB[i] ); // byte-identical across runs

    // Spot-check the hand formula: (0,0): rhoRed = 0.2, rhoNir = 0.65
    // -> (0.65-0.2)/(0.85) = 0.529411…
    CHECK( runA[0] == approx( 0.45f / 0.85f ) );
    // (5,7): rhoRed = 0.2+0.1*sin(0.25), rhoNir = 0.6+0.05*cos(0.21)
    const double rr = 0.2 + 0.1 * std::sin( 0.25 );
    const double rn = 0.6 + 0.05 * std::cos( 0.21 );
    CHECK( runA[static_cast<size_t>( 5 * W + 7 )] == approx( ( rn - rr ) / ( rn + rr ) ) );
}
