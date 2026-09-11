// test_known_answer_corpus_8.cpp — known-answer corpus expansion (task C,
// Verification Platform 8.0).
//
// Companion to test_known_answer_corpus.cpp (Verification 7.0): every
// expectation is ANALYTICALLY DERIVABLE from documented formulas/contracts —
// the derivation sits next to each assertion. Families added here (gaps in
// the 7.0 matrix):
//
//   family            | invariant
//   ------------------+-----------------------------------------------------
//   grid ops (window) | pixel value = 10·row + col survives readWindow
//                     | exactly; sub-window algebra; byte budget = w·h·
//                     | bands·sizeof(double); #808: exceeding the budget is
//                     | a typed GeoError(Unsupported), never bad_alloc.
//   grid ops (blocks) | tiled TIFF with known block geometry: edge blocks
//                     | return exactly blockW·blockH values padded with the
//                     | band NoData (#790 contract as closed form).
//   splits (random)   | largest-remainder counts for known N and ratios;
//                     | disjointness; every sample exactly once;
//                     | testRatio=0 ⇒ zero Test samples (#788 invariant as
//                     | corpus form); same seed ⇒ same manifest.
//   splits (spatial)  | spatial_block: whole blocks are atomic — no grid
//                     | cell may straddle roles (leakage invariant #775/#817
//                     | as corpus form).
//
// Deliberately NOT duplicated: spectral/change/terrain/radiometric/temporal
// (7.0 corpus), accuracy/kappa (test_accuracy_assessment), zonal statistics
// (not implemented in the platform — refused by the 7.0 scope contract).
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dataset/dataset_types.h"
#include "dataset/split.h"
#include "geospatial/common.h"
#include "geospatial/gdal_guard.h"
#include "geospatial/metadata/canonical_metadata.h"
#include "geospatial/raster/raster_reader.h"

#include <gdal.h>
#include <gdal_priv.h>

#include <QDateTime>
#include <QString>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <vector>

using namespace sicnu::geo;
using namespace sicnu::dataset;
using Catch::Matchers::WithinAbs;

namespace
{
/// Writes W×H Float32 with pixel value = 10·row + col (exact in float32 for
/// this magnitude range). When @p tiled, uses fixed BLOCKXSIZE×BLOCKYSIZE
/// geometry and stamps NoData = -9999 so block padding has a known answer.
QString writeGridRaster( const QString &path, int width, int height,
                         int blockW = 0, int blockH = 0 )
{
    GDALAllRegister();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    char **options = nullptr;
    if ( blockW > 0 && blockH > 0 )
    {
        options = CSLSetNameValue( options, "TILED", "YES" );
        options = CSLSetNameValue( options, "BLOCKXSIZE", QString::number( blockW ).toUtf8().constData() );
        options = CSLSetNameValue( options, "BLOCKYSIZE", QString::number( blockH ).toUtf8().constData() );
    }
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), width, height, 1,
                                  GDT_Float32, options );
    REQUIRE( ds != nullptr );
    if ( options )
        CSLDestroy( options );
    double gt[6] = { 0.0, 1.0, 0.0, static_cast<double>( height ), 0.0, -1.0 };
    GDALSetGeoTransform( ds, gt );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    REQUIRE( GDALSetRasterNoDataValue( band, -9999.0 ) == CE_None );
    std::vector<float> line( width );
    for ( int row = 0; row < height; ++row )
    {
        for ( int col = 0; col < width; ++col )
            line[col] = static_cast<float>( 10 * row + col );
        REQUIRE( GDALRasterIO( band, GF_Write, 0, row, width, 1, line.data(), width, 1,
                               GDT_Float32, 0, 0 ) == CE_None );
    }
    GDALClose( ds );
    return path;
}

int roleCount( const SplitManifest &manifest, SplitRole role )
{
    return static_cast<int>( std::count_if( manifest.assignments().cbegin(),
                                            manifest.assignments().cend(),
                                            [role]( const SplitAssignment &a ) {
                                                return a.role == role;
                                            } ) );
}

QVector<SplitInput> makeNumberedInputs( int count )
{
    QVector<SplitInput> inputs;
    for ( int i = 0; i < count; ++i )
    {
        SplitInput input;
        input.sampleId = QStringLiteral( "s%1" ).arg( i, 4, 10, QLatin1Char( '0' ) );
        inputs.append( input );
    }
    return inputs;
}
} // namespace

