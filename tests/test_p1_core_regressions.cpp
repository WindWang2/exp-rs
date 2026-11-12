#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "qgsapplication.h"
#include "qgscircle.h"
#include "qgsdistancearea.h"
#include "qgsexpression.h"
#include "qgsvectorlayer.h"
#include "qgsmaplayerserverproperties.h"
#include "qgsrunprocess.h"
#include "qgsproject.h"
#include "qgslayout.h"
#include "qgslayoutitemchart.h"
#include "qgslayoutexporter.h"
#include "qgslinechartplot.h"
#include "qgsmessageoutput.h"
#include <QEventLoop>
#include <QTimer>
#include <QPointer>
#include "qgscoordinatetransformcontext.h"
#ifdef P1_TEST_EPT
#include "qgseptpointcloudindex.h"
#endif
#include "qgspointcloudstatistics.h"
#include "qgsmultibandcolorrenderer.h"
#include "qgscontrastenhancement.h"
#include "qgsrasterblock.h"
#include "qgsrasterlayer.h"
#include "qgsrasterfilewriter.h"
#include <gdal.h>
#include <QDir>
#include "qgsvectortilemvtdecoder.h"
#include "qgsvectortileloader.h"
#include "qgsquantizedmeshdataprovider.h"
#include "qgsdatasourceuri.h"
#include "qgsstacparser.h"
#include "qgsstaccatalog.h"
#include "qgsmapboxglstyleconverter.h"
#include "qgsogcutils.h"
#include "qgsgeometry.h"
#include "qgserror.h"
#include <QFile>
#include <QTemporaryDir>
#include <QUrl>
#include <memory>

namespace {
void app()
{
  static int argc = 1;
  static char name[] = "test_p1_core_regressions";
  static char *argv[] = { name, nullptr };
  static auto *application = new QgsApplication( argc, argv, false );
  (void)application;
}
#ifdef P1_TEST_EPT
class ExtentIndex : public QgsEptPointCloudIndex
{
public:
  ExtentIndex() { mExtent = QgsRectangle( 10, 20, 30, 80 ); mZMin = -5; mZMax = 15; }
};
#endif
class ThreeBands : public QgsRasterInterface
{
public:
  double values[3] = { 50, 50, 50 };
  QgsRasterInterface *clone() const override
  {
    auto *copy = new ThreeBands;
    for ( int i = 0; i < 3; ++i ) copy->values[i] = values[i];
    return copy;
  }
  int bandCount() const override { return 3; }
  Qgis::DataType dataType( int ) const override { return Qgis::DataType::Float64; }
  QgsRasterBlock *block( int band, const QgsRectangle &, int w, int h, QgsRasterBlockFeedback * = nullptr ) override
  {
    auto *result = new QgsRasterBlock( Qgis::DataType::Float64, w, h );
    for ( int i = 0; i < w * h; ++i ) result->setValue( i, values[band-1] );
    return result;
  }
};
}

TEST_CASE( "Circle extent checks use independent coordinate axes", "[p1][core][circle]" )
{
  const QgsCircle circle = QgsCircle::fromExtent( QgsPoint( 100, 200 ), QgsPoint( 110, 210 ) );
  CHECK( circle.radius() == Catch::Approx( 5 ) );
  CHECK( circle.center().x() == Catch::Approx( 105 ) );
  CHECK( circle.center().y() == Catch::Approx( 205 ) );
  CHECK( QgsCircle::fromExtent( QgsPoint( 100, 200 ), QgsPoint( 110, 110 ) ).isEmpty() );
}

TEST_CASE( "Projected line distance uses the projected endpoint in feet", "[p1][core][distance]" )
{
  app();
  QgsCoordinateReferenceSystem crs = QgsCoordinateReferenceSystem::fromProj(
    "+proj=tmerc +lat_0=0 +lon_0=0 +k=1 +x_0=0 +y_0=0 +datum=WGS84 +units=ft +no_defs" );
  REQUIRE( crs.isValid() );
  QgsDistanceArea distance;
  distance.setSourceCrs( crs, QgsCoordinateTransformContext() );
  distance.setEllipsoid( "NONE" );
  const QgsPointXY start( 1234, 5678 );
  QgsPointXY end;
  CHECK( distance.measureLineProjected( start, 30.48, 0, &end ) == Catch::Approx( 100 ) );
  CHECK( start.distance( end ) == Catch::Approx( 100 ) );
}

