// test_r25_a_spectral_guards.cpp — R25-A DEF-4 + DEF-5.
//
// DEF-4: rs:spectral_derivative must honour WAVELENGTH_UNITS. A cube
// stamped WAVELENGTH=0.46..0.95 with WAVELENGTH_UNITS=Micrometers was read
// as 0.46..0.95 nm: first derivatives inflated 1000x (second order 1e6x)
// and outputWavelengthsNm mislabelled, silently. Oracle: a linear
// reflectance ramp r(λ) = (λ−460)/490 over 460..950 nm has dr/dλ = 1/490
// everywhere; the midpoint of the first band pair is 465 nm.
//
// DEF-5: SID over fully disjoint supports must be +inf, not 0.0. [1,0,0]
// vs [0,1,0] skipped every KL term and returned 0.0 — the value that means
// "spectrally identical" — so a disjoint pixel tied with genuine matches in
// sidClassify and won by lowest reference index. Partial overlap (zero band
// on one side only) keeps the pinned finite contract.

#include "synthetic_raster_builder.h"

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/algorithms/spectral_classification.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QString>
#include <QTemporaryDir>

#include <gdal.h>
#include <gdal_priv.h>
#include <json/json.h>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace sicnu::testing;
using namespace sicnu::operators;

namespace
{

constexpr float kNoData = -9999.0f;

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

/// Stamps per-band WAVELENGTH (+ WAVELENGTH_UNITS when non-empty).
void stampWavelengths( const QString &path, const std::vector<double> &valuesNm,
                       const QString &unit )
{
    GDALDataset *ds = static_cast<GDALDataset *>( GDALOpen( path.toUtf8().constData(), GA_Update ) );
    REQUIRE( ds != nullptr );
    for ( int b = 0; b < static_cast<int>( valuesNm.size() ); ++b )
    {
        ds->GetRasterBand( b + 1 )
            ->SetMetadataItem( "WAVELENGTH", std::to_string( valuesNm[b] ).c_str() );
        if ( !unit.isEmpty() )
            ds->GetRasterBand( b + 1 )
                ->SetMetadataItem( "WAVELENGTH_UNITS", unit.toUtf8().constData() );
    }
    GDALClose( ds );
}

/// Builds a 4x4, 50-band cube whose band b carries reflectance r_b =
/// 10·b/490 — the linear ramp r(λ) = (λ−460)/490 sampled every 10 nm.
QString writeRampCube( const QTemporaryDir &dir, const QString &name )
{
    constexpr int kBands = 50;
    constexpr int kSide = 4;
    RsSyntheticRasterBuilder b( kSide, kSide, kBands, GDT_Float32 );
    b.withCrs( "EPSG:32650" );
    b.withGeoTransform( 0.0, 1.0, static_cast<double>( kSide ), -1.0 );
    for ( int band = 0; band < kBands; ++band )
    {
        const float value = static_cast<float>( 10.0 * band / 490.0 );
        for ( int y = 0; y < kSide; ++y )
            for ( int x = 0; x < kSide; ++x )
                b.withPixel( band + 1, x, y, value );
    }
    return b.writeToDisk( dir.filePath( name ) );
}

} // namespace

