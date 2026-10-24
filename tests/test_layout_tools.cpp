// test_layout_tools.cpp — Headless cartographic layout tool regressions
//
// Covers the Agent/MCP surface of the Cartographic Layout Studio: layout
// creation, item add/get/set through the shared property layer (same
// QgsLayoutItem setters + undo commands as the GUI inspector), alignment,
// template round-trip, export with memory preflight, and project persistence.
#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>

#include <limits>

#include <QApplication>


#include <gdal_priv.h>

#include <qgsapplication.h>
#include <qgscolorrampshader.h>
#include <qgscolorrampimpl.h>
#include <qgslayertree.h>
#include <qgslayertreemodellegendnode.h>
#include <qgscolorramplegendnode.h>
#include <qgslayout.h>
#include <qgslayoutitemlabel.h>
#include <qgslayoutitemlegend.h>
#include <qgslayoutitemmap.h>
#include <qgslayoutitempage.h>
#include <qgslayoutitemregistry.h>
#include <qgslayoutitemscalebar.h>
#include <qgslayoutpagecollection.h>
#include <qgslayoutsize.h>
#include <qgslayoutmanager.h>
#include <qgslayoutundostack.h>
#include <qgsprintlayout.h>
#include <qgsproject.h>
#include <qgsrasterlayer.h>
#include <qgssinglebandpseudocolorrenderer.h>
#include <qgsvectorlayer.h>

#include "agent/layout_tools/layout_service.h"
#include "agent/layout_tools/layout_tools.h"
#include "agent/spatial_tools/spatial_tool.h"
#include "support/qt_lifecycle.h"

using sicnu::agent::layout_tools::LayoutService;
using sicnu::agent::spatial_tools::SpatialToolRegistry;

// Retirement note (Track 2 R4): the retired defense left via _Exit before any
// static destructor could run. The shared TeardownListener
// (support/qt_lifecycle.h) instead performs the ordered teardown — deferred
// deletes, exitQgis()/invalidateCaches() while the Q_GLOBAL_STATIC guards are
// alive, then heap-owned app deletion — inside the run, before main returns.
CATCH_REGISTER_LISTENER( sicnu::test::qtlifecycle::TeardownListener )

namespace
{
void ensureQgisApplication()
{
  if ( QApplication::instance() )
    return;
  static int argc = 1;
  static char applicationName[] = "test_layout_tools";
  static char *argv[] = { applicationName, nullptr };
  static auto *application = new QgsApplication( argc, argv, true );
  ( void ) application;
  QgsApplication::initQgis();
}

void cleanupProject()
{
  QgsProject::instance()->removeAllMapLayers();
  QgsProject::instance()->layoutManager()->clear();
}

Json::Value jsonObject( const std::string &text )
{
  Json::CharReaderBuilder builder;
  std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
  Json::Value value;
  std::string errors;
  reader->parse( text.data(), text.data() + text.size(), &value, &errors );
  return value;
}

bool fuzzyEquals( double a, double b, double epsilon = 0.01 )
{
  return std::abs( a - b ) < epsilon;
}

SpatialToolRegistry &registry()
{
  auto &reg = SpatialToolRegistry::instance();
  reg.registerBuiltinTools(); // idempotent; registers layout:* tools too
  return reg;
}

sicnu::agent::spatial_tools::SpatialToolPtr tool( const std::string &name )
{
  const auto t = registry().find( name );
  REQUIRE( t.has_value() );
  return *t;
}

sicnu::agent::spatial_tools::SpatialToolResult run( const std::string &name, const std::string &inputJson )
{
  return tool( name )->execute( jsonObject( inputJson ) );
}

} // namespace

TEST_CASE( "Layout tools are registered under layout: prefix", "[layout][mcp]" )
{
  ensureQgisApplication();
  cleanupProject();
  registry();

  CHECK( registry().find( "layout:list" ).has_value() );
  CHECK( registry().find( "layout:create" ).has_value() );
  CHECK( registry().find( "layout:list_items" ).has_value() );
  CHECK( registry().find( "layout:add_item" ).has_value() );
  CHECK( registry().find( "layout:set_item_properties" ).has_value() );
  CHECK( registry().find( "layout:export" ).has_value() );

  // Schemas must be valid objects with a type.
  const auto t = tool( "layout:set_item_properties" );
  CHECK( t->inputSchema()["type"].asString() == "object" );
  CHECK( t->inputSchema()["required"].isArray() );
}

TEST_CASE( "layout:create/list round trip with page sizes", "[layout][mcp]" )
{
  ensureQgisApplication();
  cleanupProject();

  auto result = run( "layout:create", R"({"name":"Thematic","page_size":"A3","orientation":"landscape"})" );
  REQUIRE( result.success );
  CHECK( LayoutService::instance().layoutNames().contains( QStringLiteral( "Thematic" ) ) );

  QgsPrintLayout *layout = LayoutService::instance().findLayout( QStringLiteral( "Thematic" ) );
  REQUIRE( layout != nullptr );
  REQUIRE( layout->pageCollection()->pageCount() == 1 );
  CHECK( fuzzyEquals( layout->pageCollection()->pages().first()->pageSize().width(), 420.0 ) );
  CHECK( fuzzyEquals( layout->pageCollection()->pages().first()->pageSize().height(), 297.0 ) );

  // Duplicate names are rejected.
  auto dup = run( "layout:create", R"({"name":"Thematic"})" );
  CHECK( !dup.success );

  auto list = run( "layout:list", "{}" );
  REQUIRE( list.success );
  CHECK( list.output["layouts"].size() == 1 );
}

