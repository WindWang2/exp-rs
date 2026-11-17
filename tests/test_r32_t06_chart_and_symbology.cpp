// tests/test_r32_t06_chart_and_symbology.cpp
// R32-T06 WP05/WP06 — desktop/cartography delivery behaviour regressions.
//
// WP05 (chart specs): `validateChartSpec` only inspected `style.palette` when
// `style` was *already* an object, so a non-object `style` (string, number,
// boolean, array) was accepted and then threw from the renderer, because
// `renderInlineChart` reaches `chart["style"].isMember(...)`. A direct probe
// against the installed jsoncpp confirms the failure mode:
//
//   style="s"  -> THREW in Json::Value::find(begin, end):
//                 requires objectValue or nullValue
//   style=3 / true / [1] -> same
//   style=null -> tolerated (reads as "no style")
//
// The oracle here is the product contract, not jsoncpp's: illegal shapes must
// come back as a stable, contextual validation error, must not half-update an
// existing chart and must not leave a stray artifact behind; legal shapes
// (absent / null / empty object) must keep rendering real pixels.
//
// WP06 (symbology): `QgsSymbol::defaultSymbol()` returns nullptr for a layer
// whose geometry type is Null (a plain attribute table loaded as a vector
// layer, e.g. a CSV without a geometry column). Both renderer builders
// dereferenced that pointer *after* opening an undo transaction, so applying
// categorized/graduated symbology to a non-spatial table crashed the process.
// The oracle is a typed refusal with the table left viewable, its previous
// renderer intact and the undo stack unpolluted — plus, on real geometry, the
// normal categorized/graduated/undo/redo behaviour still holding.
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include "agent/cartography/chart_registry.h"
#include "agent/commands/workspace_commands.h"
#include "agent/spatial_tools/spatial_tool.h"
#include "agent/spatial_tools/spatial_tool_provider.h"

#include <qgis.h>
#include <qgsapplication.h>
#include <qgsfeature.h>
#include <qgsfield.h>
#include <qgsfields.h>
#include <qgsgeometry.h>
#include <qgsproject.h>
#include <qgssymbol.h>
#include <qgsvectorlayer.h>

#include <QFile>
#include <QImage>
#include <QString>
#include <QTemporaryDir>

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

int main( int argc, char *argv[] )
{
  QgsApplication application( argc, argv, true );
  QgsApplication::initQgis();
  const int result = Catch::Session().run( argc, argv );
  QgsProject::instance()->clear();
  QgsApplication::exitQgis();
  return result;
}

using namespace sicnu::agent::spatial_tools;

namespace
{

SpatialToolResult callTool( const std::string &name, const Json::Value &input )
{
  const auto tool = SpatialToolRegistry::instance().find( name );
  if ( !tool )
    return SpatialToolResult::failure( "tool not registered: " + name, "NOT_FOUND", "validation" );
  return ( *tool )->execute( input );
}

bool mentions( const std::string &haystack, const char *needle )
{
  return haystack.find( needle ) != std::string::npos;
}

// A legal bar chart: the shape the renderer actually supports.
Json::Value barChart()
{
  Json::Value chart( Json::objectValue );
  chart["kind"] = "bar";
  chart["title"] = "class areas";
  chart["width_px"] = 200;
  chart["height_px"] = 120;
  Json::Value binding( Json::objectValue );
  binding["mode"] = "inline";
  Json::Value data( Json::arrayValue );
  for ( const char *label : { "water", "forest", "urban" } )
  {
    Json::Value entry( Json::objectValue );
    entry["label"] = label;
    entry["value"] = 1.0 + static_cast<double>( data.size() );
    data.append( entry );
  }
  binding["data"] = data;
  chart["binding"] = binding;
  return chart;
}

// A vector layer that carries attributes but no geometry at all — what the
// desktop delivers when a user loads a plain CSV/DBF table.
QgsVectorLayer *makeAttributeTable( const QString &name )
{
  auto *layer = new QgsVectorLayer( QStringLiteral( "None" ), name, QStringLiteral( "memory" ) );
  QgsFields fields;
  fields.append( QgsField( QStringLiteral( "class" ), QMetaType::Type::QString ) );
  fields.append( QgsField( QStringLiteral( "area" ), QMetaType::Type::Double ) );
  layer->dataProvider()->addAttributes( fields.toList() );
  layer->updateFields();
  const char *classes[] = { "water", "forest", "urban", "farm" };
  for ( int i = 0; i < 8; ++i )
  {
    QgsFeature feature( layer->fields() );
    feature.setAttribute( QStringLiteral( "class" ), QString( classes[i % 4] ) );
    feature.setAttribute( QStringLiteral( "area" ), 10.0 * ( i + 1 ) );
    layer->dataProvider()->addFeature( feature );
  }
  layer->updateExtents();
  return layer;
}

QgsVectorLayer *makePolygonLayer( const QString &name )
{
  auto *layer = new QgsVectorLayer( QStringLiteral( "Polygon?crs=EPSG:4326" ), name,
                                    QStringLiteral( "memory" ) );
  QgsFields fields;
  fields.append( QgsField( QStringLiteral( "class" ), QMetaType::Type::QString ) );
  fields.append( QgsField( QStringLiteral( "area" ), QMetaType::Type::Double ) );
  layer->dataProvider()->addAttributes( fields.toList() );
  layer->updateFields();
  const char *classes[] = { "water", "forest", "urban", "farm" };
  for ( int i = 0; i < 16; ++i )
  {
    QgsFeature feature( layer->fields() );
    feature.setAttribute( QStringLiteral( "class" ), QString( classes[i % 4] ) );
    feature.setAttribute( QStringLiteral( "area" ), 10.0 * ( i + 1 ) );
    const QString wkt = QStringLiteral( "Polygon((%1 0, %1 1, %2 1, %2 0, %1 0))" )
                          .arg( i )
                          .arg( i + 1 );
    feature.setGeometry( QgsGeometry::fromWkt( wkt ) );
    layer->dataProvider()->addFeature( feature );
  }
  layer->updateExtents();
  return layer;
}

QString rendererType( const QgsVectorLayer *layer )
{
  return layer->renderer() ? layer->renderer()->type() : QString();
}

} // namespace

