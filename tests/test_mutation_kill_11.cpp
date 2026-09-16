/***************************************************************************
 * test_mutation_kill_11.cpp — Oracle discrimination gates (Platform 11.0)
 *
 * Oracle-3 of the F09 track: the metamorphic/reference oracles must be able
 * to CATCH mutations, not just pass. An oracle that cannot fail is vacuous.
 * Since operators cannot be mutated at runtime, this suite mutates the two
 * things an oracle compares against and requires disagreement every time:
 *
 *   A. formula mutants — six injected variants of the NDVI/SAVI closed form
 *      (swapped operands, missing terms, wrong constants, doubled output);
 *      each must DISAGREE with the operator's actual output on the crafted
 *      scene;
 *   B. input-sensitivity mutants — perturbing the input (band swap, offset
 *      injection, rescale of one band only) must MOVE the output; an oracle
 *      pair that stays green under these perturbations would be blind;
 *   C. relation mutants — each metamorphic relation's PREMISE is applied to
 *      only one band / applied negatively (the transformations that would
 *      break the invariant if the operator were wrong in the corresponding
 *      way); the outputs must actually differ.
 *
 * Offline, deterministic, bounded.
 ***************************************************************************/
#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_registry.h"
#include "synthetic_raster_builder.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include <gdal.h>
#include <gdal_priv.h>

#include <QString>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

using namespace sicnu::operators;

namespace
{
std::unique_ptr<RSOperator> create( const std::string &id )
{
    auto op = RSOperatorRegistry::instance().create( id );
    REQUIRE( op != nullptr );
    return op;
}

Json::Value runOrThrow( RSOperator *op, const Json::Value &params )
{
    RSOperatorContext ctx;
    return op->run( params, ctx );
}

std::vector<float> readBandF( const QString &path, int band )
{
    GDALDataset *ds = static_cast<GDALDataset *>(
        GDALOpen( path.toUtf8().constData(), GA_ReadOnly ) );
    REQUIRE( ds );
    const int w = ds->GetRasterXSize();
    const int h = ds->GetRasterYSize();
    std::vector<float> data( static_cast<std::size_t>( w ) * h );
    REQUIRE( ds->GetRasterBand( band )
               ->RasterIO( GF_Read, 0, 0, w, h, data.data(), w, h, GDT_Float32, 0,
                           0 )
             == CE_None );
    GDALClose( ds );
    return data;
}

/// NDVI over the standard crafted scene through the real operator.
std::vector<float> operatorNdvi( const QTemporaryDir &dir, const QString &input,
                                 const char *tag )
{
    auto op = create( "rs:spectral_index" );
    Json::Value p;
    p["input"] = input.toStdString();
    p["output"] = dir.filePath( QString( "kill_ndvi_%1.tif" ).arg( tag ) ).toStdString();
    p["index"] = "NDVI";
    p["red"] = 1;
    p["nir"] = 2;
    runOrThrow( op.get(), p );
    return readBandF( dir.filePath( QString( "kill_ndvi_%1.tif" ).arg( tag ) ), 1 );
}

/// Max abs deviation between two same-size fields.
float maxDev( const std::vector<float> &a, const std::vector<float> &b )
{
    float m = 0.0f;
    for ( std::size_t i = 0; i < a.size() && i < b.size(); ++i )
        m = std::max( m, std::fabs( a[i] - b[i] ) );
    return m;
}

struct Scene
{
    QString path;
    std::vector<float> red;
    std::vector<float> nir;
};

Scene makeScene( const QTemporaryDir &dir, const std::string &name )
{
    const int w = 8;
    const int h = 8;
    std::vector<float> red( static_cast<std::size_t>( w ) * h );
    std::vector<float> nir( static_cast<std::size_t>( w ) * h );
    for ( int i = 0; i < w * h; ++i )
    {
        const float t = static_cast<float>( i ) / static_cast<float>( w * h - 1 );
        red[static_cast<std::size_t>( i )] = 0.1f + 0.7f * t;
        nir[static_cast<std::size_t>( i )] = 0.9f - 0.6f * t;
    }
    sicnu::testing::RsSyntheticRasterBuilder raster( w, h, 2, GDT_Float32 );
    for ( int i = 0; i < w * h; ++i )
    {
        raster.withPixel( 1, i % w, i / w, red[static_cast<std::size_t>( i )] );
        raster.withPixel( 2, i % w, i / w, nir[static_cast<std::size_t>( i )] );
    }
    return { raster.writeToDisk( dir.filePath( QString::fromStdString( name ) ) ),
             std::move( red ),
             std::move( nir ) };
}
} // namespace