TEST_CASE( "layout:add_item + get/set_item_properties drive the real QgsLayoutItem",
           "[layout][mcp][properties]" )
{
  ensureQgisApplication();
  cleanupProject();

  REQUIRE( run( "layout:create", R"({"name":"L"})" ).success );
  QgsPrintLayout *layout = LayoutService::instance().findLayout( QStringLiteral( "L" ) );
  REQUIRE( layout != nullptr );

  SECTION( "label item" )
  {
    auto added = run( "layout:add_item",
                      R"({"layout":"L","type":"title","properties":{"x":20,"y":10,"width":150,"text":"四川省耕地分布图"}})" );
    REQUIRE( added.success );
    const std::string itemId = added.output["id"].asString();
    CHECK( added.output["item"]["text"].asString() == "四川省耕地分布图" );

    // Compact listing exposes geometry in mm.
    auto listing = run( "layout:list_items", R"({"layout":"L"})" );
    REQUIRE( listing.success );
    CHECK( listing.output["items"].size() == 1 );
    CHECK( fuzzyEquals( listing.output["items"][0]["x"].asDouble(), 20.0 ) );
    CHECK( fuzzyEquals( listing.output["items"][0]["width"].asDouble(), 150.0 ) );

    // Property edit through the shared layer: one undoable step.
    const std::string setInput = std::string( R"({"layout":"L","item":")" ) + itemId +
                                 R"(","properties":{"x":42.5,"text":"成都小学分布图","font_size":16}})";
    auto setResult = run( "layout:set_item_properties", setInput );
    REQUIRE( setResult.success );
    CHECK( setResult.output["applied"].size() >= 2 );

    QgsLayoutItem *item = LayoutService::instance().findItem( layout, QString::fromStdString( itemId ) );
    REQUIRE( item != nullptr );
    CHECK( fuzzyEquals( item->positionWithUnits().x(), 42.5 ) );
    CHECK( fuzzyEquals( item->sizeWithUnits().width(), 150.0 ) ); // untouched property preserved

    // Undo restores the previous state (same stack the GUI uses).
    layout->undoStack()->stack()->undo();
    CHECK( fuzzyEquals( item->positionWithUnits().x(), 20.0 ) );

    // Unknown keys are reported, not silently dropped.
    auto invalid = tool( "layout:set_item_properties" )->execute(
        jsonObject( std::string( R"({"layout":"L","item":")" ) + itemId +
                    R"(","properties":{"no_such_key":1}})" ) );
    REQUIRE( invalid.success );
    CHECK( invalid.output["ignored"].size() == 1 );
  }

  SECTION( "map/legend/scalebar/northarrow linkage" )
  {
    REQUIRE( run( "layout:add_item", R"({"layout":"L","type":"map","properties":{"x":10,"y":10}})" ).success );
    auto legend = run( "layout:add_item", R"({"layout":"L","type":"legend","properties":{"title":"Legend"}})" );
    REQUIRE( legend.success );
    auto scaleBar = run( "layout:add_item", R"({"layout":"L","type":"scalebar"})" );
    REQUIRE( scaleBar.success );
    auto north = run( "layout:add_item", R"({"layout":"L","type":"northarrow"})" );
    REQUIRE( north.success );

    // Newly added satellite items auto-link to the layout reference map.
    QgsLayoutItemMap *map = layout->referenceMap();
    REQUIRE( map != nullptr );
    QgsLayoutItem *legendItem = LayoutService::instance().findItem( layout, QString::fromStdString( legend.output["id"].asString() ) );
    auto *legendPtr = qobject_cast<QgsLayoutItemLegend *>( legendItem );
    REQUIRE( legendPtr != nullptr );
    CHECK( legendPtr->linkedMap() == map );

    QgsLayoutItem *barItem = LayoutService::instance().findItem( layout, QString::fromStdString( scaleBar.output["id"].asString() ) );
    auto *bar = qobject_cast<QgsLayoutItemScaleBar *>( barItem );
    REQUIRE( bar != nullptr );
    CHECK( bar->linkedMap() == map );

    QgsLayoutItem *northItem = LayoutService::instance().findItem( layout, QString::fromStdString( north.output["id"].asString() ) );
    CHECK( LayoutService::instance().itemProperties( northItem )["type"].asString() == "northarrow" );

    // get_item_properties reports the linkage by name.
    const std::string getInput =
        std::string( R"({"layout":"L","item":")" ) + legend.output["id"].asString() + R"("})";
    auto propResult = run( "layout:get_item_properties", getInput );
    REQUIRE( propResult.success );
    CHECK( propResult.output["properties"].isMember( "linked_map" ) );
  }
}

TEST_CASE( "layout:align_items and distribute_items reposition items", "[layout][mcp][align]" )
{
  ensureQgisApplication();
  cleanupProject();

  REQUIRE( run( "layout:create", R"({"name":"Align"})" ).success );
  QgsPrintLayout *layout = LayoutService::instance().findLayout( QStringLiteral( "Align" ) );
  REQUIRE( layout != nullptr );

  std::vector<QString> ids;
  for ( int i = 0; i < 3; ++i )
  {
    const std::string addInput = R"({"layout":"Align","type":"label","properties":{"x":)" +
                                 std::to_string( 10 + i * 20 ) + R"(,"y":)" + std::to_string( 10 + i * 25 ) + R"(}})";
    auto added = run( "layout:add_item", addInput );
    REQUIRE( added.success );
    ids.push_back( QString::fromStdString( added.output["id"].asString() ) );
  }

  std::string items = "[";
  for ( int i = 0; i < 3; ++i )
    items += "\"" + ids[i].toStdString() + "\"" + ( i < 2 ? "," : "" );
  items += "]";

  // Align right edges → all items share the maximum right edge x+width.
  auto aligned = tool( "layout:align_items" )->execute(
      jsonObject( std::string( R"({"layout":"Align","items":)" + items + R"(,"alignment":"right"})" ) ) );
  REQUIRE( aligned.success );

  double rightEdge = 0.0;
  bool first = true;
  for ( const QString &id : ids )
  {
    QgsLayoutItem *item = LayoutService::instance().findItem( layout, id );
    REQUIRE( item != nullptr );
    const double edge = item->positionWithUnits().x() + item->sizeWithUnits().width();
    if ( first )
    {
      rightEdge = edge;
      first = false;
    }
    else
    {
      CHECK( fuzzyEquals( edge, rightEdge ) );
    }
  }

  // Distribute needs ≥ 3 items.
  auto distributed = tool( "layout:distribute_items" )->execute(
      jsonObject( std::string( R"({"layout":"Align","items":)" + items + R"(,"distribution":"vspace"})" ) ) );
  REQUIRE( distributed.success );

  // Bad alignment name → validation error.
  auto bad = tool( "layout:align_items" )->execute(
      jsonObject( std::string( R"({"layout":"Align","items":)" + items + R"(,"alignment":"diagonal"})" ) ) );
  CHECK( !bad.success );
}