// ---------------------------------------------------------------- WP05 ------

TEST_CASE( "mechanism: a non-object style makes jsoncpp throw from isMember",
           "[r32t06][wp05][chart][mechanism]" )
{
  // The pre-fix renderer reached exactly this call with no guard between the
  // validator and the painter (validateChartSpec only inspected style.palette
  // when style was already an object). Keeping the probe in the suite means the
  // premise is re-checked on every run: if jsoncpp ever tolerates a non-object
  // argument, this goes red and the reasoning behind the guard must be revisited.
  Json::Value chart = barChart();
  chart["style"] = "thin";
  REQUIRE_THROWS_AS( chart["style"].isMember( "show_grid" ), Json::LogicError );
  // null is the documented "absent" shape and stays tolerable.
  chart["style"] = Json::Value();
  REQUIRE_NOTHROW( chart["style"].isMember( "show_grid" ) );
}

TEST_CASE( "chart specs reject a non-object style instead of throwing", "[r32t06][wp05][chart]" )
{
  using namespace sicnu::agent::cartography;
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  // Every illegal JSON shape, plus the observed failure mode for each.
  const struct
  {
    const char *json;
    const char *shape;
  } cases[] = {
    { "\"thin\"", "string" },
    { "7", "number" },
    { "true", "boolean" },
    { "[1,2]", "array" },
  };

  int index = 0;
  for ( const auto &testCase : cases )
  {
    INFO( "style shape: " << testCase.shape );
    Json::Value chart = barChart();
    Json::Value style;
    Json::CharReaderBuilder builder;
    std::string errs;
    std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    REQUIRE( reader->parse( testCase.json, testCase.json + std::strlen( testCase.json ), &style,
                            &errs ) );
    chart["style"] = style;

    // 1. Validation reports the shape instead of accepting it silently.
    const auto problems = validateChartSpec( chart );
    REQUIRE_FALSE( problems.empty() );
    CHECK( mentions( problems.front(), "style" ) );

    // 2. Registry refuses the chart and names the reason, without throwing.
    QString error;
    QString id;
    REQUIRE_NOTHROW( id = ChartRegistry::instance().createChart( chart, &error ) );
    CHECK( id.isEmpty() );
    CHECK_FALSE( error.isEmpty() );

    // 3. The render entry point fails cleanly and leaves no artifact behind.
    const QString path = dir.filePath( QStringLiteral( "chart_%1.png" ).arg( index ) );
    error.clear();
    bool rendered = true;
    REQUIRE_NOTHROW( rendered = renderChartToFile( chart, path, &error ) );
    CHECK_FALSE( rendered );
    CHECK_FALSE( error.isEmpty() );
    CHECK_FALSE( QFile::exists( path ) );
    ++index;
  }
  ChartRegistry::instance().clear();
}

