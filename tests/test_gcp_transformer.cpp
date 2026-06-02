// tests/test_gcp_transformer.cpp — TDD for QgsGcpTransformerInterface
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "qgsgcptransformer.h"
#include "qgspointxy.h"

#include <QVector>
#include <memory>

using Catch::Approx;
using TM = QgsGcpTransformerInterface::TransformMethod;

namespace {
struct GcpPair {
    double sx, sy, dx, dy;
};

QVector<QgsPointXY> sources( const std::vector<GcpPair> &g ) {
    QVector<QgsPointXY> r;
    r.reserve( static_cast<int>( g.size() ) );
    for ( const auto &p : g )
        r.emplace_back( p.sx, p.sy );
    return r;
}

QVector<QgsPointXY> destinations( const std::vector<GcpPair> &g ) {
    QVector<QgsPointXY> r;
    r.reserve( static_cast<int>( g.size() ) );
    for ( const auto &p : g )
        r.emplace_back( p.dx, p.dy );
    return r;
}

int minimumGcpCountFor( TM method ) {
    std::unique_ptr<QgsGcpTransformerInterface> t( QgsGcpTransformerInterface::create( method ) );
    REQUIRE( t != nullptr );
    return t->minimumGcpCount();
}
} // namespace

TEST_CASE( "minimumGcpCount per method", "[georef][transformer]" ) {
    REQUIRE( minimumGcpCountFor( TM::Linear ) == 2 );
    REQUIRE( minimumGcpCountFor( TM::Helmert ) == 2 );
    REQUIRE( minimumGcpCountFor( TM::PolynomialOrder1 ) == 3 );
    REQUIRE( minimumGcpCountFor( TM::PolynomialOrder2 ) == 6 );
    REQUIRE( minimumGcpCountFor( TM::PolynomialOrder3 ) == 10 );
    REQUIRE( minimumGcpCountFor( TM::ThinPlateSpline ) == 1 );
    REQUIRE( minimumGcpCountFor( TM::Projective ) == 4 );
}

TEST_CASE( "PolynomialOrder1: pure translation roundtrip", "[georef][transformer]" ) {
    // Source -> destination with +100,+200 offset
    std::vector<GcpPair> g = {
        { 0, 0, 100, 200 },
        { 10, 0, 110, 200 },
        { 0, 10, 100, 210 }
    };
    std::unique_ptr<QgsGcpTransformerInterface> t(
        QgsGcpTransformerInterface::createFromParameters( TM::PolynomialOrder1, sources( g ), destinations( g ) ) );
    REQUIRE( t != nullptr );

    double x = 5.0;
    double y = 5.0;
    REQUIRE( t->transform( x, y, false ) );
    REQUIRE( x == Approx( 105.0 ).margin( 1e-6 ) );
    REQUIRE( y == Approx( 205.0 ).margin( 1e-6 ) );
}

TEST_CASE( "Linear transform: pure translation", "[georef][transformer]" ) {
    // Linear (scale + translation) — exercise the codepath that does not require GSL
    std::vector<GcpPair> g = {
        { 0, 0, 100, 200 },
        { 10, 0, 110, 200 },
        { 0, 10, 100, 210 },
        { 10, 10, 110, 210 }
    };
    std::unique_ptr<QgsGcpTransformerInterface> t(
        QgsGcpTransformerInterface::createFromParameters( TM::Linear, sources( g ), destinations( g ) ) );
    REQUIRE( t != nullptr );

    double x = 5.0;
    double y = 5.0;
    REQUIRE( t->transform( x, y, false ) );
    REQUIRE( x == Approx( 105.0 ).margin( 1e-6 ) );
    REQUIRE( y == Approx( 205.0 ).margin( 1e-6 ) );
}