TEST_CASE( "layout:save_template/apply_template round-trip preserves properties", "[layout][mcp][template]" )
{
  ensureQgisApplication();
  cleanupProject();

  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString templatePath = dir.filePath( QStringLiteral( "template.qpt" ) );

  REQUIRE( run( "layout:create", R"({"name":"Original"})" ).success );
  QgsPrintLayout *layout = LayoutService::instance().findLayout( QStringLiteral( "Original" ) );
  REQUIRE( layout != nullptr );

  tool( "layout:add_item" )->execute( jsonObject(
      R"({"layout":"Original","type":"title","properties":{"x":15,"y":8,"width":180,"text":"Round Trip 标题"}})" ) );
  tool( "layout:add_item" )->execute( jsonObject( R"({"layout":"Original","type":"label","properties":{"x":30,"y":60,"rotation":12.5,"opacity":70}})" ) );

  const std::string saveInput = R"({"layout":"Original","path":")" + templatePath.toStdString() + R"("})";
  auto saved = run( "layout:save_template", saveInput );
  REQUIRE( saved.success );
  REQUIRE( QFile::exists( templatePath ) );

  const std::string applyInput = R"({"name":"FromTemplate","path":")" + templatePath.toStdString() + R"("})";
  auto applied = run( "layout:apply_template", applyInput );
  REQUIRE( applied.success );
  REQUIRE( applied.output["items"].size() == 2 );
  QgsPrintLayout *reloaded = LayoutService::instance().findLayout( QStringLiteral( "FromTemplate" ) );
  REQUIRE( reloaded != nullptr );
  // The template load must replace the initialized default page, not stack a second one.
  REQUIRE( reloaded->pageCollection()->pageCount() == 1 );

  QList<QgsLayoutItemLabel *> labels;
  reloaded->layoutItems( labels );
  REQUIRE( labels.size() == 2 );

  bool foundTitle = false;
  bool foundRotated = false;
  for ( QgsLayoutItemLabel *label : labels )
  {
    if ( label->text() == QStringLiteral( "Round Trip 标题" ) )
    {
      foundTitle = true;
      CHECK( fuzzyEquals( label->positionWithUnits().x(), 15.0 ) );
      CHECK( fuzzyEquals( label->positionWithUnits().y(), 8.0 ) );
      CHECK( fuzzyEquals( label->sizeWithUnits().width(), 180.0 ) );
    }
    else
    {
      foundRotated = true;
      CHECK( fuzzyEquals( label->itemRotation(), 12.5 ) );
      CHECK( fuzzyEquals( label->itemOpacity(), 0.7 ) );
    }
  }
  CHECK( foundTitle );
  CHECK( foundRotated );
}

TEST_CASE( "layout:export writes real images with memory preflight", "[layout][mcp][export]" )
{
  ensureQgisApplication();
  cleanupProject();

  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  REQUIRE( run( "layout:create", R"({"name":"Export"})" ).success );
  tool( "layout:add_item" )->execute( jsonObject( R"({"layout":"Export","type":"label","properties":{"text":"导出测试"}})" ) );

  // A4 @ 96 dpi → 794x1123 px.
  const QString png = dir.filePath( QStringLiteral( "out.png" ) );
  const std::string pngInput =
      R"({"layout":"Export","path":")" + png.toStdString() + R"(","format":"png","dpi":96})";
  auto exported = run( "layout:export", pngInput );
  REQUIRE( exported.success );
  QImage image( png );
  REQUIRE( !image.isNull() );
  CHECK( std::abs( image.width() - 794 ) <= 2 );
  CHECK( std::abs( image.height() - 1123 ) <= 2 );
  CHECK( ( !image.isNull() && image.sizeInBytes() > 0 ) );

  // PDF export produces a valid non-empty file (%PDF magic).
  const QString pdf = dir.filePath( QStringLiteral( "out.pdf" ) );
  const std::string pdfInput =
      R"({"layout":"Export","path":")" + pdf.toStdString() + R"(","format":"pdf","dpi":96})";
  auto pdfResult = run( "layout:export", pdfInput );
  REQUIRE( pdfResult.success );
  QFile pdfFile( pdf );
  REQUIRE( pdfFile.open( QIODevice::ReadOnly ) );
  const QByteArray magic = pdfFile.read( 5 );
  CHECK( magic.startsWith( "%PDF" ) );

  // Memory guard: A0 @ 1200 dpi ≈ 39684 × 56063 px exceeds the per-edge pixel
  // limit and must be rejected before any buffer is allocated.
  REQUIRE( run( "layout:create", R"({"name":"A0Big","page_size":"A0"})" ).success );
  const QString huge = dir.filePath( QStringLiteral( "huge.png" ) );
  const std::string hugeInput =
      R"({"layout":"A0Big","path":")" + huge.toStdString() + R"(","format":"png","dpi":1200})";
  auto refused = run( "layout:export", hugeInput );
  CHECK( !refused.success );
  CHECK( refused.error.find( "reduce the DPI" ) != std::string::npos );
  CHECK( !QFile::exists( huge ) );

  // Unknown formats are rejected.
  auto badFormat = run( "layout:export", R"({"layout":"Export","path":"/tmp/x.gif","format":"gif"})" );
  CHECK( !badFormat.success );
}

TEST_CASE( "layout:save_project/load_project keeps layouts alive", "[layout][mcp][project]" )
{
  ensureQgisApplication();
  cleanupProject();

  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString projectPath = dir.filePath( QStringLiteral( "project.qgs" ) );

  REQUIRE( run( "layout:create", R"({"name":"Persist"})" ).success );
  tool( "layout:add_item" )->execute( jsonObject(
      R"({"layout":"Persist","type":"title","properties":{"text":"持久化标题"}})" ) );

  const std::string saveInput = R"({"path":")" + projectPath.toStdString() + R"("})";
  REQUIRE( run( "layout:save_project", saveInput ).success );
  REQUIRE( QFile::exists( projectPath ) );

  // Loading a project replaces the current one and restores the layout.
  REQUIRE( run( "layout:load_project", saveInput ).success );
  CHECK( LayoutService::instance().layoutNames().contains( QStringLiteral( "Persist" ) ) );

  QgsPrintLayout *layout = LayoutService::instance().findLayout( QStringLiteral( "Persist" ) );
  REQUIRE( layout != nullptr );
  QList<QgsLayoutItemLabel *> labels;
  layout->layoutItems( labels );
  REQUIRE( labels.size() == 1 );
  CHECK( labels.first()->text() == QStringLiteral( "持久化标题" ) );
}