TEST_CASE( "chart specs keep absent, null and empty styles renderable", "[r32t06][wp05][chart]" )
{
  using namespace sicnu::agent::cartography;
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  ChartRegistry::instance().clear();

  // absent / null / empty object are all "no style" — same contract, and each
  // must still produce a real image so the guard cannot pass by refusing all.
  const int variants = 3;
  for ( int variant = 0; variant < variants; ++variant )
  {
    INFO( "style variant index: " << variant );
    Json::Value chart = barChart();
    if ( variant == 1 )
      chart["style"] = Json::Value(); // explicit null
    else if ( variant == 2 )
      chart["style"] = Json::Value( Json::objectValue );

    CHECK( validateChartSpec( chart ).empty() );

    QString error;
    const QString id = ChartRegistry::instance().createChart( chart, &error );
    REQUIRE_FALSE( id.isEmpty() );
    CHECK( error.isEmpty() );

    const QString path = dir.filePath( QStringLiteral( "ok_%1.png" ).arg( variant ) );
    REQUIRE( renderChartToFile( chart, path, &error ) );
    CHECK( error.isEmpty() );
    const QImage image( path );
    REQUIRE_FALSE( image.isNull() );
    CHECK( image.width() == 200 );
    CHECK( image.height() == 120 );
    // "A file exists" is not evidence that a chart was drawn: count pixels that
    // differ from the white canvas so an empty/flat render cannot pass.
    int drawnPixels = 0;
    for ( int y = 0; y < image.height(); ++y )
      for ( int x = 0; x < image.width(); ++x )
        if ( image.pixel( x, y ) != qRgb( 255, 255, 255 ) )
          ++drawnPixels;
    CHECK( drawnPixels > 200 );
  }
  ChartRegistry::instance().clear();
}

TEST_CASE( "a rejected style patch keeps the stored chart untouched", "[r32t06][wp05][chart]" ){
  using namespace sicnu::agent::cartography;
  ChartRegistry::instance().clear();
  Json::Value chart = barChart();
  Json::Value goodStyle( Json::objectValue );
  goodStyle["show_grid"] = true;
  chart["style"] = goodStyle;

  QString error;
  const QString id = ChartRegistry::instance().createChart( chart, &error );
  REQUIRE_FALSE( id.isEmpty() );
  const Json::Value before = ChartRegistry::instance().find( id );
  REQUIRE_FALSE( before.isNull() );

  Json::Value patch( Json::objectValue );
  patch["style"] = "thin";
  REQUIRE_FALSE( ChartRegistry::instance().updateChart( id, patch, &error ) );
  CHECK_FALSE( error.isEmpty() );

  // No partial update: the stored spec is byte-identical to the accepted one,
  // and it still validates and renders.
  const Json::Value after = ChartRegistry::instance().find( id );
  CHECK( after.toStyledString() == before.toStyledString() );
  CHECK( validateChartSpec( after ).empty() );
  ChartRegistry::instance().clear();
}

TEST_CASE( "a non-object binding is still refused (differential control)",
           "[r32t06][wp05][chart]" )
{
  // #2084 named both `style` and `binding`. `binding` was already refused
  // before this change; keeping it as the control proves the new style guard
  // is not the only shape check in play and that the binding refusal cannot
  // regress while the style path is reworked.
  using namespace sicnu::agent::cartography;
  ChartRegistry::instance().clear();
  const char *shapes[] = { "\"x\"", "3", "true", "[1]" };
  for ( const char *shape : shapes )
  {
    INFO( "binding shape: " << shape );
    Json::Value chart = barChart();
    Json::Value binding;
    Json::CharReaderBuilder builder;
    std::string errs;
    std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    REQUIRE( reader->parse( shape, shape + std::strlen( shape ), &binding, &errs ) );
    chart["binding"] = binding;

    const auto problems = validateChartSpec( chart );
    REQUIRE_FALSE( problems.empty() );
    CHECK( mentions( problems.front(), "binding" ) );

    QString error;
    CHECK( ChartRegistry::instance().createChart( chart, &error ).isEmpty() );
    CHECK_FALSE( error.isEmpty() );
  }
  ChartRegistry::instance().clear();
}

// ---------------------------------------------------------------- WP06 ------

TEST_CASE( "mechanism: defaultSymbol returns null for a non-spatial layer",
           "[r32t06][wp06][symbology][mechanism]" )
{
  // Both builders called this with the layer's own geometry type and then
  // dereferenced the result *after* the undo transaction had been opened. The
  // probe pins the premise: a NoGeometry layer yields no default symbol.
  QgsProject::instance()->clear();
  auto *table = makeAttributeTable( QStringLiteral( "r32probe" ) );
  CHECK( table->geometryType() == Qgis::GeometryType::Null );
  CHECK( QgsSymbol::defaultSymbol( table->geometryType() ) == nullptr );
  // ...while real geometry still has one, so the guard cannot be "always refuse".
  auto *poly = makePolygonLayer( QStringLiteral( "r32probe-poly" ) );
  CHECK( QgsSymbol::defaultSymbol( poly->geometryType() ) != nullptr );
  delete poly;
  delete table;
}

