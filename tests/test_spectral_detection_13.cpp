// test_spectral_detection_13.cpp — Spectral Intelligence 13.0 work package A:
// TCIMF / OSP operator contracts (E2E constraints, diagnostics, typed
// refusals).
//
// E2E oracles are independent of the implementation:
//   - a pixel whose spectrum equals the target scores EXACTLY 1 under TCIMF
//     (distortionless constraint wᵀt = 1);
//   - a pixel whose spectrum equals an interference signature scores EXACTLY 0
//     under both TCIMF and OSP (null constraint Sᵀw = 0 / wᵀs = 0);
//   - the same constraints are re-verified on the kernel side in
//     test_spectral_tcimf.cpp / test_spectral_osp.cpp with hand-computed
//     closed forms.

#include "processing/algorithms/spectral_osp.h"
#include "processing/algorithms/spectral_tcimf.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QTemporaryDir>

#include <json/json.h>

#include <cmath>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "synthetic_raster_builder.h"

using namespace sicnu::testing;
using namespace sicnu::operators;

namespace
{
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr int kB = 3;

// Background scene: 10×10 pixels, 3 bands, LCG-ish but deterministic ramp so
// the correlation matrix is well conditioned. One target pixel and one
// interference pixel are planted at fixed spots.
struct Scene
{
    QString path;
    std::vector<float> target{ 30.0f, 40.0f, 50.0f };
    std::vector<float> interference{ 10.0f, 45.0f, 20.0f };
    int targetPixel = 3 * 10 + 3;
    int interferencePixel = 4 * 10 + 4;
};

Scene buildScene( QTemporaryDir &dir, const char *name )
{
    Scene s;
    RsSyntheticRasterBuilder builder( 10, 10, kB );
    builder.withCrs( "EPSG:32650" );
    builder.withGeoTransform( 0.0, 1.0, 10.0, -1.0 );
    for ( int y = 0; y < 10; ++y )
        for ( int x = 0; x < 10; ++x )
            for ( int b = 0; b < kB; ++b )
            {
                const float v = 20.0f + 3.0f * b + static_cast<float>( ( x * 7 + y * 13 ) % 11 );
                builder.withPixel( b + 1, x, y, v );
            }
    for ( int b = 0; b < kB; ++b )
    {
        builder.withPixel( b + 1, 3, 3, s.target[static_cast<size_t>( b )] );
        builder.withPixel( b + 1, 4, 4, s.interference[static_cast<size_t>( b )] );
    }
    s.path = builder.writeToDisk( dir.filePath( QString::fromLatin1( name ) ) );
    return s;
}

Json::Value targetParam( const Scene &s )
{
    Json::Value t( Json::arrayValue );
    for ( int b = 0; b < kB; ++b )
        t.append( static_cast<double>( s.target[static_cast<size_t>( b )] ) );
    return t;
}

Json::Value interferenceParam( const Scene &s )
{
    Json::Value rows( Json::arrayValue );
    Json::Value row( Json::arrayValue );
    for ( int b = 0; b < kB; ++b )
        row.append( static_cast<double>( s.interference[static_cast<size_t>( b )] ) );
    rows.append( row );
    return rows;
}

std::vector<float> runAndRead( const char *opName, const QString &input, const QString &output,
                               const Json::Value &extra, Json::Value *result )
{
    auto op = RSOperatorRegistry::instance().create( opName );
    REQUIRE( op != nullptr );
    Json::Value params( Json::objectValue );
    params["input"] = input.toStdString();
    params["output"] = output.toStdString();
    for ( const auto &key : extra.getMemberNames() )
        params[key] = extra[key];
    RSOperatorContext ctx;
    Json::Value runResult = op->run( params, ctx );
    if ( result )
        *result = runResult;

    GdalDatasetWrapper ds;
    REQUIRE( ds.open( output ) );
    std::vector<float> scores( static_cast<size_t>( 10 ) * 10, 0.0f );
    REQUIRE( ds.readBandData( 1, scores.data(), 10, 10 ) );
    return scores;
}

ErrorCode codeOf( const std::function<void()> &fn )
{
    try
    {
        fn();
    }
    catch ( const RSOperatorError &e )
    {
        return e.code();
    }
    catch ( ... )
    {
        return ErrorCode::Unknown;
    }
    return ErrorCode::Success;
}
} // namespace