#ifdef P1_TEST_EPT
TEST_CASE( "Point cloud fallback statistics retain the Y extent", "[p1][core][pointcloud]" )
{
  app();
  ExtentIndex index;
  const auto stats = index.QgsAbstractPointCloudIndex::metadataStatistics();
  CHECK( stats.statisticsOf( "X" ).minimum == 10 );
  CHECK( stats.statisticsOf( "X" ).maximum == 30 );
  CHECK( stats.statisticsOf( "Y" ).minimum == 20 );
  CHECK( stats.statisticsOf( "Y" ).maximum == 80 );
  CHECK( stats.statisticsOf( "Z" ).minimum == -5 );
  CHECK( stats.statisticsOf( "Z" ).maximum == 15 );
}

#endif

TEST_CASE( "Multi-source vector tile enumeration respects each source size", "[p1][core][vectortile]" )
{
  QgsVectorTileMatrixSet structure;
  QgsVectorTileMVTDecoder decoder( structure );
  QMap<QString, QByteArray> sources;
  for ( int source = 0; source < 2; ++source )
  {
    vector_tile::Tile tile;
    for ( int i = 0; i <= source; ++i )
    {
      auto *layer = tile.add_layers();
      layer->set_version( 2 );
      layer->set_name( std::string( "layer" ) + std::to_string( source ) + std::to_string( i ) );
    }
    const std::string bytes = tile.SerializeAsString();
    sources[QString::number( source )] = QByteArray( bytes.data(), bytes.size() );
  }
  REQUIRE( decoder.decode( QgsVectorTileRawData( QgsTileXYZ( 0, 0, 0 ), sources ) ) );
  CHECK( decoder.layers() == QStringList{ "layer00", "layer10", "layer11" } );
}

TEST_CASE( "RGB contrast clipping checks the value of each channel", "[p1][core][raster]" )
{
  ThreeBands input;
  QgsMultiBandColorRenderer renderer( &input, 1, 2, 3 );
  for ( int band = 1; band <= 3; ++band )
  {
    auto *contrast = new QgsContrastEnhancement( Qgis::DataType::Float64 );
    contrast->setMinimumValue( 0 );
    contrast->setMaximumValue( 100 );
    contrast->setContrastEnhancementAlgorithm( QgsContrastEnhancement::StretchAndClipToMinimumMaximum );
    if ( band == 1 ) renderer.setRedContrastEnhancement( contrast );
    if ( band == 2 ) renderer.setGreenContrastEnhancement( contrast );
    if ( band == 3 ) renderer.setBlueContrastEnhancement( contrast );
  }
  for ( int clipped : { 0, 1, 2, 3 } )
  {
    for ( int i = 0; i < 3; ++i ) input.values[i] = clipped == i + 1 ? 200 : 50;
    std::unique_ptr<QgsRasterBlock> block( renderer.block( 1, QgsRectangle( 0, 0, 1, 1 ), 1, 1 ) );
    REQUIRE( block );
    CHECK( qAlpha( block->color( 0 ) ) == ( clipped ? 0 : 255 ) );
  }
}

TEST_CASE( "Missing quantized mesh fields return a stable diagnostic", "[p1][core][mesh]" )
{
  app();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  QFile file( dir.filePath( "layer.json" ) );
  REQUIRE( file.open( QIODevice::WriteOnly ) );
  REQUIRE( file.write( "{}" ) == 2 );
  file.close();
  QgsDataSourceUri uri;
  uri.setParam( "url", QUrl::fromLocalFile( file.fileName() ).toString() );
  QgsError error;
  QgsQuantizedMeshMetadata metadata( QString::fromUtf8( uri.encodedUri() ), QgsCoordinateTransformContext(), error );
  CHECK( error.summary().contains( "Missing field: format" ) );
}


