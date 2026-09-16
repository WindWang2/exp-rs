// tests/test_fusion_quality_report.cpp — F15 Package F oracle tests.
//
// Independent truths: identical bands give Q=1, ratios 1, ERGAS=0, RASE=0;
// a constant additive distortion shifts the mean ratio by offset/mean; a
// linear contrast stretch scales the std ratio — all hand-derivable.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processing/algorithms/fusion_quality_report.h"

#include <json/json.h>

#include <QDir>
#include <QFile>

#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

using namespace rs::fusion;
using Catch::Approx;

namespace {

struct Bands
{
    std::vector<std::vector<float>> storage;
    std::vector<const float *> ptrs;

    const float *add( const std::vector<float> &v )
    {
        storage.push_back( v );
        ptrs.push_back( storage.back().data() );
        return storage.back().data();
    }
};

std::vector<float> ramp( int n, double a, double b )
{
    std::vector<float> v( static_cast<size_t>( n ) );
    for ( int i = 0; i < n; ++i )
        v[static_cast<size_t>( i )] = static_cast<float>( a + ( b - a ) * i / std::max( 1, n - 1 ) );
    return v;
}

Json::Value parse( const std::string &json )
{
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errs;
    std::istringstream stream( json );
    REQUIRE( Json::parseFromStream( builder, stream, &root, &errs ) );
    return root;
}

} // namespace

TEST_CASE( "FusionQuality: identical bands pass with unit indices",
           "[processing][mosaic][fusion-quality]" )
{
    const int n = 64;
    Bands ref;
    ref.add( ramp( n, 0.0, 100.0 ) );
    Bands fus;
    fus.add( ramp( n, 0.0, 100.0 ) );

    FusionQualityThresholds t;
    const auto report = evaluateFusionQuality( fus.ptrs, ref.ptrs, n, 1, 1.0, t );
    CHECK( report.qIndex == Approx( 1.0 ).margin( 1e-9 ) );
    CHECK( report.ergas == Approx( 0.0 ).margin( 1e-9 ) );
    CHECK( report.rase == Approx( 0.0 ).margin( 1e-9 ) );
    CHECK( report.meanRatio[0] == Approx( 1.0 ).margin( 1e-9 ) );
    CHECK( report.stdRatio[0] == Approx( 1.0 ).margin( 1e-9 ) );
    CHECK( report.meanCc == Approx( 1.0 ).margin( 1e-9 ) );
    CHECK( report.passed );
    CHECK( report.violations.empty() );
}

TEST_CASE( "FusionQuality: additive distortion shifts the mean ratio and fails the guard",
           "[processing][mosaic][fusion-quality]" )
{
    const int n = 128;
    Bands ref;
    const auto r = ramp( n, 20.0, 120.0 );
    ref.add( r );
    Bands fus;
    fus.add( ramp( n, 25.0, 125.0 ) ); // constant +5 distortion

    const auto report = evaluateFusionQuality( fus.ptrs, ref.ptrs, n, 1, 1.0 );
    // mean(degraded) = mean(ref) + 5 -> ratio = 1 + 5/70.
    CHECK( report.meanRatio[0] == Approx( 1.0 + 5.0 / 70.0 ).margin( 1e-6 ) );
    // Std identical under a pure offset.
    CHECK( report.stdRatio[0] == Approx( 1.0 ).margin( 1e-6 ) );
    CHECK_FALSE( report.passed );
    REQUIRE_FALSE( report.violations.empty() );
    bool hasMeanRatioViolation = false;
    for ( const std::string &v : report.violations )
        if ( v.find( "mean ratio" ) != std::string::npos )
            hasMeanRatioViolation = true;
    CHECK( hasMeanRatioViolation );
}

TEST_CASE( "FusionQuality: contrast stretch scales the std ratio",
           "[processing][mosaic][fusion-quality]" )
{
    const int n = 256;
    Bands ref;
    ref.add( ramp( n, 0.0, 80.0 ) );
    Bands fus;
    fus.add( ramp( n, 0.0, 96.0 ) ); // ×1.2 stretch -> std ratio 1.2

    const auto report = evaluateFusionQuality( fus.ptrs, ref.ptrs, n, 1, 1.0 );
    CHECK( report.stdRatio[0] == Approx( 1.2 ).margin( 1e-6 ) );
    CHECK( report.meanRatio[0] == Approx( 1.2 ).margin( 1e-6 ) );
    CHECK_FALSE( report.passed );
}