TEST_CASE( "layout:auto_arrange builds the thematic composition without touching user items",
           "[layout][mcp][autoarrange]" )
{
  ensureQgisApplication();
  cleanupProject();

  REQUIRE( run( "layout:create", R"({"name":"Auto"})" ).success );
  QgsPrintLayout *layout = LayoutService::instance().findLayout( QStringLiteral( "Auto" ) );
  REQUIRE( layout != nullptr );

  // A user item must never be moved by auto-arrange.
  auto userItem = run( "layout:add_item", R"({"layout":"Auto","type":"label","properties":{"x":7,"y":7,"name":"user-note"}})" );
  REQUIRE( userItem.success );
  QgsLayoutItem *user = LayoutService::instance().findItem( layout, QStringLiteral( "user-note" ) );
  REQUIRE( user != nullptr );
  const double userY = user->positionWithUnits().y();

  // Dry-run must not mutate.
  auto dry = run( "layout:auto_arrange", R"({"layout":"Auto","apply":false})" );
  REQUIRE( dry.success );
  CHECK( dry.output["applied"].asBool() == false );
  {
    QList<QgsLayoutItem *> items;
    layout->layoutItems( items );
    int contentCount = 0;
    for ( QgsLayoutItem *item : items )
    {
      if ( item->type() != QgsLayoutItemRegistry::LayoutPage )
        ++contentCount;
    }
    CHECK( contentCount == 1 );
  }

  auto applied = run( "layout:auto_arrange", R"({"layout":"Auto","apply":true})" );
  REQUIRE( applied.success );
  CHECK( applied.output["applied"].asBool() == true );

    QList<QgsLayoutItem *> itemsWithPages;
  layout->layoutItems( itemsWithPages );
  QList<QgsLayoutItem *> items;
  for ( QgsLayoutItem *item : itemsWithPages )
  {
    if ( item->type() != QgsLayoutItemRegistry::LayoutPage )
      items.append( item );
  }
  REQUIRE( items.size() == 7 ); // user label + map/title/legend/scalebar/north/source

  const auto findByType = [layout]( const QString &id ) {
    return LayoutService::instance().findItem( layout, id );
  };
  QgsLayoutItem *map = findByType( QStringLiteral( "auto:map" ) );
  QgsLayoutItem *title = findByType( QStringLiteral( "auto:title" ) );
  QgsLayoutItem *north = findByType( QStringLiteral( "auto:northarrow" ) );
  QgsLayoutItem *scaleBar = findByType( QStringLiteral( "auto:scalebar" ) );
  REQUIRE( map != nullptr );
  REQUIRE( title != nullptr );
  REQUIRE( north != nullptr );
  REQUIRE( scaleBar != nullptr );

  // Title above map; north arrow inside the map region (not over the legend);
  // scale bar below the map; user item untouched.
  CHECK( title->positionWithUnits().y() < map->positionWithUnits().y() );
  const double northX = north->positionWithUnits().x();
  CHECK( northX >= map->positionWithUnits().x() );
  CHECK( northX + north->sizeWithUnits().width() <=
         map->positionWithUnits().x() + map->sizeWithUnits().width() );
  CHECK( scaleBar->positionWithUnits().y() >=
         map->positionWithUnits().y() + map->sizeWithUnits().height() );
  CHECK( fuzzyEquals( user->positionWithUnits().y(), userY ) );

  // Everything on the A4 page.
  for ( QgsLayoutItem *item : items )
  {
    CHECK( item->positionWithUnits().x() >= 0.0 );
    CHECK( item->positionWithUnits().y() >= 0.0 );
    CHECK( item->positionWithUnits().x() + item->sizeWithUnits().width() <= 210.5 );
    CHECK( item->positionWithUnits().y() + item->sizeWithUnits().height() <= 297.5 );
  }
}

// ---------------------------------------------------------------------------
// Performance smoke: large layouts must stay linear and interactive.
// ---------------------------------------------------------------------------
TEST_CASE( "Layout tools scale to 500 items without quadratic blowups", "[layout][mcp][perf]" )
{
  ensureQgisApplication();
  cleanupProject();

  REQUIRE( run( "layout:create", R"({"name":"Big"})" ).success );
  QgsPrintLayout *layout = LayoutService::instance().findLayout( QStringLiteral( "Big" ) );
  REQUIRE( layout != nullptr );

  // Build 500 shapes through the service (direct API: creation cost is not
  // what this test measures).
  for ( int i = 0; i < 500; ++i )
  {
    const std::string input = R"({"layout":"Big","type":"shape","properties":{"x":)" +
                              std::to_string( 2 + ( i % 20 ) * 10 ) + R"(,"y":)" +
                              std::to_string( 2 + ( i / 20 ) * 5 ) + R"(,"name":"shape_)" +
                              std::to_string( i ) + R"("}})";
    REQUIRE( run( "layout:add_item", input ).success );
  }
  QList<QgsLayoutItem *> items;
  layout->layoutItems( items );
  int contentCount = 0;
  for ( QgsLayoutItem *item : items )
  {
    if ( item->type() != QgsLayoutItemRegistry::LayoutPage )
      ++contentCount;
  }
  REQUIRE( contentCount == 500 );

  // Compact listing of all 500 items.
  QElapsedTimer timer;
  timer.start();
  auto listing = run( "layout:list_items", R"({"layout":"Big"})" );
  REQUIRE( listing.success );
  REQUIRE( listing.output["items"].size() == 500 );
  const qint64 listMs = timer.elapsed();

  // Full property read of one item resolves through the uuid pass.
  timer.restart();
  QgsLayoutItem *first = LayoutService::instance().findItem( layout, QStringLiteral( "shape_0" ) );
  REQUIRE( first != nullptr );
  const Json::Value props = LayoutService::instance().itemProperties( first );
  REQUIRE( props.isMember( "x" ) );
  const qint64 readMs = timer.elapsed();

  // One batch edit must not rescan quadratically (generous CI-safe bounds;
  // a quadratic regression would be seconds-to-minutes, not milliseconds).
  timer.restart();
  Json::Value update( Json::objectValue );
  update["opacity"] = 55.0;
  update["rotation"] = 10.0;
  QStringList applied, ignored;
  REQUIRE( LayoutService::instance().applyItemProperties( first, update, &applied, &ignored, nullptr ) );
  const qint64 editMs = timer.elapsed();
  REQUIRE( fuzzyEquals( first->itemOpacity(), 0.55 ) );

  INFO( "list 500 items: " << listMs << " ms; read: " << readMs << " ms; edit: " << editMs << " ms" );
  CHECK( listMs < 2000 );
  CHECK( readMs < 100 );
  CHECK( editMs < 200 );
}

