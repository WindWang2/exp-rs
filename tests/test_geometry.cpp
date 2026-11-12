// Geometry tests — verify QgsGeometry operations
#include <catch2/catch_test_macros.hpp>

#include <qgsgeometry.h>
#include <qgspointxy.h>
#include <qgspoint.h>
#include <qgstessellator.h>
#include <qgspolygon.h>
#include <qgsmultipolygon.h>
#include <qgspolyhedralsurface.h>
#include <QSet>
#include <cmath>

TEST_CASE("Geometry from point", "[geometry]") {
  SECTION("Point geometry from QgsPointXY") {
    QgsPointXY pt(116.4, 39.9);
    QgsGeometry geom = QgsGeometry::fromPointXY(pt);
    REQUIRE_FALSE(geom.isEmpty());
    REQUIRE(geom.type() == Qgis::GeometryType::Point);
  }

  SECTION("Empty geometry") {
    QgsGeometry empty;
    REQUIRE(empty.isEmpty());
  }
}

TEST_CASE("Geometry from WKT", "[geometry]") {
  SECTION("Point WKT") {
    QgsGeometry geom = QgsGeometry::fromWkt("POINT(116.4 39.9)");
    REQUIRE_FALSE(geom.isEmpty());
    REQUIRE(geom.type() == Qgis::GeometryType::Point);
  }

  SECTION("LineString WKT") {
    QgsGeometry geom = QgsGeometry::fromWkt("LINESTRING(0 0, 1 1, 2 0)");
    REQUIRE_FALSE(geom.isEmpty());
    REQUIRE(geom.type() == Qgis::GeometryType::Line);
  }

  SECTION("Polygon WKT") {
    QgsGeometry geom = QgsGeometry::fromWkt("POLYGON((0 0, 1 0, 1 1, 0 1, 0 0))");
    REQUIRE_FALSE(geom.isEmpty());
    REQUIRE(geom.type() == Qgis::GeometryType::Polygon);
  }
}

TEST_CASE("Geometry from polyline", "[geometry]") {
  QgsPolylineXY polyline = {QgsPointXY(0, 0), QgsPointXY(1, 1), QgsPointXY(2, 0)};
  QgsGeometry geom = QgsGeometry::fromPolylineXY(polyline);
  REQUIRE_FALSE(geom.isEmpty());
  REQUIRE(geom.type() == Qgis::GeometryType::Line);
}

TEST_CASE("Geometry from polygon", "[geometry]") {
  QgsPolygonXY polygon;
  QgsPolylineXY ring = {QgsPointXY(0, 0), QgsPointXY(1, 0), QgsPointXY(1, 1), QgsPointXY(0, 1), QgsPointXY(0, 0)};
  polygon.append(ring);
  QgsGeometry geom = QgsGeometry::fromPolygonXY(polygon);
  REQUIRE_FALSE(geom.isEmpty());
  REQUIRE(geom.type() == Qgis::GeometryType::Polygon);
}