// ---------------------------------------------------------------------------
// Grid ops — window algebra
// ---------------------------------------------------------------------------

TEST_CASE( "known-answer: readWindow preserves the 10·row+col grid exactly",
           "[known_answer8][grid]" )
{
    QTemporaryDir dir;
    const auto path = writeGridRaster( dir.filePath( QStringLiteral( "grid.tif" ) ), 6, 4 );
    const auto reader = RasterReader::open( path.toStdString() );
    REQUIRE( reader.isOpen() );
    CHECK( reader.metadata().width == 6 );
    CHECK( reader.metadata().height == 4 );

    const auto full = reader.readWindow( { 1 }, { 0, 0, 6, 4 } );
    REQUIRE( full.size() == 6 * 4 );
    for ( int row = 0; row < 4; ++row )
        for ( int col = 0; col < 6; ++col )
        {
            INFO( "row=" << row << " col=" << col );
            REQUIRE_THAT( full[row * 6 + col], WithinAbs( 10.0 * row + col, 1e-6 ) );
        }

    // Sub-window [x=2, y=1, 3x2] is the exact slice of the same grid.
    const auto sub = reader.readWindow( { 1 }, { 2, 1, 3, 2 } );
    REQUIRE( sub.size() == 3 * 2 );
    const double expected[6] = { 12, 13, 14, 22, 23, 24 };
    for ( int i = 0; i < 6; ++i )
        REQUIRE_THAT( sub[i], WithinAbs( expected[i], 1e-6 ) );
}

TEST_CASE( "known-answer: window byte budget is exact and exceeded reads are "
           "typed errors (#808)",
           "[known_answer8][grid]" )
{
    QTemporaryDir dir;
    const auto path = writeGridRaster( dir.filePath( QStringLiteral( "grid.tif" ) ), 6, 4 );
    const auto reader = RasterReader::open( path.toStdString() );

    // Budget = w·h·bands·sizeof(double) — the documented formula.
    const RasterWindow full{ 0, 0, 6, 4 };
    CHECK( RasterReader::windowByteBudget( reader.metadata(), full, { 1 } ) == 6 * 4 * 8 );
    CHECK( RasterReader::windowByteBudget( reader.metadata(), full, { 1, 1 } ) == 6 * 4 * 2 * 8 );

    // One byte below the requirement is a typed refusal, not an allocation.
    bool threw = false;
    try
    {
        ( void ) reader.readWindow( { 1 }, full, 6 * 4 * 8 - 1 );
    }
    catch ( const GeoError &e )
    {
        threw = true;
        CHECK( e.code() == ErrorCode::Unsupported );
    }
    REQUIRE( threw );
    // Exactly the requirement succeeds.
    REQUIRE_NOTHROW( ( void ) reader.readWindow( { 1 }, full, 6 * 4 * 8 ) );
}

// ---------------------------------------------------------------------------
// Grid ops — block geometry and NoData padding (#790)
// ---------------------------------------------------------------------------