// ---------------------------------------------------------------------------
// Phase H: raster renderer → legend color-ramp linkage. A layout legend
// linked to a map with a ramp-rendered raster must surface the color ramp
// through QGIS's native color ramp legend nodes (the "color bar").
// ---------------------------------------------------------------------------
TEST_CASE( "Raster renderer drives legend color ramp nodes in layouts", "[layout][mcp][colorramp]" )
{
  ensureQgisApplication();
  cleanupProject();

  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString tif = dir.filePath( QStringLiteral( "dem.tif" ) );

  // 8x8 float GeoTIFF with values 0..7.
  {
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDatasetH ds = GDALCreate( driver, tif.toUtf8().constData(), 8, 8, 1, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    double geoTransform[6] = { 100.0, 1.0, 0.0, 200.0, 0.0, -1.0 };
    GDALSetGeoTransform( ds, geoTransform );
    float data[64];
    for ( int i = 0; i < 64; ++i )
      data[i] = static_cast< float >( i % 8 );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    GDALRasterIO( band, GF_Write, 0, 0, 8, 8, data, 8, 8, GDT_Float32, 0, 0 );
    GDALClose( ds );
  }

  auto *layer = new QgsRasterLayer( tif, QStringLiteral( "DEM" ), QStringLiteral( "gdal" ) );
  REQUIRE( layer->isValid() );

  // Continuous pseudocolor renderer over a red→green ramp.
  auto *renderer = new QgsSingleBandPseudoColorRenderer( layer->dataProvider(), 1 );
  renderer->createShader( new QgsGradientColorRamp( QColor( 200, 30, 30 ), QColor( 30, 180, 60 ) ),
                          Qgis::ShaderInterpolationMethod::Linear,
                          Qgis::ShaderClassificationMethod::Continuous,
                          /*classes=*/0, /*clip=*/false, layer->extent() );
  layer->setRenderer( renderer );
  QgsProject::instance()->addMapLayer( layer );

  // The renderer must produce a color ramp legend node — this is exactly
  // what a linked layout legend renders as its color bar.
  QgsLayerTreeLayer *nodeLayer = QgsProject::instance()->layerTreeRoot()->findLayer( layer->id() );
  REQUIRE( nodeLayer != nullptr );
  const QList<QgsLayerTreeModelLegendNode *> nodes = renderer->createLegendNodes( nodeLayer );
  REQUIRE( !nodes.isEmpty() );
  bool hasColorRampNode = false;
  for ( QgsLayerTreeModelLegendNode *node : nodes )
  {
    if ( qobject_cast<QgsColorRampLegendNode *>( node ) )
      hasColorRampNode = true;
    delete node;
  }
  CHECK( hasColorRampNode );

  // End to end through the layout tools: a map with the raster layer and a
  // linked legend picks the layer up automatically.
  REQUIRE( run( "layout:create", R"({"name":"Thematic"})" ).success );
  QgsPrintLayout *layout = LayoutService::instance().findLayout( QStringLiteral( "Thematic" ) );
  REQUIRE( layout != nullptr );

  auto mapAdded = run( "layout:add_item",
                       R"({"layout":"Thematic","type":"map","properties":{"x":10,"y":30,"width":180,"height":200,"layers":["DEM"]}})" );
  REQUIRE( mapAdded.success );
  QgsLayoutItemMap *map = layout->referenceMap();
  REQUIRE( map != nullptr );
  REQUIRE( map->layers().contains( layer ) );

  auto legendAdded = run( "layout:add_item",
                          R"json({"layout":"Thematic","type":"legend","properties":{"title":"高程 (m)"}})json" );
  REQUIRE( legendAdded.success );
  QgsLayoutItem *legendItem =
      LayoutService::instance().findItem( layout, QString::fromStdString( legendAdded.output["id"].asString() ) );
  auto *legend = qobject_cast<QgsLayoutItemLegend *>( legendItem );
  REQUIRE( legend != nullptr );
  CHECK( legend->linkedMap() == map );

  // The legend model mirrors the linked map's layers.
  legend->refresh();
  CHECK( legend->model()->rootGroup()->findLayers().size() == 1 );
}

TEST_CASE( "LayoutService robustness: null layouts, non-finite properties and bounds (R13-G WP-07)", "[layout_service][robustness]" )
{
  ensureQgisApplication();
  QString error;

  SECTION( "removeItem on null layout fails cleanly with Null layout error" )
  {
    bool removed = LayoutService::instance().removeItem( nullptr, QStringLiteral( "any" ), &error );
    CHECK_FALSE( removed );
    CHECK( error == QStringLiteral( "Null layout" ) );
  }

  SECTION( "applyItemProperties rejects non-finite and malformed map extents" )
  {
    REQUIRE( run( "layout:create", R"({"name":"RobustnessTest"})" ).success );
    QgsPrintLayout *layout = LayoutService::instance().findLayout( QStringLiteral( "RobustnessTest" ) );
    REQUIRE( layout != nullptr );

    auto mapAdded = run( "layout:add_item",
                         R"({"layout":"RobustnessTest","type":"map","properties":{"x":10,"y":10,"width":100,"height":100}})" );
    REQUIRE( mapAdded.success );
    QgsLayoutItem *item = LayoutService::instance().findItem( layout, QString::fromStdString( mapAdded.output["id"].asString() ) );
    REQUIRE( item != nullptr );

    Json::Value badProps( Json::objectValue );
    Json::Value ext( Json::arrayValue );
    ext.append( std::numeric_limits<double>::quiet_NaN() );
    ext.append( 0.0 );
    ext.append( 100.0 );
    ext.append( 100.0 );
    badProps["extent"] = ext;

    QStringList applied;
    QStringList ignored;
    LayoutService::instance().applyItemProperties( item, badProps, &applied, &ignored, &error );
    CHECK( applied.empty() );
    CHECK( !ignored.empty() );
    CHECK( ignored.first().contains( "extent" ) );

    // Verify negative sibling dimension in batch resize cannot poison item height
    Json::Value resizeProps( Json::objectValue );
    resizeProps["width"] = 120.0;
    resizeProps["height"] = -10.0;
    applied.clear();
    ignored.clear();
    LayoutService::instance().applyItemProperties( item, resizeProps, &applied, &ignored, &error );
    CHECK( item->sizeWithUnits().height() == 100.0 );
    CHECK( item->sizeWithUnits().width() == 120.0 );
    CHECK( !ignored.filter( QStringLiteral( "height" ) ).isEmpty() );

    // Verify negative and NaN frame_width are rejected
    Json::Value frameProps( Json::objectValue );
    frameProps["frame_width"] = -2.0;
    applied.clear();
    ignored.clear();
    LayoutService::instance().applyItemProperties( item, frameProps, &applied, &ignored, &error );
    CHECK( applied.empty() );
    CHECK( !ignored.filter( QStringLiteral( "frame_width" ) ).isEmpty() );

    frameProps["frame_width"] = std::numeric_limits<double>::quiet_NaN();
    applied.clear();
    ignored.clear();
    LayoutService::instance().applyItemProperties( item, frameProps, &applied, &ignored, &error );
    CHECK( applied.empty() );
    CHECK( !ignored.filter( QStringLiteral( "frame_width" ) ).isEmpty() );

    LayoutService::instance().deleteLayout( QStringLiteral( "RobustnessTest" ) );
  }

  SECTION( "exportLayout rejects a NaN dpi instead of allocating NaN-sized rasters (R18-D)" )
  {
    REQUIRE( run( "layout:create", R"({"name":"NanDpiTest"})" ).success );
    QgsPrintLayout *layout = LayoutService::instance().findLayout( QStringLiteral( "NanDpiTest" ) );
    REQUIRE( layout != nullptr );

    // NaN fails BOTH comparisons of `dpi <= 0 || dpi > 1200`, so the guard
    // used to wave it through into NaN-sized raster math (the governed
    // cartography chain rejects the same value — export.cpp uses the
    // negated-range NaN-safe form). The refusal must come from THIS guard:
    // its error names the DPI bound; a downstream QGIS exporter failure
    // (NaN-sized image) would carry a different, nondescript message.
    error.clear();
    const bool ok = LayoutService::instance().exportLayout(
      layout, QStringLiteral( "/tmp/r18d-nan-dpi.png" ), QStringLiteral( "png" ),
      std::numeric_limits<double>::quiet_NaN(), /*maxBytes=*/0, &error );
    CHECK_FALSE( ok );
    REQUIRE( !error.isEmpty() );
    CHECK( error.contains( QStringLiteral( "DPI" ), Qt::CaseInsensitive ) );

    LayoutService::instance().deleteLayout( QStringLiteral( "NanDpiTest" ) );
  }

  SECTION( "exportLayout refuses sub-delivery dpi below the governed 72 floor (R20-04 DPI unify)" )
  {
    REQUIRE( run( "layout:create", R"({"name":"LowDpiTest"})" ).success );
    QgsPrintLayout *layout = LayoutService::instance().findLayout( QStringLiteral( "LowDpiTest" ) );
    REQUIRE( layout != nullptr );

    // layout:export (this seam) and cartography:export (export.cpp, pinned
    // at [72, 1200]) drive the same QGIS export engine; a 36-dpi export was
    // waved through HERE while the governed chain refuses it — two bounds
    // for one engine. The refusal must come from the DPI guard: its error
    // names the 72 floor (a rendering failure would not).
    error.clear();
    const bool ok = LayoutService::instance().exportLayout(
      layout, QStringLiteral( "/tmp/r2004-low-dpi.png" ), QStringLiteral( "png" ),
      /*dpi=*/36.0, /*maxBytes=*/0, &error );
    CHECK_FALSE( ok );
    REQUIRE( !error.isEmpty() );
    CHECK( error == QStringLiteral( "DPI must be between 72 and 1200" ) );

    // The boundary itself stays inclusive: 72 exports exactly like the
    // governed chain accepts it.
    error.clear();
    const bool ok72 = LayoutService::instance().exportLayout(
      layout, QStringLiteral( "/tmp/r2004-72-dpi.png" ), QStringLiteral( "png" ),
      /*dpi=*/72.0, /*maxBytes=*/0, &error );
    CHECK( ok72 );

    LayoutService::instance().deleteLayout( QStringLiteral( "LowDpiTest" ) );
  }
}

// ---------------------------------------------------------------------------
// R14-D WP-C+D — partial layer resolution honesty, multi-page export
// reporting, non-finite scale-bar/font guards.
// ---------------------------------------------------------------------------

TEST_CASE( "Partially-resolving map layer lists surface every missing reference (CARTO-5)",
           "[layout][mcp][properties][r14d]" )
{
  ensureQgisApplication();
  cleanupProject();

  auto *layer = new QgsVectorLayer( QStringLiteral( "Point?crs=EPSG:4326" ),
                                    QStringLiteral( "lt-layers-a" ), QStringLiteral( "memory" ) );
  QgsProject::instance()->addMapLayer( layer );

  REQUIRE( run( "layout:create", R"({"name":"Layers"})" ).success );
  QgsPrintLayout *layout = LayoutService::instance().findLayout( QStringLiteral( "Layers" ) );
  REQUIRE( layout != nullptr );

  // Some refs resolve, one does not: the tool must succeed with the
  // resolved subset AND report the missing ref — never drop it silently.
  auto added = run( "layout:add_item",
                    R"({"layout":"Layers","type":"map","properties":{"x":10,"y":10,"layers":["lt-layers-a","nope"]}})" );
  REQUIRE( added.success );
  bool surfacedNope = false;
  for ( const auto &entry : added.output["ignored"] )
    surfacedNope = surfacedNope ||
                   entry.asString().find( "nope" ) != std::string::npos;
  CHECK( surfacedNope );

  QgsLayoutItem *mapItem =
    LayoutService::instance().findItem( layout, QString::fromStdString( added.output["id"].asString() ) );
  REQUIRE( mapItem != nullptr );
  const Json::Value props = LayoutService::instance().itemProperties( mapItem );
  REQUIRE( props["layers"].isArray() );
  CHECK( props["layers"].size() == 1 );
  CHECK( props["layers"][0].asString() == "lt-layers-a" );

  // The property-edit path reports per-ref too (and applies nothing when
  // nothing resolves).
  const std::string setInput =
      std::string( R"({"layout":"Layers","item":")" ) + added.output["id"].asString() +
      R"(","properties":{"layers":["nope"]}})";
  auto setResult = run( "layout:set_item_properties", setInput );
  REQUIRE( setResult.success );
  surfacedNope = false;
  for ( const auto &entry : setResult.output["ignored"] )
    surfacedNope = surfacedNope ||
                   entry.asString().find( "nope" ) != std::string::npos;
  CHECK( surfacedNope );
  CHECK( LayoutService::instance().itemProperties( mapItem )["layers"].size() == 1 );

  LayoutService::instance().deleteLayout( QStringLiteral( "Layers" ) );
  cleanupProject();
}