TEST_CASE( "rs:spectral_derivative honours WAVELENGTH_UNITS (μm cube)",
           "[spectral][derivative][r25a][operator][e2e]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString cube = writeRampCube( dir, "ramp_um.tif" );
    REQUIRE( !cube.isEmpty() );
    // Stamp the axis in micrometres: 0.46, 0.47, ..., 0.95.
    std::vector<double> umAxis;
    for ( int b = 0; b < 50; ++b )
        umAxis.push_back( 0.46 + 0.01 * b );
    stampWavelengths( cube, umAxis, QStringLiteral( "Micrometers" ) );

    Json::Value params( Json::objectValue );
    params["input"] = cube.toStdString();
    params["output"] = dir.filePath( "deriv.tif" ).toStdString();
    params["order"] = 1;
    const RunResult run = runOperator( "rs:spectral_derivative", params );
    REQUIRE( run.ok );
    CHECK( run.json["bandsOut"].asInt() == 49 );

    // The derivative of r(λ) = (λ−460)/490 is 1/490 per nm everywhere.
    // RED on master: 1/0.49 = 2.0408... (1000x).
    GdalDatasetWrapper outDs;
    REQUIRE( outDs.open( dir.filePath( "deriv.tif" ) ) );
    CHECK( outDs.bandCount() == 49 );
    std::vector<float> first( 16 );
    std::vector<float> last( 16 );
    REQUIRE( outDs.readBandData( 1, first.data(), 4, 4 ) );
    REQUIRE( outDs.readBandData( 49, last.data(), 4, 4 ) );
    CHECK( first[0] == Catch::Approx( 1.0 / 490.0 ).epsilon( 1e-6 ) );
    CHECK( last[0] == Catch::Approx( 1.0 / 490.0 ).epsilon( 1e-6 ) );

    // Midpoint of the first band pair: (460 + 470)/2 = 465 nm.
    // RED on master: 0.465.
    CHECK( run.json["outputWavelengthsNm"][0].asDouble() == Catch::Approx( 465.0 ).epsilon( 1e-6 ) );
    CHECK( run.json["outputWavelengthsNm"][48].asDouble()
           == Catch::Approx( 945.0 ).epsilon( 1e-6 ) );
}

TEST_CASE( "rs:spectral_derivative keeps the nm default and refuses unknown units",
           "[spectral][derivative][r25a][operator][e2e]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    SECTION( "explicit wavelengths parameter keeps its nm contract" )
    {
        const QString cube = writeRampCube( dir, "ramp_param.tif" );
        Json::Value params( Json::objectValue );
        params["input"] = cube.toStdString();
        params["output"] = dir.filePath( "deriv_param.tif" ).toStdString();
        params["order"] = 1;
        Json::Value axis( Json::arrayValue );
        for ( int b = 0; b < 50; ++b )
            axis.append( 460.0 + 10.0 * b );
        params["wavelengths"] = axis;
        const RunResult run = runOperator( "rs:spectral_derivative", params );
        REQUIRE( run.ok );
        GdalDatasetWrapper outDs;
        REQUIRE( outDs.open( dir.filePath( "deriv_param.tif" ) ) );
        std::vector<float> deriv( 49 );
        REQUIRE( outDs.readBandData( 1, deriv.data(), 4, 4 ) );
        CHECK( deriv[0] == Catch::Approx( 1.0 / 490.0 ).epsilon( 1e-6 ) );
        CHECK( run.json["outputWavelengthsNm"][0].asDouble()
               == Catch::Approx( 465.0 ).epsilon( 1e-6 ) );
    }

    SECTION( "no WAVELENGTH_UNITS defaults to nm (pinned behaviour)" )
    {
        const QString cube = writeRampCube( dir, "ramp_nm.tif" );
        std::vector<double> nmAxis;
        for ( int b = 0; b < 50; ++b )
            nmAxis.push_back( 460.0 + 10.0 * b );
        stampWavelengths( cube, nmAxis, QString() );
        Json::Value params( Json::objectValue );
        params["input"] = cube.toStdString();
        params["output"] = dir.filePath( "deriv_nm.tif" ).toStdString();
        params["order"] = 1;
        const RunResult run = runOperator( "rs:spectral_derivative", params );
        REQUIRE( run.ok );
        GdalDatasetWrapper outDs;
        REQUIRE( outDs.open( dir.filePath( "deriv_nm.tif" ) ) );
        std::vector<float> deriv( 49 );
        REQUIRE( outDs.readBandData( 1, deriv.data(), 4, 4 ) );
        CHECK( deriv[0] == Catch::Approx( 1.0 / 490.0 ).epsilon( 1e-6 ) );
    }

    SECTION( "unknown units are a typed refusal, not a silent nm read" )
    {
        const QString cube = writeRampCube( dir, "ramp_angstrom.tif" );
        std::vector<double> nmAxis;
        for ( int b = 0; b < 50; ++b )
            nmAxis.push_back( 4600.0 + 100.0 * b ); // angstroms
        stampWavelengths( cube, nmAxis, QStringLiteral( "Angstrom" ) );
        Json::Value params( Json::objectValue );
        params["input"] = cube.toStdString();
        params["output"] = dir.filePath( "deriv_bad.tif" ).toStdString();
        params["order"] = 1;
        const RunResult run = runOperator( "rs:spectral_derivative", params );
        CHECK_FALSE( run.ok ); // RED on master: silently treated as nm
        CHECK( run.error.code() == ErrorCode::InvalidInputData );
    }
}