TEST_CASE( "Rendered raster export accepts valid tile providers", "[p1][core][raster][export]" )
{
  app();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  GDALAllRegister();
  const QString source = dir.filePath( "source.tif" );
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver );
  GDALDatasetH ds = GDALCreate( driver, source.toUtf8().constData(), 2, 2, 3, GDT_Byte, nullptr );
  REQUIRE( ds );
  double transform[] = { 0, 1, 0, 2, 0, -1 };
  GDALSetGeoTransform( ds, transform );
  for ( int band = 1; band <= 3; ++band )
  {
    unsigned char values[4] = { 50, 50, 50, 50 };
    REQUIRE( GDALRasterIO( GDALGetRasterBand( ds, band ), GF_Write, 0, 0, 2, 2,
                          values, 2, 2, GDT_Byte, 0, 0 ) == CE_None );
  }
  GDALClose( ds );
  QgsRasterLayer layer( source, "source", "gdal" );
  REQUIRE( layer.isValid() );
  layer.setRenderer( new QgsMultiBandColorRenderer( layer.dataProvider(), 1, 2, 3 ) );
  const QString output = dir.filePath( "tiles" );
  QgsRasterFileWriter writer( output );
  writer.setOutputFormat( "GTiff" );
  writer.setTiledMode( true );
  writer.setMaxTileWidth( 1 );
  writer.setMaxTileHeight( 1 );
  CHECK( writer.writeRaster( layer.pipe(), 2, 2, layer.extent(), layer.crs(),
                            QgsCoordinateTransformContext() ) == Qgis::RasterFileWriterResult::Success );
  const QStringList tiles = QDir( output ).entryList( { "*.tif" }, QDir::Files );
  REQUIRE( tiles.size() == 4 );
  for ( const QString &tile : tiles )
  {
    GDALDatasetH result = GDALOpen( QDir( output ).filePath( tile ).toUtf8().constData(), GA_ReadOnly );
    REQUIRE( result );
    CHECK( GDALGetRasterCount( result ) == 4 );
    GDALClose( result );
  }
}



TEST_CASE( "STAC catalogs accept the minimum stable version", "[core_safety]" )
{
  for ( const char *version : { "1.0.0", "1.0.1", "1.1.0" } )
  {
    QgsStacParser parser;
    parser.setData( QString( R"({"type":"Catalog","stac_version":"%1","id":"test","description":"test","links":[]})" ).arg( version ).toUtf8() );
    REQUIRE( parser.catalog() );
  }
  for ( const char *version : { "1.0.0-rc1", "0.9.0", "2.0.0", "1x0y1" } )
  {
    QgsStacParser parser;
    parser.setData( QString( R"({"type":"Catalog","stac_version":"%1","id":"test","description":"test","links":[]})" ).arg( version ).toUtf8() );
    CHECK( !parser.catalog() );
  }
}

TEST_CASE( "Empty Mapbox font stops fall back with a named warning", "[core_safety]" )
{
  app();
  QgsMapBoxGlStyleConverter converter;
  const QString style = R"({"version":8,"layers":[{"id":"labels","type":"symbol","source-layer":"places","layout":{"text-field":"{name}","text-font":{"stops":[]}}}]})";
  CHECK( converter.convert( style ) == QgsMapBoxGlStyleConverter::Success );
  CHECK( converter.warnings().join( " " ).contains( "Empty text-font stops" ) );
}

TEST_CASE( "Mapserver GML retains every MultiLineString member", "[core_safety]" )
{
  const QString xml = R"(<gml:MultiLineString xmlns:gml="http://www.opengis.net/gml">
  <gml:LineString><gml:coordinates>0,0 1,1</gml:coordinates></gml:LineString>
  <gml:LineString><gml:coordinates>2,2 3,3</gml:coordinates></gml:LineString>
  </gml:MultiLineString>)";
  const QgsGeometry geometry = QgsOgcUtils::geometryFromGML( xml );
  REQUIRE( !geometry.isNull() );
  const auto lines = geometry.asMultiPolyline();
  REQUIRE( lines.size() == 2 );
  CHECK( lines[0].front() == QgsPointXY( 0, 0 ) );
  CHECK( lines[1].back() == QgsPointXY( 3, 3 ) );
}


TEST_CASE( "Layer clones retain server properties without modifying the source", "[functional]" )
{
  app();
  QgsVectorLayer source( "Point?crs=EPSG:4326", "source", "memory" );
  REQUIRE( source.isValid() );
  source.serverProperties()->setTitle( "Service title" );
  source.serverProperties()->setAbstract( "Service abstract" );
  std::unique_ptr<QgsVectorLayer> copy( source.clone() );
  REQUIRE( copy );
  CHECK( source.serverProperties()->title() == "Service title" );
  CHECK( source.serverProperties()->abstract() == "Service abstract" );
  CHECK( copy->serverProperties()->title() == source.serverProperties()->title() );
  CHECK( copy->serverProperties()->abstract() == source.serverProperties()->abstract() );
}