TEST_CASE( "layout:export reports every page of a multi-page layout (CARTO-15)",
           "[layout][mcp][export][r14d]" )
{
  ensureQgisApplication();
  cleanupProject();

  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  REQUIRE( run( "layout:create", R"({"name":"MultiExport"})" ).success );
  QgsPrintLayout *layout = LayoutService::instance().findLayout( QStringLiteral( "MultiExport" ) );
  REQUIRE( layout != nullptr );
  REQUIRE( run( "layout:add_item",
                R"({"layout":"MultiExport","type":"label","properties":{"text":"第 1 页"}})" )
              .success );
  // Second physical page (pageCollection page 0 is the body page).
  auto *secondPage = new QgsLayoutItemPage( layout );
  secondPage->setPageSize( QgsLayoutSize( 210.0, 297.0 ) );
  layout->pageCollection()->addPage( secondPage );
  REQUIRE( layout->pageCollection()->pageCount() == 2 );

  const QString png = dir.filePath( QStringLiteral( "multi.png" ) );
  const std::string input =
      R"({"layout":"MultiExport","path":")" + png.toStdString() + R"(","format":"png","dpi":96})";
  auto exported = run( "layout:export", input );
  REQUIRE( exported.success );
  CHECK( exported.output["page_count"].asInt() == 2 );
  REQUIRE( exported.output["pages"].isArray() );
  REQUIRE( exported.output["pages"].size() == 2 );
  // Deterministic page names: page 0 at the declared path, page 1 as the
  // _2 sibling — and both are reported with their real byte sizes.
  const QString page2 = dir.filePath( QStringLiteral( "multi_2.png" ) );
  CHECK( exported.output["pages"][0]["path"].asString() == png.toStdString() );
  CHECK( exported.output["pages"][1]["path"].asString() == page2.toStdString() );
  REQUIRE( QFile::exists( png ) );
  REQUIRE( QFile::exists( page2 ) );
  CHECK( exported.output["pages"][0]["bytes"].asInt64() ==
         static_cast<Json::Int64>( QFileInfo( png ).size() ) );
  CHECK( exported.output["pages"][1]["bytes"].asInt64() ==
         static_cast<Json::Int64>( QFileInfo( page2 ).size() ) );
  CHECK( exported.output["bytes"].asInt64() ==
         static_cast<Json::Int64>( QFileInfo( png ).size() ) );

  LayoutService::instance().deleteLayout( QStringLiteral( "MultiExport" ) );
  cleanupProject();
}

