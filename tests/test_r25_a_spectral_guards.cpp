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
    std::vector<float> deriv( 49 );
    REQUIRE( outDs.readBandData( 1, deriv.data(), 4, 4 ) );
    CHECK( deriv[0] == Catch::Approx( 1.0 / 490.0 ).epsilon( 1e-6 ) );
    CHECK( deriv[48] == Catch::Approx( 1.0 / 490.0 ).epsilon( 1e-6 ) );

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