TEST_CASE( "known-answer: tiled edge blocks return full block geometry padded "
           "with NoData",
           "[known_answer8][grid]" )
{
    // 40×36 raster with 16×16 tiles (GTiff requires tile sizes that are
    // multiples of 16) → 3×3 block grid; cols 40-47 / rows 36-47 of the edge
    // blocks do not exist. readBlock must still return 16·16 values, padded
    // with the band's NoData (-9999), never truncated (#790 as closed form).
    QTemporaryDir dir;
    const auto path = writeGridRaster( dir.filePath( QStringLiteral( "tiled.tif" ) ), 40, 36, 16, 16 );
    const auto reader = RasterReader::open( path.toStdString() );
    REQUIRE( reader.isOpen() );
    const auto blockSize = reader.blockSize( 1 );
    REQUIRE( blockSize.first == 16 );
    REQUIRE( blockSize.second == 16 );

    // Interior block (0,0): rows 0-15 × cols 0-15, exact grid values.
    const auto interior = reader.readBlock( 1, 0, 0 );
    REQUIRE( interior.size() == 16 * 16 );
    CHECK_THAT( interior[0], WithinAbs( 0.0, 1e-6 ) );
    CHECK_THAT( interior[15], WithinAbs( 15.0, 1e-6 ) );
    CHECK_THAT( interior[16], WithinAbs( 10.0, 1e-6 ) );
    CHECK_THAT( interior[255], WithinAbs( 10 * 15 + 15, 1e-6 ) );

    // Right-edge block (2,0): cols 32-39 real, cols 40-47 do not exist.
    const auto rightEdge = reader.readBlock( 1, 2, 0 );
    REQUIRE( rightEdge.size() == 16 * 16 );
    CHECK_THAT( rightEdge[0], WithinAbs( 32.0, 1e-6 ) );
    CHECK_THAT( rightEdge[7], WithinAbs( 39.0, 1e-6 ) );
    for ( const int idx : { 8, 9, 14, 15 } )
        CHECK( rightEdge[idx] == -9999.0 );
    CHECK_THAT( rightEdge[16], WithinAbs( 10 * 1 + 32, 1e-6 ) );
    CHECK_THAT( rightEdge[23], WithinAbs( 10 * 1 + 39, 1e-6 ) );
    CHECK( rightEdge[24] == -9999.0 );
    CHECK( rightEdge[31] == -9999.0 );

    // Bottom-edge block (0,2): rows 32-35 real (block rows 0-3 = indices
    // 0-63), rows 36-47 beyond the raster (indices 64-255) are NoData.
    const auto bottomEdge = reader.readBlock( 1, 0, 2 );
    REQUIRE( bottomEdge.size() == 16 * 16 );
    CHECK_THAT( bottomEdge[0], WithinAbs( 10 * 32 + 0, 1e-6 ) );
    CHECK_THAT( bottomEdge[15], WithinAbs( 10 * 32 + 15, 1e-6 ) );
    CHECK_THAT( bottomEdge[16], WithinAbs( 10 * 33 + 0, 1e-6 ) );
    CHECK_THAT( bottomEdge[63], WithinAbs( 10 * 35 + 15, 1e-6 ) );
    CHECK( bottomEdge[64] == -9999.0 );
    CHECK( bottomEdge[255] == -9999.0 );
}

// ---------------------------------------------------------------------------
// Splits — largest remainder + disjointness invariants
// ---------------------------------------------------------------------------

TEST_CASE( "known-answer: random split with largest-remainder counts",
           "[known_answer8][splits]" )
{
    // N = 20, ratios 0.5/0.25/0.25 → 10/5/5 exactly (no remainder to
    // distribute). Every sample appears exactly once; roles are disjoint;
    // the same seed reproduces the same assignment bit-for-bit.
    SplitConfig config;
    config.method = SplitMethod::Random;
    config.trainRatio = 0.5;
    config.validationRatio = 0.25;
    config.testRatio = 0.25;
    config.seed = 7;

    const auto inputs = makeNumberedInputs( 20 );
    const auto first = SplitEngine::generate( config, QStringLiteral( "v1" ), inputs );
    REQUIRE( first );
    const auto &manifest = first.value();

    CHECK( roleCount( manifest, SplitRole::Train ) == 10 );
    CHECK( roleCount( manifest, SplitRole::Validation ) == 5 );
    CHECK( roleCount( manifest, SplitRole::Test ) == 5 );

    // Every sample exactly once.
    std::set<QString> ids;
    for ( const auto &a : manifest.assignments() )
        CHECK( ids.insert( a.sampleId ).second );
    CHECK( ids.size() == 20 );

    // Same seed ⇒ identical manifest assignments (determinism contract).
    const auto second = SplitEngine::generate( config, QStringLiteral( "v1" ), inputs );
    REQUIRE( second );
    CHECK( second.value().assignments() == manifest.assignments() );
}