// R21-D: the tool used to build pages[] by re-walking the delivery
// directory after the export, so a stale sibling from an earlier, larger
// delivery (or a page the design now excludes) was reported as a page THIS
// run delivered. The delivery set now comes from the export itself.
TEST_CASE( "layout:export never re-manifests stale page siblings (R21-D)",
           "[layout][mcp][export][r21d]" )
{
  ensureQgisApplication();
  cleanupProject();

  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  REQUIRE( run( "layout:create", R"({"name":"StaleSiblings"})" ).success );
  QgsPrintLayout *layout = LayoutService::instance().findLayout( QStringLiteral( "StaleSiblings" ) );
  REQUIRE( layout != nullptr );
  REQUIRE( run( "layout:add_item",
                R"({"layout":"StaleSiblings","type":"label","properties":{"text":"body"}})" )
              .success );

  // A stale sibling from an earlier, larger delivery sitting in the target
  // directory: the tool must not report it as a delivered page.
  const QString png = dir.filePath( QStringLiteral( "single.png" ) );
  const QString staleSibling = dir.filePath( QStringLiteral( "single_2.png" ) );
  {
    QFile stale( staleSibling );
    REQUIRE( stale.open( QIODevice::WriteOnly ) );
    stale.write( "stale" );
    stale.close();
  }
  REQUIRE( QFile::exists( staleSibling ) );

  const std::string input =
      R"({"layout":"StaleSiblings","path":")" + png.toStdString() + R"(","format":"png","dpi":96})";
  const auto exported = run( "layout:export", input );
  REQUIRE( exported.success );
  CHECK( exported.output["page_count"].asInt() == 1 );
  REQUIRE( exported.output["pages"].isArray() );
  CHECK( exported.output["pages"].size() == 1 );
  CHECK( exported.output["pages"][0]["path"].asString() == png.toStdString() );
  // The stale sibling is gone: this run's delivery is the complete set.
  CHECK( !QFile::exists( staleSibling ) );

  LayoutService::instance().deleteLayout( QStringLiteral( "StaleSiblings" ) );
  cleanupProject();
}