TEST_CASE( "FusionQuality: degenerate inputs produce violations, not exceptions",
           "[processing][mosaic][fusion-quality]" )
{
    Bands a, b;
    a.add( ramp( 16, 0, 1 ) );
    b.add( ramp( 16, 0, 1 ) );
    b.add( ramp( 16, 0, 1 ) );
    auto report = evaluateFusionQuality( a.ptrs, b.ptrs, 16, 1, 1.0 );
    CHECK_FALSE( report.passed );
    CHECK( report.violations[0].find( "band count" ) != std::string::npos );

    Bands empty;
    report = evaluateFusionQuality( {}, empty.ptrs, 16, 1, 1.0 );
    CHECK_FALSE( report.passed );
    CHECK( report.violations[0].find( "no bands" ) != std::string::npos );

    report = evaluateFusionQuality( a.ptrs, a.ptrs, 0, 10, 1.0 );
    CHECK_FALSE( report.passed );
    CHECK( report.violations[0].find( "dimensions" ) != std::string::npos );
}

TEST_CASE( "FusionQuality: Q index is sensitive to structural change",
           "[processing][mosaic][fusion-quality]" )
{
    const int n = 128;
    Bands ref;
    ref.add( ramp( n, 0.0, 50.0 ) );
    Bands fus;
    // Half-amplitude copy: correlated but not identical -> 0 < Q < 1.
    const auto half = ramp( n, 0.0, 25.0 );
    fus.add( half );
    const auto report = evaluateFusionQuality( fus.ptrs, ref.ptrs, n, 1, 1.0 );
    CHECK( report.qIndex > 0.1 );
    CHECK( report.qIndex < 0.999 );
}

TEST_CASE( "FusionQuality: JSON artifact has fixed schema and round-trips",
           "[processing][mosaic][fusion-quality]" )
{
    const int n = 64;
    Bands ref;
    ref.add( ramp( n, 0.0, 100.0 ) );
    Bands fus;
    fus.add( ramp( n, 0.0, 100.0 ) );
    const auto report = evaluateFusionQuality( fus.ptrs, ref.ptrs, n, 1, 1.0 );

    const std::string json = report.toJson();
    const Json::Value root = parse( json );
    CHECK( root["schema"].asString() == "exp-rs/fusion-quality-report@1" );
    CHECK( root["wald"].isMember( "ergas" ) );
    CHECK( root["indices"].isMember( "q" ) );
    CHECK( root["spectralDistortion"]["meanRatio"].size() == 1u );
    CHECK( root["verdict"]["passed"].asBool() );

    // Field order is deterministic: two serializations are byte-identical.
    CHECK( json == report.toJson() );
}

TEST_CASE( "FusionQuality: atomic report write never leaves a truncated file",
           "[processing][mosaic][fusion-quality]" )
{
    const QString dir = QDir::tempPath() + QStringLiteral( "/exp_rs_fqr_test" );
    REQUIRE( QDir().mkpath( dir ) );
    const std::string path = ( dir + QStringLiteral( "/report.json" ) ).toStdString();

    std::string err;
    REQUIRE( writeTextFileAtomic( path, "{\"a\":1}", &err ) );
    {
        QFile f( QString::fromStdString( path ) );
        REQUIRE( f.open( QIODevice::ReadOnly ) );
        CHECK( f.readAll() == QByteArray( "{\"a\":1}" ) );
        f.close();
    }

    // Overwrite with longer content: readers must see the full new file.
    REQUIRE( writeTextFileAtomic( path, std::string( 5000, 'x' ), &err ) );
    {
        QFile f( QString::fromStdString( path ) );
        REQUIRE( f.open( QIODevice::ReadOnly ) );
        CHECK( f.size() == 5000 );
        CHECK( f.read( 4 ) == QByteArray( "xxxx" ) );
        f.close();
    }

    // Unwritable directory fails with a message.
    CHECK_FALSE( writeTextFileAtomic( "/nonexistent-dir-xyz/report.json", "{}", &err ) );
    CHECK_FALSE( err.empty() );
}
