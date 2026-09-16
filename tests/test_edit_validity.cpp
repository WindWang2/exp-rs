// test_edit_validity.cpp — F11 Package C: validity report + repair preview.
//
// Oracle: hand-built bow-tie polygon (known self-intersection) and a known
// valid square. The repair candidate is checked against the original input
// (input never mutated; candidate valid; area preserved by the split).
#include <catch2/catch_test_macros.hpp>

#include "editing/rs_geometry_validity.h"

#include <qgsgeometry.h>

namespace
{

/// Bow-tie: (0,0) (10,10) (10,0) (0,10) — edges cross in the middle.
QgsGeometry bowTie()
{
    return QgsGeometry::fromPolygonXY( { QVector<QgsPointXY>{
      QgsPointXY( 0, 0 ), QgsPointXY( 10, 10 ), QgsPointXY( 10, 0 ),
      QgsPointXY( 0, 10 ), QgsPointXY( 0, 0 ) } } );
}

QgsGeometry square()
{
    return QgsGeometry::fromPolygonXY( { QVector<QgsPointXY>{
      QgsPointXY( 0, 0 ), QgsPointXY( 10, 0 ), QgsPointXY( 10, 10 ),
      QgsPointXY( 0, 10 ), QgsPointXY( 0, 0 ) } } );
}

} // namespace

TEST_CASE( "valid square reports zero issues", "[editing][validity][f11]" )
{
    const QVector<RsValidityIssue> issues = RsGeometryValidity::validate( square() );
    CHECK( issues.isEmpty() );
    CHECK( RsGeometryValidity::isValid( square() ) );
}

TEST_CASE( "bow-tie polygon reports a self-intersection issue",
           "[editing][validity][f11][oracle2]" )
{
    const QgsGeometry geom = bowTie();
    const QVector<RsValidityIssue> issues = RsGeometryValidity::validate( geom );
    REQUIRE_FALSE( issues.isEmpty() );
    CHECK_FALSE( issues.first().message.isEmpty() );
    CHECK( RsGeometryValidity::isValid( geom ) == false );
}

TEST_CASE( "null geometry is reported, never silently accepted",
           "[editing][validity][f11][negative]" )
{
    const QVector<RsValidityIssue> issues = RsGeometryValidity::validate( QgsGeometry() );
    REQUIRE( issues.size() == 1 );
    CHECK( issues.first().message.contains( QStringLiteral( "null" ) ) );
}

TEST_CASE( "repairPreview returns a valid candidate without mutating input",
           "[editing][validity][f11][oracle2]" )
{
    const QgsGeometry original = bowTie();

    bool ok = false;
    const QgsGeometry candidate = RsGeometryValidity::repairPreview( original, false, &ok );
    REQUIRE( ok );

    // Input untouched: still the same bow-tie (still invalid).
    CHECK_FALSE( RsGeometryValidity::isValid( original ) );

    // makeValid splits the figure-eight ring into its two lobes
    // (each 25 m² by shoelace on the hand-written ring) — the repaired
    // area is the hand-known 50 m², independent of the invalid input's
    // meaningless signed area.
    CHECK( RsGeometryValidity::isValid( candidate ) );
    CHECK( candidate.area() == 50.0 );
}

TEST_CASE( "repairPreview on an invalid null geometry fails closed",
           "[editing][validity][f11][negative]" )
{
    bool ok = true;
    const QgsGeometry candidate = RsGeometryValidity::repairPreview( QgsGeometry(), false, &ok );
    CHECK_FALSE( ok );
    CHECK( candidate.isNull() );
}

TEST_CASE( "repairPreview of an already-valid geometry succeeds",
           "[editing][validity][f11]" )
{
    bool ok = false;
    const QgsGeometry candidate = RsGeometryValidity::repairPreview( square(), false, &ok );
    REQUIRE( ok );
    CHECK( RsGeometryValidity::isValid( candidate ) );
    CHECK( candidate.area() == square().area() );
}