TEST_CASE( "Geometry expressions honor explicit miter limits", "[functional]" )
{
  app();
  const QgsGeometry line = QgsGeometry::fromWkt( "LINESTRING(0 0, 10 0, 0 -1)" );
  for ( const char *name : { "offset_curve", "single_sided_buffer" } )
  {
    const auto evaluate = [&]( double limit ) {
      QgsExpression expression( QString( "%1(geom_from_wkt('LINESTRING(0 0, 10 0, 0 -1)'), 1, 8, 2, %2)" )
                                .arg( name ).arg( limit ) );
      const QVariant result = expression.evaluate();
      REQUIRE_FALSE( expression.hasEvalError() );
      return result.value<QgsGeometry>();
    };
    const QgsGeometry shortMiter = evaluate( 1 );
    const QgsGeometry longMiter = evaluate( 20 );
    REQUIRE( !shortMiter.isNull() );
    REQUIRE( !longMiter.isNull() );
    CHECK_FALSE( shortMiter.equals( longMiter ) );
    const auto expected = std::string( name ) == "offset_curve"
      ? line.offsetCurve( 1, 8, Qgis::JoinStyle::Miter, 20 )
      : line.singleSidedBuffer( 1, 8, Qgis::BufferSide::Left, Qgis::JoinStyle::Miter, 20 );
    CHECK( longMiter.equals( expected ) );
  }
}

#if QT_CONFIG(process)
namespace {
class CapturedProcessOutput : public QgsMessageOutput
{
public:
  QString appended;
  void setMessage( const QString &, Qgis::StringFormat ) override {}
  void appendMessage( const QString &message ) override { appended += message; }
  void setTitle( const QString & ) override {}
  void showMessage( bool ) override {}
};
CapturedProcessOutput *processOutput = nullptr;
}

TEST_CASE( "Captured subprocesses drain stdout and stderr independently", "[functional][process]" )
{
  app();
  CapturedProcessOutput output;
  processOutput = &output;
  struct Restore {
    ~Restore() { QgsMessageOutput::setMessageOutputCreator( []() -> QgsMessageOutput * { return new QgsMessageOutputConsole; } ); processOutput = nullptr; }
  } restore;
  QgsMessageOutput::setMessageOutputCreator( []() -> QgsMessageOutput * { return processOutput; } );
#ifdef Q_OS_WIN
  const QString command = "cmd.exe /c \"echo STDOUT_MARKER & echo STDERR_MARKER 1>&2\"";
#else
  const QString command = "/bin/sh -c \"printf STDOUT_MARKER; printf STDERR_MARKER >&2\"";
#endif
  QPointer<QgsRunProcess> process = QgsRunProcess::create( command, true );
  QEventLoop loop;
  QObject::connect( process, &QObject::destroyed, &loop, &QEventLoop::quit );
  QTimer::singleShot( 5000, &loop, &QEventLoop::quit );
  loop.exec();
  const bool completed = process.isNull();
  if ( process ) process->dialogGone();
  CHECK( completed );
  CHECK( output.appended.contains( "STDOUT_MARKER" ) );
  CHECK( output.appended.contains( "STDERR_MARKER" ) );
  CHECK( output.appended.contains( "color=red>STDERR_MARKER" ) );
}
#endif


TEST_CASE( "Layout charts render without a linked vector layer", "[functional][layout]" )
{
  app();
  QgsProject project;
  QgsLayout layout( &project );
  layout.initializeDefaults();
  auto *chart = new QgsLayoutItemChart( &layout );
  chart->setPlot( new QgsLineChartPlot );
  QgsLayoutItemChart::SeriesDetails series( "Missing layer" );
  series.setXExpression( "1" );
  series.setYExpression( "2" );
  chart->setSeriesList( { series } );
  chart->attemptResize( QgsLayoutSize( 100, 100 ) );
  layout.addLayoutItem( chart );
  QgsLayoutExporter exporter( &layout );
  CHECK_FALSE( exporter.renderPageToImage( 0, QSize( 300, 300 ) ).isNull() );
}