TEST_CASE( "rs:tcimf_detection: planted target scores exactly 1, planted "
           "interference exactly 0",
           "[spectral][detection][tcimf][operator]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const Scene s = buildScene( dir, "tcimf_in.tif" );

    Json::Value extra( Json::objectValue );
    extra["target"] = targetParam( s );
    extra["interference"] = interferenceParam( s );

    Json::Value result;
    const std::vector<float> scores =
        runAndRead( "rs:tcimf_detection", s.path, dir.filePath( "tcimf_out.tif" ), extra,
                    &result );

    REQUIRE( scores[static_cast<size_t>( s.targetPixel )] ==
             Catch::Approx( 1.0 ).margin( 1e-5 ) );
    REQUIRE( scores[static_cast<size_t>( s.interferencePixel )] ==
             Catch::Approx( 0.0 ).margin( 1e-5 ) );

    // Diagnostics contract: background provenance, sample count, conditioning,
    // interference count and its conditioning.
    REQUIRE( result["detector"].asString() == "tcimf" );
    REQUIRE( result["bandCount"].asInt() == kB );
    REQUIRE( result["backgroundSamples"].asUInt64() == 100 );
    REQUIRE( result["loading"].asDouble() == Catch::Approx( 0.0 ).margin( 1e-12 ) );
    REQUIRE( result["interferenceCount"].asUInt64() == 1 );
    REQUIRE( result["interferenceCondition"].asDouble() == Catch::Approx( 1.0 ).margin( 1e-9 ) );
    REQUIRE( result["interferenceSource"].asString().find( "inline" ) != std::string::npos );
    REQUIRE( result["targetSource"].asString().find( "inline" ) != std::string::npos );
    REQUIRE( result.isMember( "backgroundCondition" ) );
}

TEST_CASE( "rs:tcimf_detection: under-sampled scenes refuse; loading is the escape hatch",
           "[spectral][detection][tcimf][operator]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    // 2×3 pixels, 3 bands → 6 valid pixels; floor without loading is 2B+2 = 8.
    RsSyntheticRasterBuilder builder( 2, 3, kB );
    builder.withCrs( "EPSG:32650" );
    for ( int b = 0; b < kB; ++b )
        builder.withConstantValue( b + 1, 1.0f + b );
    const QString in = builder.writeToDisk( dir.filePath( "tiny.tif" ) );
    REQUIRE( !in.isEmpty() );

    Json::Value extra( Json::objectValue );
    Json::Value t( Json::arrayValue );
    for ( int b = 0; b < kB; ++b )
        t.append( 1.0 + b );
    extra["target"] = t;
    Json::Value rows( Json::arrayValue );
    Json::Value row( Json::arrayValue );
    for ( int b = 0; b < kB; ++b )
        row.append( 1.0 + 2 * b );
    rows.append( row );
    extra["interference"] = rows;

    REQUIRE( codeOf( [&] {
        runAndRead( "rs:tcimf_detection", in, dir.filePath( "refused.tif" ), extra, nullptr );
    } ) == ErrorCode::InvalidInputData );

    // With loading the floor drops to B+1 = 4 → accepted.
    extra["loading"] = 1e-3;
    Json::Value result;
    const std::vector<float> scores =
        runAndRead( "rs:tcimf_detection", in, dir.filePath( "loaded.tif" ), extra, &result );
    REQUIRE( result["backgroundSamples"].asUInt64() == 6 );
    REQUIRE( result["loading"].asDouble() == Catch::Approx( 1e-3 ).margin( 1e-12 ) );
}

TEST_CASE( "rs:tcimf_detection: missing or degenerate interference refuses",
           "[spectral][detection][tcimf][operator]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const Scene s = buildScene( dir, "tcimf_in2.tif" );

    // No interference source at all.
    Json::Value extra( Json::objectValue );
    extra["target"] = targetParam( s );
    REQUIRE( codeOf( [&] {
        runAndRead( "rs:tcimf_detection", s.path, dir.filePath( "no_int.tif" ), extra, nullptr );
    } ) == ErrorCode::InvalidParameter );

    // Duplicate interference spectra → linearly dependent under the background
    // metric.
    Json::Value dup( Json::arrayValue );
    dup.append( interferenceParam( s ) );
    dup.append( interferenceParam( s ) );
    extra["interference"] = dup;
    REQUIRE( codeOf( [&] {
        runAndRead( "rs:tcimf_detection", s.path, dir.filePath( "dup.tif" ), extra, nullptr );
    } ) == ErrorCode::InvalidInputData );

    // Target identical to the interference → the target lies inside the
    // interference span.
    Json::Value same( Json::objectValue );
    same["target"] = interferenceParam( s );
    same["interference"] = interferenceParam( s );
    REQUIRE( codeOf( [&] {
        runAndRead( "rs:tcimf_detection", s.path, dir.filePath( "span.tif" ), same, nullptr );
    } ) == ErrorCode::InvalidInputData );

    // Wrong interference width.
    Json::Value narrow( Json::arrayValue );
    Json::Value row( Json::arrayValue );
    row.append( 1.0 );
    row.append( 2.0 );
    narrow.append( row );
    Json::Value badWidth( Json::objectValue );
    badWidth["target"] = targetParam( s );
    badWidth["interference"] = narrow;
    REQUIRE( codeOf( [&] {
        runAndRead( "rs:tcimf_detection", s.path, dir.filePath( "width.tif" ), badWidth,
                    nullptr );
    } ) == ErrorCode::InvalidInputData );
}