TEST_CASE( "symbology refuses a non-spatial attribute table without dirtying state",
           "[r32t06][wp06][symbology]" )
{
  SpatialToolRegistry::instance().registerBuiltinTools();
  QgsProject::instance()->clear();

  auto *table = makeAttributeTable( QStringLiteral( "r32table" ) );
  REQUIRE_FALSE( table->isSpatial() );
  QgsProject::instance()->addMapLayer( table );

  const QString beforeRenderer = rendererType( table );
  auto &stack = sicnu::agent::commands::WorkspaceCommandStack::instance();
  const int beforeHistory = static_cast<int>( stack.history( 100 ).size() );

  Json::Value input( Json::objectValue );
  input["target"] = "r32table";

  SECTION( "categorized" )
  {
    Json::Value field = input;
    field["field"] = "class";
    SpatialToolResult result;
    REQUIRE_NOTHROW( result = callTool( "symbology:apply_categorical", field ) );
    CHECK_FALSE( result.success );
    CHECK( result.errorCode == "UNSUPPORTED" );
    CHECK( result.errorCategory == "validation" );
    CHECK( mentions( result.error, "geometry" ) );
    CHECK( mentions( result.error, "r32table" ) );
  }

  SECTION( "graduated" )
  {
    Json::Value field = input;
    field["field"] = "area";
    field["classes"] = 4;
    SpatialToolResult result;
    REQUIRE_NOTHROW( result = callTool( "symbology:apply_graduated", field ) );
    CHECK_FALSE( result.success );
    CHECK( result.errorCode == "UNSUPPORTED" );
    CHECK( mentions( result.error, "geometry" ) );
  }

  // The refusal is inert: the table keeps its renderer, stays readable and the
  // undo stack gained nothing.
  CHECK( rendererType( table ) == beforeRenderer );
  CHECK( rendererType( table ) != QStringLiteral( "categorizedSymbol" ) );
  CHECK( rendererType( table ) != QStringLiteral( "graduatedSymbol" ) );
  CHECK( static_cast<int>( stack.history( 100 ).size() ) == beforeHistory );
  CHECK( table->featureCount() == 8 );
  CHECK( table->fields().lookupField( QStringLiteral( "class" ) ) >= 0 );

  QgsProject::instance()->removeAllMapLayers();
}

TEST_CASE( "symbology still applies and replays on real geometry", "[r32t06][wp06][symbology]" )
{
  SpatialToolRegistry::instance().registerBuiltinTools();
  QgsProject::instance()->clear();

  auto *layer = makePolygonLayer( QStringLiteral( "r32poly" ) );
  REQUIRE( layer->isSpatial() );
  QgsProject::instance()->addMapLayer( layer );

  Json::Value input( Json::objectValue );
  input["target"] = "r32poly";
  input["field"] = "class";
  const auto applied = callTool( "symbology:apply_categorical", input );
  REQUIRE( applied.success );
  CHECK( applied.output["categories"].asInt() == 4 );
  CHECK( rendererType( layer ) == QStringLiteral( "categorizedSymbol" ) );

  Json::Value graduatedInput( Json::objectValue );
  graduatedInput["target"] = "r32poly";
  graduatedInput["field"] = "area";
  graduatedInput["classes"] = 4;
  const auto graduated = callTool( "symbology:apply_graduated", graduatedInput );
  REQUIRE( graduated.success );
  CHECK( graduated.output["classes"].asInt() == 4 );
  CHECK( rendererType( layer ) == QStringLiteral( "graduatedSymbol" ) );

  auto &stack = sicnu::agent::commands::WorkspaceCommandStack::instance();
  REQUIRE( stack.undo() );
  CHECK( rendererType( layer ) == QStringLiteral( "categorizedSymbol" ) );
  REQUIRE( stack.redo() );
  CHECK( rendererType( layer ) == QStringLiteral( "graduatedSymbol" ) );

  QgsProject::instance()->removeAllMapLayers();
}

TEST_CASE( "symbology undo after the layer is gone does not crash", "[r32t06][wp06][symbology]" )
{
  SpatialToolRegistry::instance().registerBuiltinTools();
  QgsProject::instance()->clear();

  auto *layer = makePolygonLayer( QStringLiteral( "r32gone" ) );
  QgsProject::instance()->addMapLayer( layer );

  Json::Value input( Json::objectValue );
  input["target"] = "r32gone";
  input["field"] = "class";
  REQUIRE( callTool( "symbology:apply_categorical", input ).success );

  QgsProject::instance()->removeAllMapLayers(); // deletes the layer
  auto &stack = sicnu::agent::commands::WorkspaceCommandStack::instance();
  REQUIRE_NOTHROW( stack.undo() );
  REQUIRE_NOTHROW( stack.redo() );
}