TEST_CASE( "Non-finite font sizes and scale-bar segments are ignored, not applied (CARTO-7)",
           "[layout_service][robustness][r14d]" )
{
  ensureQgisApplication();
  cleanupProject();
  QString error;

  REQUIRE( run( "layout:create", R"({"name":"FiniteGuards"})" ).success );
  QgsPrintLayout *layout = LayoutService::instance().findLayout( QStringLiteral( "FiniteGuards" ) );
  REQUIRE( layout != nullptr );

  auto labelAdded = run( "layout:add_item",
                         R"({"layout":"FiniteGuards","type":"label","properties":{"font_size":12}})" );
  REQUIRE( labelAdded.success );
  QgsLayoutItem *labelItem =
    LayoutService::instance().findItem( layout, QString::fromStdString( labelAdded.output["id"].asString() ) );
  REQUIRE( labelItem != nullptr );
  const double finiteSize = qobject_cast<QgsLayoutItemLabel *>( labelItem )->font().pointSizeF();

  Json::Value nanFont( Json::objectValue );
  nanFont["font_size"] = std::numeric_limits<double>::quiet_NaN();
  QStringList applied;
  QStringList ignored;
  REQUIRE( LayoutService::instance().applyItemProperties( labelItem, nanFont, &applied, &ignored,
                                                          &error ) );
  CHECK( applied.empty() );
  CHECK( !ignored.filter( QStringLiteral( "font_size" ) ).isEmpty() );
  CHECK( qobject_cast<QgsLayoutItemLabel *>( labelItem )->font().pointSizeF() == finiteSize );

  auto barAdded = run( "layout:add_item", R"({"layout":"FiniteGuards","type":"scalebar"})" );
  REQUIRE( barAdded.success );
  QgsLayoutItem *barItem =
    LayoutService::instance().findItem( layout, QString::fromStdString( barAdded.output["id"].asString() ) );
  REQUIRE( barItem != nullptr );

  // NaN units_per_segment: <= 0 guards pass NaN — finiteness must refuse.
  Json::Value nanSegments( Json::objectValue );
  nanSegments["units_per_segment"] = std::numeric_limits<double>::quiet_NaN();
  applied.clear();
  ignored.clear();
  REQUIRE( LayoutService::instance().applyItemProperties( barItem, nanSegments, &applied, &ignored,
                                                          &error ) );
  CHECK( applied.empty() );
  CHECK( !ignored.filter( QStringLiteral( "units_per_segment" ) ).isEmpty() );

  // The declared distance unit applies (and is reported), unknown ids do not.
  Json::Value unitProps( Json::objectValue );
  unitProps["unit"] = "km";
  applied.clear();
  ignored.clear();
  REQUIRE( LayoutService::instance().applyItemProperties( barItem, unitProps, &applied, &ignored,
                                                          &error ) );
  CHECK( applied.contains( QStringLiteral( "unit" ) ) );
  auto *bar = qobject_cast<QgsLayoutItemScaleBar *>( barItem );
  REQUIRE( bar != nullptr );
  CHECK( bar->units() == Qgis::DistanceUnit::Kilometers );

  Json::Value badUnit( Json::objectValue );
  badUnit["unit"] = "furlongs";
  applied.clear();
  ignored.clear();
  REQUIRE( LayoutService::instance().applyItemProperties( barItem, badUnit, &applied, &ignored,
                                                          &error ) );
  CHECK( applied.empty() );
  CHECK( !ignored.filter( QStringLiteral( "unit" ) ).isEmpty() );
  CHECK( bar->units() == Qgis::DistanceUnit::Kilometers );

  LayoutService::instance().deleteLayout( QStringLiteral( "FiniteGuards" ) );
  cleanupProject();
}

// R21-D: a wrong-TYPED boolean key used to throw Json::LogicError mid-apply
// (asBool() on a string/number), so the batch returned ok=false with the
// keys applied BEFORE it already mutated — a half-applied item and a
// misleading error. The documented contract is "unknown/invalid keys are
// reported through applied/ignored": wrong-typed keys are IGNORED.
TEST_CASE( "Wrong-typed boolean item properties are ignored, not a half-applied failure (R21-D)",
           "[layout][mcp][r21d]" )
{
  ensureQgisApplication();
  cleanupProject();

  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  QgsPrintLayout *layout = LayoutService::instance().createLayout(
    QStringLiteral( "BoolTypeGuards" ), QStringLiteral( "A4" ) );
  REQUIRE( layout != nullptr );
  QgsLayoutItem *item = nullptr;
  {
    // A plain shape: the generic item surface (visible/locked) applies.
    Json::Value shapeProps( Json::objectValue );
    shapeProps["name"] = "shape-1";
    QString addError;
    item = LayoutService::instance().addItem( layout, QStringLiteral( "shape" ), shapeProps,
                                              &addError );
    REQUIRE( item != nullptr );
    item = LayoutService::instance().findItem( layout, QStringLiteral( "shape-1" ) );
    REQUIRE( item != nullptr );
  }

  // A good key BEFORE the bad one must still be applied, and the bad key
  // must land in ignored with the call reporting success.
  Json::Value props( Json::objectValue );
  props["x"] = 30.0;
  props["visible"] = std::string( "yes" ); // wrong type: string, not bool
  QStringList applied;
  QStringList ignored;
  QString error;
  REQUIRE( LayoutService::instance().applyItemProperties( item, props, &applied, &ignored,
                                                          &error ) );
  CHECK( !ignored.filter( QStringLiteral( "visible" ) ).isEmpty() );
  CHECK( fuzzyEquals( item->positionWithUnits().x(), 30.0 ) ); // x really applied

  // locked: same shape of defect, same outcome.
  Json::Value lockProps( Json::objectValue );
  lockProps["locked"] = 7.0; // wrong type: number
  applied.clear();
  ignored.clear();
  REQUIRE( LayoutService::instance().applyItemProperties( item, lockProps, &applied, &ignored,
                                                          &error ) );
  CHECK( !ignored.filter( QStringLiteral( "locked" ) ).isEmpty() );

  // And the valid boolean still applies (guard did not break the key).
  Json::Value goodProps( Json::objectValue );
  goodProps["visible"] = false;
  applied.clear();
  ignored.clear();
  REQUIRE( LayoutService::instance().applyItemProperties( item, goodProps, &applied, &ignored,
                                                          &error ) );
  CHECK( ignored.isEmpty() );
  CHECK( !item->isVisible() );

  LayoutService::instance().deleteLayout( QStringLiteral( "BoolTypeGuards" ) );
  cleanupProject();
}