TEST_CASE( "SID divergence: disjoint supports are infinite, partial overlap finite",
           "[spectral][sid][r25a]" )
{
    using SpectralClassification::spectralDivergence;

    // Disjoint supports: maximal divergence, NOT "identical" 0.0.
    const float t[] = { 1.0f, 0.0f, 0.0f };
    const float r[] = { 0.0f, 1.0f, 0.0f };
    CHECK( std::isinf( spectralDivergence( t, r, 3, kNoData ) ) ); // RED: 0.0
    CHECK( spectralDivergence( t, r, 3, kNoData ) > 0.0 );

    // Partial overlap keeps the pinned finite contract (zero band on one
    // side only is an unobserved band, not infinite evidence).
    const float tPartial[] = { 0.0f, 0.5f };
    const float rPartial[] = { 0.5f, 0.5f };
    CHECK( std::isfinite( spectralDivergence( tPartial, rPartial, 2, kNoData ) ) );

    // Identical spectra still diverge zero.
    const float same[] = { 0.2f, 0.4f, 0.4f };
    CHECK( spectralDivergence( same, same, 3, kNoData )
           == Catch::Approx( 0.0 ).margin( 1e-9 ) );
}

TEST_CASE( "sidClassify never labels a disjoint pixel as its first reference",
           "[spectral][sid][r25a]" )
{
    // Pixel support {band 0}; ref 0 support {band 1} (disjoint), ref 1
    // support {bands 0,1} (finite divergence).
    const float pixel[] = { 1.0f, 0.0f };
    const float refs[] = { 0.0f, 1.0f,
                           0.9f, 0.1f };
    int labels[1] = { -1 };
    float divergences[1] = { 0.0f };
    REQUIRE( SpectralClassification::sidClassify( pixel, 1, 2, refs, 2, labels, divergences,
                                                  kNoData ) );
    CHECK( labels[0] == 1 ); // RED on master: 0 with divergence 0.0
    CHECK( std::isfinite( divergences[0] ) );
    CHECK( divergences[0] > 0.0f );
}

TEST_CASE( "rs:spectral_resample honours WAVELENGTH_UNITS on the source axis",
           "[spectral][resample][r25a][operator][e2e]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    // Same μm-stamped ramp cube as the derivative case: 460..950 nm axis
    // declared as 0.46..0.95 μm, reflectance r(λ) = (λ−460)/490.
    const QString cube = writeRampCube( dir, "ramp_resample_um.tif" );
    REQUIRE( !cube.isEmpty() );
    std::vector<double> umAxis;
    for ( int b = 0; b < 50; ++b )
        umAxis.push_back( 0.46 + 0.01 * b );
    stampWavelengths( cube, umAxis, QStringLiteral( "Micrometers" ) );

    Json::Value params( Json::objectValue );
    params["input"] = cube.toStdString();
    params["output"] = dir.filePath( "resampled.tif" ).toStdString();
    Json::Value targets( Json::arrayValue );
    targets.append( 500.0 );
    targets.append( 600.0 );
    targets.append( 700.0 );
    params["wavelengths"] = targets; // target axis, nm
    const RunResult run = runOperator( "rs:spectral_resample", params );
    REQUIRE( run.ok );
    CHECK( run.json["bands"].asInt() == 3 );

    // Linear interpolation of the ramp at 500/600/700 nm is exact:
    // r = (λ−460)/490. RED on master: the μm source axis (0.46..0.95 "nm")
    // places every nm target out of range → all NaN.
    GdalDatasetWrapper outDs;
    REQUIRE( outDs.open( dir.filePath( "resampled.tif" ) ) );
    std::vector<float> band( 16 );
    const double expected[3] = { 40.0 / 490.0, 140.0 / 490.0, 240.0 / 490.0 };
    for ( int b = 0; b < 3; ++b )
    {
        REQUIRE( outDs.readBandData( b + 1, band.data(), 4, 4 ) );
        for ( float v : band )
            CHECK( v == Catch::Approx( expected[b] ).epsilon( 1e-5 ) );
    }
}