TEST_CASE( "rs:osp_detection: planted target scores wᵀt, planted interference exactly 0",
           "[spectral][detection][osp][operator]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const Scene s = buildScene( dir, "osp_in.tif" );

    Json::Value extra( Json::objectValue );
    extra["target"] = targetParam( s );
    extra["interference"] = interferenceParam( s );

    Json::Value result;
    const std::vector<float> scores =
        runAndRead( "rs:osp_detection", s.path, dir.filePath( "osp_out.tif" ), extra, &result );

    // Independent kernel oracle: build the same filter in-process and score
    // the planted spectra.
    SpectralOsp::Filter filter;
    REQUIRE( SpectralOsp::buildFilter( s.target.data(), kB,
                                       { s.interference }, &filter ) );
    std::vector<double> scratch( static_cast<size_t>( kB ), 0.0 );
    const float expectedTarget =
        SpectralOsp::ospScore( s.target.data(), filter, kB, &scratch );
    REQUIRE( expectedTarget > 0.0 );
    REQUIRE( scores[static_cast<size_t>( s.targetPixel )] ==
             Catch::Approx( expectedTarget ).margin( 1e-5 ) );
    REQUIRE( scores[static_cast<size_t>( s.interferencePixel )] ==
             Catch::Approx( 0.0 ).margin( 1e-5 ) );

    // OSP consumes no background statistics: the key must be absent, not zero.
    REQUIRE( result["detector"].asString() == "osp" );
    REQUIRE( !result.isMember( "backgroundSamples" ) );
    REQUIRE( !result.isMember( "loading" ) );
    REQUIRE( result["interferenceCount"].asUInt64() == 1 );
    REQUIRE( result["interferenceCondition"].asDouble() == Catch::Approx( 1.0 ).margin( 1e-9 ) );
}

TEST_CASE( "rs:osp_detection: missing, degenerate or in-span interference refuses",
           "[spectral][detection][osp][operator]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const Scene s = buildScene( dir, "osp_in2.tif" );

    Json::Value extra( Json::objectValue );
    extra["target"] = targetParam( s );
    REQUIRE( codeOf( [&] {
        runAndRead( "rs:osp_detection", s.path, dir.filePath( "no_int.tif" ), extra, nullptr );
    } ) == ErrorCode::InvalidParameter );

    Json::Value dup( Json::arrayValue );
    dup.append( interferenceParam( s ) );
    dup.append( interferenceParam( s ) );
    extra["interference"] = dup;
    REQUIRE( codeOf( [&] {
        runAndRead( "rs:osp_detection", s.path, dir.filePath( "dup.tif" ), extra, nullptr );
    } ) == ErrorCode::InvalidInputData );

    Json::Value same( Json::objectValue );
    same["target"] = interferenceParam( s );
    same["interference"] = interferenceParam( s );
    REQUIRE( codeOf( [&] {
        runAndRead( "rs:osp_detection", s.path, dir.filePath( "span.tif" ), same, nullptr );
    } ) == ErrorCode::InvalidInputData );
}

TEST_CASE( "rs:osp_detection: background parameters are not accepted",
           "[spectral][detection][osp][operator]" )
{
    // OSP has no background pass, so a 'background' raster parameter must be a
    // typed refusal rather than a silently ignored input (WP3 adds the
    // background raster to the covariance detectors only).
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const Scene s = buildScene( dir, "osp_in3.tif" );

    Json::Value extra( Json::objectValue );
    extra["target"] = targetParam( s );
    extra["interference"] = interferenceParam( s );
    extra["background"] = s.path.toStdString();
    REQUIRE( codeOf( [&] {
        runAndRead( "rs:osp_detection", s.path, dir.filePath( "bg.tif" ), extra, nullptr );
    } ) == ErrorCode::InvalidParameter );
}