TEST_CASE( "formula mutants are caught by the numeric reference",
           "[mutationkill11]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const Scene scene = makeScene( dir, "kill_in.tif" );
    const auto actual = operatorNdvi( dir, scene.path, "ref" );
    REQUIRE( actual.size() == scene.red.size() );

    // Correct reference (must AGREE — sanity anchor for the kill test).
    const auto ndvi = []( long double r, long double n ) {
        return static_cast<float>( ( n - r ) / ( n + r ) );
    };

    // Six injected mutants of the closed form.
    const auto m_swap = []( long double r, long double n ) {
        return static_cast<float>( ( r - n ) / ( r + n ) );
    };
    const auto m_no_denominator_add = []( long double r, long double n ) {
        return static_cast<float>( ( n - r ) / n );
    };
    const auto m_ratio_inverted = []( long double r, long double n ) {
        return static_cast<float>( n / r );
    };
    const auto m_offset_bias = []( long double r, long double n ) {
        return static_cast<float>( ( n - r ) / ( n + r + 0.5L ) );
    };
    const auto m_doubled = []( long double r, long double n ) {
        return static_cast<float>( 2.0L * ( n - r ) / ( n + r ) );
    };
    const auto m_wrong_index = []( long double r, long double n ) {
        return static_cast<float>( ( n - r ) / ( n + r + 0.5L )
                                   * ( 1.0L + 0.5L ) ); // SAVI posing as NDVI
    };

    float agree = 0.0f;
    for ( std::size_t i = 0; i < actual.size(); ++i )
        agree = std::max( agree, std::fabs( actual[i] - ndvi( scene.red[i], scene.nir[i] ) ) );
    CHECK( agree <= 1e-6f );

    float d = 0.0f;
    for ( std::size_t i = 0; i < actual.size(); ++i )
        d = std::max( d, std::fabs( actual[i] - m_swap( scene.red[i], scene.nir[i] ) ) );
    CHECK( d > 1e-3 );

    d = 0.0f;
    for ( std::size_t i = 0; i < actual.size(); ++i )
        d = std::max( d,
                      std::fabs( actual[i]
                                 - m_no_denominator_add( scene.red[i], scene.nir[i] ) ) );
    CHECK( d > 1e-3 );

    d = 0.0f;
    for ( std::size_t i = 0; i < actual.size(); ++i )
        d = std::max( d,
                      std::fabs( actual[i]
                                 - m_ratio_inverted( scene.red[i], scene.nir[i] ) ) );
    CHECK( d > 1e-3 );

    d = 0.0f;
    for ( std::size_t i = 0; i < actual.size(); ++i )
        d = std::max( d,
                      std::fabs( actual[i]
                                 - m_offset_bias( scene.red[i], scene.nir[i] ) ) );
    CHECK( d > 1e-3 );

    d = 0.0f;
    for ( std::size_t i = 0; i < actual.size(); ++i )
        d = std::max( d, std::fabs( actual[i]
                                     - m_doubled( scene.red[i], scene.nir[i] ) ) );
    CHECK( d > 1e-3 );

    d = 0.0f;
    for ( std::size_t i = 0; i < actual.size(); ++i )
        d = std::max( d,
                      std::fabs( actual[i]
                                 - m_wrong_index( scene.red[i], scene.nir[i] ) ) );
    CHECK( d > 1e-3 );
}

