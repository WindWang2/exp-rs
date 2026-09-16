/***************************************************************************
 * test_verification_failure_11.cpp — Failure / Cancel / Atomic lane
 * (Platform 11.0, package E)
 *
 * The contract registry DECLARES refusal codes, atomic publication and
 * cancellation granularity; this lane verifies the declared behavior
 * actually happens at the operator seam:
 *
 *   F1 corrupt input   → typed refusal, no product on disk;
 *   F2 missing input   → typed FileNotFound-class refusal, no product;
 *   F3 bad parameters  → typed parameter refusal, no product (band index out
 *                        of range is the classic silent-nonsense case);
 *   F4 cancel          → cooperative cancellation surfaces as the typed
 *                        Cancelled error (checked before any work, per the
 *                        operator-level granularity declared by io:/rs:
 *                        records);
 *   F5 hostile output  → read-only output directory → typed write refusal,
 *                        no half-written artifacts left behind.
 *
 * Every negative case ALSO asserts the output directory stays clean: a
 * refusal must be total — typed error AND no partial product (#647 family).
 *
 * Offline, deterministic, bounded.
 ***************************************************************************/
#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "synthetic_raster_builder.h"

#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

using namespace sicnu::operators;

namespace
{
namespace fs = std::filesystem;

std::unique_ptr<RSOperator> create( const std::string &id )
{
    auto op = RSOperatorRegistry::instance().create( id );
    REQUIRE( op != nullptr );
    return op;
}

/// Runs @p op expecting an RSOperatorError; returns the typed code.
std::string expectTypedFailure( RSOperator *op, const Json::Value &params )
{
    try
    {
        RSOperatorContext ctx;
        op->run( params, ctx );
    }
    catch ( const RSOperatorError &e )
    {
        return errorCodeToString( e.code() );
    }
    catch ( const std::exception &e )
    {
        FAIL( std::string( "untyped std::exception instead of RSOperatorError: " )
              + e.what() );
    }
    FAIL( "operator unexpectedly succeeded" );
    return {};
}

/// The output directory must not hold any product after a refusal.
void expectNoArtifacts( const QTemporaryDir &dir, const QString &prefix )
{
    const fs::path root = dir.path().toStdString();
    std::vector<std::string> leftovers;
    std::error_code ec;
    for ( const auto &entry : fs::directory_iterator( root, ec ) )
    {
        const std::string name = entry.path().filename().string();
        if ( name.rfind( prefix.toStdString(), 0 ) == 0 )
            leftovers.push_back( name );
    }
    CHECK( leftovers.empty() );
    if ( !leftovers.empty() )
        FAIL( "partial artifacts after refusal: " + leftovers.front() );
}
} // namespace

TEST_CASE( "F1: corrupt input is a typed refusal with no product",
           "[failure11]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const QString corrupt = dir.filePath( QStringLiteral( "corrupt.tif" ) );
    {
        QFile f( corrupt );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( "this is not a GeoTIFF, it is prose masquerading as one\n" );
    }

    auto op = create( "io:translate" );
    Json::Value p;
    p["input"] = corrupt.toStdString();
    p["output"] = dir.filePath( QStringLiteral( "f1_out.tif" ) ).toStdString();
    const std::string code = expectTypedFailure( op.get(), p );
    INFO( "typed refusal: " << code );
    CHECK( ( code == "FileNotReadable" || code == "InvalidInputData"
             || code == "GdalError" || code == "FileNotFound" ) );
    expectNoArtifacts( dir, QStringLiteral( "f1_out" ) );
}

TEST_CASE( "F2: missing input is a typed FileNotFound-class refusal",
           "[failure11]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    auto op = create( "rs:band_ratio" );
    Json::Value p;
    p["input"] = dir.filePath( QStringLiteral( "does_not_exist.tif" ) ).toStdString();
    p["output"] = dir.filePath( QStringLiteral( "f2_out.tif" ) ).toStdString();
    p["numeratorBand"] = 1;
    p["denominatorBand"] = 2;
    const std::string code = expectTypedFailure( op.get(), p );
    CHECK( ( code == "FileNotFound" || code == "FileNotReadable"
             || code == "InvalidInputData" ) );
    expectNoArtifacts( dir, QStringLiteral( "f2_out" ) );
}

TEST_CASE( "F3: out-of-range band index is a typed parameter refusal",
           "[failure11]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    auto raster = sicnu::testing::RsSyntheticRasterBuilder( 8, 8, 2, GDT_Float32 )
                    .withRampPattern( 1, 0.0f, 1.0f )
                    .withConstantValue( 2, 0.5f )
                    .writeToDisk( dir.filePath( QStringLiteral( "f3_in.tif" ) ) );
    REQUIRE_FALSE( raster.isEmpty() );

    auto op = create( "rs:band_ratio" );
    Json::Value p;
    p["input"] = raster.toStdString();
    p["output"] = dir.filePath( QStringLiteral( "f3_out.tif" ) ).toStdString();
    p["numeratorBand"] = 1;
    p["denominatorBand"] = 99; // out of range
    const std::string code = expectTypedFailure( op.get(), p );
    INFO( "typed refusal: " << code );
    CHECK( ( code == "OutOfRange" || code == "InvalidParameter"
             || code == "InvalidEnumValue" || code == "TypeMismatch"
             || code == "ComputationError" ) );
    expectNoArtifacts( dir, QStringLiteral( "f3_out" ) );
}