TEST_CASE( "Earcut preserves each source vertex elevation across exterior and hole rings", "[geometry][issue-1870]" )
{
  const QStringList fixtures = {
    QStringLiteral( "POLYGON Z((0 0 10,4 0 20,5 2 15,4 4 30,0 4 40,-1 2 25,0 0 10))" ),
    QStringLiteral( "POLYGON Z((0 0 10,10 0 10,10 10 10,0 10 10,0 0 10),(2 2 20,2 4 20,4 4 20,4 2 20,2 2 20))" )
  };
  for ( const QString &wkt : fixtures )
  {
    INFO( wkt.toStdString() );
    const QgsGeometry geometry = QgsGeometry::fromWkt( wkt );
    const auto *polygon = qgsgeometry_cast<const QgsPolygon *>( geometry.constGet() );
    REQUIRE( polygon != nullptr );
    QVector<QgsPoint> expected;
    auto source = geometry.vertices();
    while ( source.hasNext() )
    {
      const QgsPoint point = source.next();
      if ( !expected.contains( point ) )
        expected.append( point );
    }
    QgsTessellator tessellator;
    tessellator.setTriangulationAlgorithm( Qgis::TriangulationAlgorithm::Earcut );
    tessellator.setExtrusionFaces( Qgis::ExtrusionFace::Roof );
    tessellator.setOutputZUp( true );
    tessellator.addPolygon( *polygon, 0 );
    INFO( tessellator.error().toStdString() );
    REQUIRE( tessellator.error().isEmpty() );
    const auto triangles = tessellator.asMultiPolygon();
    REQUIRE( triangles->numGeometries() > 0 );
    QSet<int> seen;
    auto output = triangles->vertices();
    while ( output.hasNext() )
    {
      const QgsPoint actual = output.next();
      int matched = -1;
      for ( int i = 0; i < expected.size(); ++i )
      {
        if ( std::abs( actual.x() - expected[i].x() ) < 0.0001
             && std::abs( actual.y() - expected[i].y() ) < 0.0001
             && std::abs( actual.z() - expected[i].z() ) < 0.0001 )
          matched = i;
      }
      INFO( actual.asWkt().toStdString() );
      CHECK( matched >= 0 );
      if ( matched >= 0 )
        seen.insert( matched );
    }
    CHECK( seen.size() == expected.size() );

    QgsTessellator flat;
    flat.setTriangulationAlgorithm( Qgis::TriangulationAlgorithm::Earcut );
    flat.setExtrusionFaces( Qgis::ExtrusionFace::Roof );
    flat.setOutputZUp( true );
    flat.setInputZValueIgnored( true );
    flat.addPolygon( *polygon, 0 );
    REQUIRE( flat.error().isEmpty() );
    const auto flatTriangles = flat.asMultiPolygon();
    REQUIRE( flatTriangles->numGeometries() > 0 );
    auto flatOutput = flatTriangles->vertices();
    while ( flatOutput.hasNext() )
      CHECK( flatOutput.next().z() == 0 );
  }
}

TEST_CASE( "polyhedral vertex adjacency retains patch and ring identity", "[geometry][issue-1852]" )
{
  QgsPolyhedralSurface surface;
  const QgsGeometry first = QgsGeometry::fromWkt( "POLYGON((0 0,2 0,0 2,0 0))" );
  const QgsGeometry second = QgsGeometry::fromWkt( "POLYGON((10 10,20 10,25 15,20 20,10 20,10 10),(12 12,12 14,14 14,14 12,12 12))" );
  surface.addPatch( qgsgeometry_cast<const QgsPolygon *>( first.constGet() )->clone() );
  surface.addPatch( qgsgeometry_cast<const QgsPolygon *>( second.constGet() )->clone() );
  REQUIRE( surface.partCount() == 2 );
  QgsVertexId previous, next;
  surface.adjacentVertices( QgsVertexId( 1, 0, 3 ), previous, next );
  CHECK( previous == QgsVertexId( 1, 0, 2 ) );
  CHECK( next == QgsVertexId( 1, 0, 4 ) );
  surface.adjacentVertices( QgsVertexId( 1, 1, 1 ), previous, next );
  CHECK( previous == QgsVertexId( 1, 1, 0 ) );
  CHECK( next == QgsVertexId( 1, 1, 2 ) );
  for ( const QgsVertexId invalid : { QgsVertexId( -1, 0, 0 ), QgsVertexId( 2, 0, 0 ),
                                     QgsVertexId( 1, -1, 0 ), QgsVertexId( 1, 2, 0 ),
                                     QgsVertexId( 1, 0, 99 ) } )
  {
    surface.adjacentVertices( invalid, previous, next );
    CHECK_FALSE( previous.isValid() );
    CHECK_FALSE( next.isValid() );
  }
}
