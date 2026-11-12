// Geometry tests — verify QgsGeometry operations
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <qgsgeometry.h>
#include <qgspointxy.h>
#include <qgspoint.h>
#include <qgscompoundcurve.h>
#include <qgsgeometryutils.h>
#include <qgsapplication.h>
#include <qgsvectorlayer.h>
#include <qgsvectorlayereditbuffer.h>
#include <qgsvectorlayereditbuffergroup.h>
#include <QTemporaryDir>
#include <gdal.h>
#include <ogr_api.h>
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

TEST_CASE( "edit buffer group retains rejected features until a repaired commit", "[geometry][issue-1879]" )
{
  static int argc = 1;
  static char name[] = "test_geometry";
  static char *argv[] = { name, nullptr };
  if ( !QCoreApplication::instance() )
    new QgsApplication( argc, argv, false );
  REQUIRE( QgsApplication::instance() != nullptr );
  QgsApplication::initQgis();
  GDALAllRegister();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString path = dir.filePath( QStringLiteral( "edits.gpkg" ) );
  GDALDriverH driver = GDALGetDriverByName( "GPKG" );
  REQUIRE( driver != nullptr );
  GDALDatasetH dataset = GDALCreate( driver, path.toUtf8().constData(), 0, 0, 0, GDT_Unknown, nullptr );
  REQUIRE( dataset != nullptr );
  OGRLayerH table = GDALDatasetCreateLayer( dataset, "edits", nullptr, wkbPoint, nullptr );
  REQUIRE( table != nullptr );
  OGRFieldDefnH field = OGR_Fld_Create( "label_id", OFTInteger );
  OGR_Fld_SetUnique( field, true );
  REQUIRE( OGR_L_CreateField( table, field, false ) == OGRERR_NONE );
  OGR_Fld_Destroy( field );
  OGRFeatureH existing = OGR_F_Create( OGR_L_GetLayerDefn( table ) );
  OGR_F_SetFieldInteger( existing, 0, 1 );
  REQUIRE( OGR_L_CreateFeature( table, existing ) == OGRERR_NONE );
  OGR_F_Destroy( existing );
  GDALClose( dataset );

  QgsVectorLayer layer( path + QStringLiteral( "|layername=edits" ), QStringLiteral( "edits" ), QStringLiteral( "ogr" ) );
  REQUIRE( layer.isValid() );
  QgsVectorLayerEditBufferGroup group;
  group.addLayer( &layer );
  REQUIRE( group.startEditing() );
  const int column = layer.fields().indexOf( QStringLiteral( "label_id" ) );
  REQUIRE( column >= 0 );
  QgsFeature feature( layer.fields() );
  feature.setAttribute( column, 1 ); // conflicts with the stored UNIQUE value
  feature.setGeometry( QgsGeometry::fromPointXY( QgsPointXY( 1, 1 ) ) );
  REQUIRE( layer.addFeature( feature ) );
  QStringList errors;
  CHECK_FALSE( group.commitChanges( errors ) );
  CHECK_FALSE( errors.isEmpty() );
  REQUIRE( layer.isEditable() );
  REQUIRE( layer.editBuffer() != nullptr );
  CHECK( layer.editBuffer()->addedFeatures().contains( feature.id() ) );

  // The user can repair the pending edit and save it; the failed commit
  // must not have cleared the undo/edit state or reported success.
  REQUIRE( layer.changeAttributeValue( feature.id(), column, 2 ) );
  errors.clear();
  REQUIRE( group.commitChanges( errors ) );
  CHECK( layer.featureCount() == 2 );
  CHECK_FALSE( layer.isEditable() );
}

TEST_CASE( "closed compound chamfer and fillet retain every intermediate junction", "[geometry][issue-1849]" )
{
  const QgsGeometry input = QgsGeometry::fromWkt( "COMPOUNDCURVE((0 0,5 0),(5 0,5 5),(5 5,0 5,0 0))" );
  const auto *curve = qgsgeometry_cast<const QgsCompoundCurve *>( input.constGet() );
  REQUIRE( curve != nullptr );
  REQUIRE( curve->isClosed() );
  REQUIRE( curve->nCurves() == 3 );
  for ( const int vertex : { 0, curve->numPoints() - 1 } )
  {
    for ( const bool fillet : { false, true } )
    {
      INFO( "vertex: " << vertex << ", fillet: " << fillet );
      auto result = fillet ? QgsGeometryUtils::filletVertex( curve, vertex, 1, 8 )
                           : QgsGeometryUtils::chamferVertex( curve, vertex, 1, 1 );
      REQUIRE( result != nullptr );
      const auto *compound = qgsgeometry_cast<const QgsCompoundCurve *>( result.get() );
      REQUIRE( compound != nullptr );
      CHECK( compound->isClosed() );
      REQUIRE( compound->nCurves() >= 3 );
      for ( int i = 1; i < compound->nCurves(); ++i )
      {
        INFO( "junction " << i );
        CHECK( compound->curveAt( i - 1 )->endPoint() == compound->curveAt( i )->startPoint() );
      }
      if ( !fillet )
      {
        const QgsGeometry chamfered( std::move( result ) );
        CHECK( chamfered.length() == Catch::Approx( 18 + std::sqrt( 2.0 ) ) );
      }
    }
  }
}