TEST_CASE( "known-answer: remainder distribution and zero test ratio",
           "[known_answer8][splits]" )
{
    // N = 10, ratios 1/3 each: raw quotas are 10/3 = 3.33… → floor 3 each
    // with one sample left; the largest-remainder rule assigns it to TRAIN
    // (#788: remainders never fall to Test). Expected: 4/3/3.
    SplitConfig config;
    config.method = SplitMethod::Random;
    config.trainRatio = 1.0 / 3.0;
    config.validationRatio = 1.0 / 3.0;
    config.testRatio = 1.0 / 3.0;
    config.seed = 1;

    const auto result = SplitEngine::generate( config, QStringLiteral( "v1" ),
                                               makeNumberedInputs( 10 ) );
    REQUIRE( result );
    CHECK( roleCount( result.value(), SplitRole::Train ) == 4 );
    CHECK( roleCount( result.value(), SplitRole::Validation ) == 3 );
    CHECK( roleCount( result.value(), SplitRole::Test ) == 3 );

    // testRatio = 0 must yield exactly zero Test samples for any N (#788).
    SplitConfig zeroTest;
    zeroTest.method = SplitMethod::Random;
    zeroTest.trainRatio = 0.8;
    zeroTest.validationRatio = 0.2;
    zeroTest.testRatio = 0.0;
    zeroTest.seed = 42;
    const auto zero = SplitEngine::generate( zeroTest, QStringLiteral( "v1" ),
                                             makeNumberedInputs( 17 ) );
    REQUIRE( zero );
    CHECK( roleCount( zero.value(), SplitRole::Test ) == 0 );
    CHECK( roleCount( zero.value(), SplitRole::Train )
             + roleCount( zero.value(), SplitRole::Validation ) == 17 );
}

TEST_CASE( "known-answer: spatial blocks are atomic across roles",
           "[known_answer8][splits]" )
{
    // 4 clusters of 5 samples each, one per DISTINCT grid cell (blockSize
    // 100×100; centers at 25/125 so floor(center/100) = 0 or 1 per axis).
    // With spatial_block, each cell lands wholly in ONE role: for every grid
    // cell, all samples share a single role (block-to-role consistency,
    // #775/#817 as corpus form).
    QVector<SplitInput> inputs;
    int id = 0;
    for ( const auto &[cx, cy] :
          { std::pair{ 25.0, 25.0 }, std::pair{ 125.0, 25.0 },
            std::pair{ 25.0, 125.0 }, std::pair{ 125.0, 125.0 } } )
    {
        for ( int k = 0; k < 5; ++k )
        {
            SplitInput input;
            input.sampleId = QStringLiteral( "p%1" ).arg( id++ );
            input.minX = cx;
            input.maxX = cx + 0.1;
            input.minY = cy;
            input.maxY = cy + 0.1;
            input.validBounds = true;
            inputs.append( input );
        }
    }

    SplitConfig config;
    config.method = SplitMethod::SpatialBlock;
    config.trainRatio = 0.5;
    config.validationRatio = 0.25;
    config.testRatio = 0.25;
    config.seed = 3;
    config.blockSizeX = 100.0;
    config.blockSizeY = 100.0;

    const auto result = SplitEngine::generate( config, QStringLiteral( "v1" ), inputs );
    REQUIRE( result );
    const auto &manifest = result.value();
    // Largest remainder over 4 blocks at 0.5/0.25/0.25: 2/1/1 blocks →
    // 10/5/5 samples (whole-block allocation makes sample counts follow).
    CHECK( roleCount( manifest, SplitRole::Train ) == 10 );
    CHECK( roleCount( manifest, SplitRole::Validation ) == 5 );
    CHECK( roleCount( manifest, SplitRole::Test ) == 5 );
    // NO block may straddle two roles. Group by floor(x/100), floor(y/100):
    std::map<std::pair<long long, long long>, std::optional<SplitRole>> blockRole;
    for ( const auto &a : manifest.assignments() )
    {
        const auto it = std::find_if( inputs.cbegin(), inputs.cend(),
                                      [&]( const SplitInput &in ) {
                                          return in.sampleId == a.sampleId;
                                      } );
        REQUIRE( it != inputs.cend() );
        // Same cell key the engine computes (floor of center / block size;
        // positive coordinates here, so integer division == floor).
        const long long bx = static_cast<long long>( ( it->minX + 0.05 ) / 100.0 );
        const long long by = static_cast<long long>( ( it->minY + 0.05 ) / 100.0 );
        auto &role = blockRole[{ bx, by }];
        if ( !role )
            role = a.role;
        else
            CHECK( *role == a.role );
    }
    // All four quadrants must actually be present in the manifest.
    CHECK( blockRole.size() == 4 );
}