TEST_CASE( "rs:apply_mask preserves WAVELENGTH_UNITS band metadata",
           "[optical][mask][r25a][operator][e2e]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    // 3-band cube stamped in μm + an all-valid (0 = keep) mask.
    RsSyntheticRasterBuilder mask( 4, 4, 1, GDT_Byte );
    mask.withCrs( "EPSG:32650" );
    mask.withGeoTransform( 0.0, 1.0, 4.0, -1.0 );
    for ( int y = 0; y < 4; ++y )
        for ( int x = 0; x < 4; ++x )
            mask.withPixel( 1, x, y, 0.0f );
    const QString maskPath = mask.writeToDisk( dir.filePath( "keep_mask.tif" ) );
    REQUIRE( !maskPath.isEmpty() );

    // Rebuild a small μm-stamped 3-band source (writeRampCube is 50-band;
    // masking cares about metadata, three bands suffice).
    RsSyntheticRasterBuilder src( 4, 4, 3, GDT_Float32 );
    src.withCrs( "EPSG:32650" );
    src.withGeoTransform( 0.0, 1.0, 4.0, -1.0 );
    src.withNoData( -9999.0 ); // apply_mask requires declared per-band NoData
    for ( int band = 1; band <= 3; ++band )
        for ( int y = 0; y < 4; ++y )
            for ( int x = 0; x < 4; ++x )
                src.withPixel( band, x, y, 0.1f * band );
    const QString srcPath = src.writeToDisk( dir.filePath( "um_src.tif" ) );
    REQUIRE( !srcPath.isEmpty() );
    stampWavelengths( srcPath, { 0.48, 0.56, 0.65 }, QStringLiteral( "Micrometers" ) );

    Json::Value params( Json::objectValue );
    params["input"] = srcPath.toStdString();
    params["mask"] = maskPath.toStdString();
    params["output"] = dir.filePath( "masked.tif" ).toStdString();
    const RunResult run = runOperator( "rs:apply_mask", params );
    REQUIRE( run.ok );

    // The units tag must survive the masking seam: WAVELENGTH travels, so
    // its units must too (RED on master: units dropped, 0.48 read as nm
    // downstream).
    GDALDatasetH out = GDALOpen( dir.filePath( "masked.tif" ).toUtf8().constData(), GA_ReadOnly );
    REQUIRE( out != nullptr );
    GDALRasterBandH b1 = GDALGetRasterBand( out, 1 );
    REQUIRE( b1 != nullptr );
    const char *units = GDALGetMetadataItem( b1, "WAVELENGTH_UNITS", nullptr );
    CHECK( units != nullptr ); // RED on master: nullptr
    if ( units )
        CHECK( QString::fromUtf8( units ).compare( QStringLiteral( "Micrometers" ),
                                                   Qt::CaseInsensitive ) == 0 );
    const char *wl = GDALGetMetadataItem( b1, "WAVELENGTH", nullptr );
    REQUIRE( wl != nullptr );
    CHECK( QString::fromUtf8( wl ).toDouble() == Catch::Approx( 0.48 ) );
    GDALClose( out );
}