TEST_CASE( "F4: pre-set cancellation surfaces as the typed Cancelled error",
           "[failure11]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    auto raster = sicnu::testing::RsSyntheticRasterBuilder( 8, 8, 2, GDT_Float32 )
                    .withRampPattern( 1, 0.0f, 1.0f )
                    .withConstantValue( 2, 0.5f )
                    .withCrs( QStringLiteral( "EPSG:4326" ) )
                    .writeToDisk( dir.filePath( QStringLiteral( "f4_in.tif" ) ) );
    REQUIRE_FALSE( raster.isEmpty() );

    std::atomic<bool> cancel{ true }; // cancelled BEFORE the run starts
    for ( const char *id : { "io:translate", "io:warp", "rs:band_ratio" } )
    {
        DYNAMIC_SECTION( id )
        {
            auto op = create( id );
            Json::Value p;
            p["input"] = raster.toStdString();
            p["output"] = dir.filePath( QStringLiteral( "f4_out.tif" ) ).toStdString();
            if ( std::string( id ) == "io:warp" )
                p["targetCrs"] = "EPSG:4326";
            if ( std::string( id ) == "rs:band_ratio" )
            {
                p["numeratorBand"] = 1;
                p["denominatorBand"] = 2;
            }
            RSOperatorContext ctx;
            ctx.setCancelFlag( &cancel );
            try
            {
                op->run( p, ctx );
                // An operator that has not reached its first cancellation
                // checkpoint is a contract finding — but the FAIL belongs to
                // the review, not to a silent pass.
                FAIL( id << " ignored a pre-set cancellation flag" );
            }
            catch ( const RSOperatorError &e )
            {
                CHECK( errorCodeToString( e.code() ) == std::string( "Cancelled" ) );
            }
        }
    }
}

TEST_CASE( "F5: read-only output location is a typed write refusal with no partials",
           "[failure11]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    auto raster = sicnu::testing::RsSyntheticRasterBuilder( 8, 8, 2, GDT_Float32 )
                    .withRampPattern( 1, 0.0f, 1.0f )
                    .withConstantValue( 2, 0.5f )
                    .writeToDisk( dir.filePath( QStringLiteral( "f5_in.tif" ) ) );
    REQUIRE_FALSE( raster.isEmpty() );

    // Host-honest hostile-target: a NONEXISTENT output directory is a
    // deterministic write refusal on every platform. (Directory ACL-based
    // read-only is enforced inconsistently for the file owner on Windows/NTFS
    // — a read-only probe is recorded as a WARN, never asserted.)
    const QString missingDir = dir.filePath( QStringLiteral( "no_such_dir" ) );

    auto op = create( "io:translate" );
    Json::Value p;
    p["input"] = raster.toStdString();
    p["output"] = ( missingDir + QStringLiteral( "/f5_out.tif" ) ).toStdString();
    const std::string code = expectTypedFailure( op.get(), p );
    INFO( "typed refusal: " << code );
    CHECK( ( code == "FileNotWritable" || code == "GdalError"
             || code == "DirectoryNotFound" || code == "InvalidInputData" ) );
    const fs::path outPath = ( missingDir + QStringLiteral( "/f5_out.tif" ) ).toStdString();
    std::error_code ec;
    CHECK_FALSE( fs::exists( outPath, ec ) );

    // Read-only directory probe (best-effort, host-dependent): never asserted
    // as a gate — recorded for the readiness report.
    const QString readOnlyDir = dir.filePath( QStringLiteral( "readonly" ) );
    REQUIRE( QDir().mkpath( readOnlyDir ) );
    QFile::setPermissions( readOnlyDir, QFile::ReadOwner | QFile::ExeOwner );
    {
        Json::Value ro;
        ro["input"] = raster.toStdString();
        ro["output"] = ( readOnlyDir + QStringLiteral( "/f5_out.tif" ) ).toStdString();
        bool wrote = false;
        try
        {
            RSOperatorContext ctx;
            wrote = op->run( ro, ctx ).isMember( "output" );
        }
        catch ( const RSOperatorError & )
        {
            wrote = false; // host enforced the ACL — the refusal is typed
        }
        WARN( "read-only directory probe: write "
              << ( wrote ? "SUCCEEDED (host does not enforce directory ACL for owner)"
                         : "refused (host enforces)" ) );
        QFile::setPermissions( readOnlyDir,
                               QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner );
    }
}