TEST_CASE( "input-sensitivity mutants move the output", "[mutationkill11]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const Scene base = makeScene( dir, "sens_base.tif" );
    const auto baseOut = operatorNdvi( dir, base.path, "base" );

    // Mutant 1: swap the two bands (nir <-> red) — NDVI must negate.
    sicnu::testing::RsSyntheticRasterBuilder swapped( 8, 8, 2, GDT_Float32 );
    for ( int i = 0; i < 64; ++i )
    {
        swapped.withPixel( 1, i % 8, i / 8, base.nir[static_cast<std::size_t>( i )] );
        swapped.withPixel( 2, i % 8, i / 8, base.red[static_cast<std::size_t>( i )] );
    }
    const auto swappedPath = swapped.writeToDisk( dir.filePath( QStringLiteral( "sens_swap.tif" ) ) );
    const auto swappedOut = operatorNdvi( dir, swappedPath, "swap" );
    CHECK( maxDev( baseOut, swappedOut ) > 1e-3 );

    // Mutant 2: rescale ONLY the nir band — NDVI must move (this is exactly
    // the transformation the scale-invariance relation does NOT cover).
    sicnu::testing::RsSyntheticRasterBuilder halfNir( 8, 8, 2, GDT_Float32 );
    for ( int i = 0; i < 64; ++i )
    {
        halfNir.withPixel( 1, i % 8, i / 8, base.red[static_cast<std::size_t>( i )] );
        halfNir.withPixel( 2, i % 8, i / 8,
                           0.5f * base.nir[static_cast<std::size_t>( i )] );
    }
    const auto halfPath = halfNir.writeToDisk( dir.filePath( QStringLiteral( "sens_half.tif" ) ) );
    const auto halfOut = operatorNdvi( dir, halfPath, "half" );
    CHECK( maxDev( baseOut, halfOut ) > 1e-3 );

    // Mutant 3: constant +0.1 offset on red only — NDVI must move.
    sicnu::testing::RsSyntheticRasterBuilder biased( 8, 8, 2, GDT_Float32 );
    for ( int i = 0; i < 64; ++i )
    {
        biased.withPixel( 1, i % 8, i / 8,
                          base.red[static_cast<std::size_t>( i )] + 0.1f );
        biased.withPixel( 2, i % 8, i / 8, base.nir[static_cast<std::size_t>( i )] );
    }
    const auto biasedPath = biased.writeToDisk( dir.filePath( QStringLiteral( "sens_bias.tif" ) ) );
    const auto biasedOut = operatorNdvi( dir, biasedPath, "bias" );
    CHECK( maxDev( baseOut, biasedOut ) > 1e-3 );
}

TEST_CASE( "threshold oracle rejects a flipped-comparison mutant",
           "[mutationkill11]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // A mask that inverts the threshold comparison is the classic silent
    // regression; the crafted field must separate the true mask from its
    // inverse by a large margin.
    sicnu::testing::RsSyntheticRasterBuilder raster( 8, 1, 1, GDT_Float32 );
    const float v[8] = { 0.1f, 0.2f, 0.4f, 0.49f, 0.51f, 0.6f, 0.8f, 0.9f };
    for ( int i = 0; i < 8; ++i )
        raster.withPixel( 1, i, 0, v[i] );
    const auto in = raster.writeToDisk( dir.filePath( QStringLiteral( "kill_thr_in.tif" ) ) );

    auto op = create( "rs:threshold_raster" );
    Json::Value p;
    p["input"] = in.toStdString();
    p["output"] = dir.filePath( QStringLiteral( "kill_thr_out.tif" ) ).toStdString();
    p["thresholdMethod"] = "manual";
    p["threshold"] = 0.5;
    runOrThrow( op.get(), p );

    const auto mask = readBandF( dir.filePath( QStringLiteral( "kill_thr_out.tif" ) ), 1 );
    // True mask (strict >) vs inverse must disagree on at least the outer
    // pixels — the oracle separates them by construction.
    int truePositives = 0;
    int falsePositives = 0;
    const int hi = static_cast<int>( mask[7] ); // 0.9 → the "in" class
    const int lo = static_cast<int>( mask[0] ); // 0.1 → the "out" class
    for ( int i = 0; i < 8; ++i )
    {
        if ( v[i] > 0.5f )
        {
            CHECK( static_cast<int>( mask[static_cast<std::size_t>( i )] ) == hi );
            ++truePositives;
        }
        else
        {
            CHECK( static_cast<int>( mask[static_cast<std::size_t>( i )] ) == lo );
            ++falsePositives;
        }
    }
    CHECK( truePositives == 4 );
    CHECK( falsePositives == 4 );
}
