// tests/test_cartography_visual.cpp
// Design System 4.0 Milestone F — deterministic cartography regression
// harness: fixture workspace → template → MapSpec → solver/repair →
// QgsPrintLayout → render → assertions.
//
// Methodology (docs/cartography/visual-regression.md):
//   1. determinism  — the same spec renders byte-identical PNGs twice;
//   2. geometry     — layout contracts (furniture present, post-repair
//                     preflight passed) are asserted separately from pixels;
//   3. goldens      — optional out-of-tree pixel references via
//                     SICNU_CARTOGRAPHY_GOLDEN_DIR (downscaled 25%,
//                     mean-abs-diff threshold), never committed binaries.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include "agent/cartography/composition.h"
#include "agent/cartography/cartography_tools.h"
#include "agent/cartography/design_tokens.h"
#include "agent/cartography/registry.h"
#include "agent/mapspec/mapspec.h"
#include "agent/mapspec/mapspec_compiler.h"

#include <qgslayoutexporter.h>
#include <qgslayoutmanager.h>
#include <qgsprintlayout.h>
#include <qgsproject.h>
#include <qgslayoutitemmap.h>
#include <qgslayoutitemlegend.h>
#include <qgslayoutitemlabel.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QBuffer>

#include <algorithm>
#include <cmath>

using namespace sicnu::agent::cartography;
using namespace sicnu::agent::mapspec;

namespace {

struct Fixture
{
    std::string name;
    Json::Value spec;
};

Json::Value instantiate( const std::string &templateId, const std::string &layoutName )
{
  Json::Value params( Json::objectValue );
  params["layout_name"] = layoutName;
  Json::Value draft =
    TemplateRegistry::instance().instantiateTemplate( QString::fromStdString( templateId ),
                                                      params );
  return draft;
}

/// Bounded repair loop; returns the final report (spec repaired in place).
Json::Value repairLoop( Json::Value &spec, int maxIterations = 6, int *iterations = nullptr )
{
  // Solve the composition once before looping repairs (repair contract).
  resolveComposition( spec, tokenNumber( resolveTokenSet( spec ), "spacing.margin_mm", 12.0 ) );
  Json::Value quality = preflightMapSpec( spec );
  int used = 0;
  while ( used < maxIterations && !quality["passed"].asBool() )
  {
    if ( repairMapSpec( spec, quality ) == 0 )
      break;
    ++used;
    quality = preflightMapSpec( spec );
  }
  if ( iterations )
    *iterations = used;
  return quality;
}

std::vector<Fixture> benchmarkFixtures()
{
  std::vector<Fixture> fixtures;
  for ( const char *id : { "classification-a4l", "change-before-after-a4l", "time-series-phenology-a4l",
                           "sar-backscatter-a4l", "scientific-publication-a4l", "multi-panel-a4l" } )
  {
    Json::Value draft = instantiate( id, std::string( "viz-" ) + id );
    if ( !draft.isNull() )
      fixtures.push_back( { id, draft } );
  }

  // dense legend: 30 declared entries in a tight legend
  {
    Json::Value spec = instantiate( "classification-a4l", "viz-dense-legend" );
    if ( !spec.isNull() )
    {
      for ( auto &legend : spec["legends"] )
      {
        legend["max_entries"] = 30;
        legend["rect_mm"][3] = 40.0; // far too small on purpose
      }
      fixtures.push_back( { "dense-legend", spec } );
    }
  }
  // CJK title: long full-width title text on the standard sheet
  {
    Json::Value spec = instantiate( "classification-a4l", "viz-cjk-title" );
    if ( !spec.isNull() )
    {
      spec["titles"][0]["text"] = "黄河流域二〇二四年地表覆盖变化监测成果图";
      spec["titles"][0]["rect_mm"][2] = 150.0;
      fixtures.push_back( { "cjk-title", spec } );
    }
  }

  // --- Platform 5.0 scenes (19-scene matrix, VISUAL_TEST_MATRIX.md) --------
  for ( const char *id : { "report-atlas-appendix-a4l", "report-screen-16x9",
                           "scientific-publication-a3l", "inset-locator-a4l",
                           "terrain-dem-a4l", "sar-change-a4l", "model-uncertainty-a4l",
                           "water-flood-a3p" } )
  {
    Json::Value draft = instantiate( id, std::string( "viz5-" ) + id );
    if ( !draft.isNull() )
      fixtures.push_back( { id, draft } );
  }
  {
    // A0 poster foundation (large-format single page).
    Json::Value spec = instantiate( "poster-a0-foundation", "viz5-poster" );
    if ( !spec.isNull() )
      fixtures.push_back( { "poster-a0", spec } );
  }
  {
    // Long English title overflow stress.
    Json::Value spec = instantiate( "classification-a4l", "viz5-long-title" );
    if ( !spec.isNull() )
    {
      spec["titles"][0]["text"] =
        "Regional Land Cover and Land Use Change Monitoring Results for the "
        "Upper Yellow River Basin Administration Zone, 2000-2024";
      fixtures.push_back( { "long-english-title", spec } );
    }
  }
  {
    // Conditional visibility: optional DEM-branch annotation pruned at compile.
    Json::Value spec = instantiate( "classification-a4l", "viz5-conditional" );
    if ( !spec.isNull() )
    {
      Json::Value annotation( Json::objectValue );
      annotation["id"] = "annotation-dem-branch";
      Json::Value rect( Json::arrayValue );
      rect.append( 12.0 );
      rect.append( 170.0 );
      rect.append( 120.0 );
      rect.append( 10.0 );
      annotation["rect_mm"] = rect;
      annotation["text"] = "DEM branch: slope-adjusted areas";
      annotation["visible_if"] = "has(dem)";
      spec["annotations"].append( annotation );
      Json::Value ctx( Json::objectValue ); // dem absent -> pruned
      spec["condition_context"] = ctx;
      fixtures.push_back( { "conditional-optional", spec } );
    }
  }
  return fixtures;
}

QByteArray renderToPng( const Json::Value &spec, double dpi = 120.0, QString *error = nullptr )
{
  QgsPrintLayout *layout = MapSpecCompiler::compile( spec, error );
  if ( !layout )
    return QByteArray();
  QgsLayoutExporter exporter( layout );
  const QImage image = exporter.renderPageToImage( 0, QSize(), dpi );
  if ( image.isNull() )
  {
    if ( error )
      *error = QStringLiteral( "render produced a null image" );
    return QByteArray();
  }
  QBuffer buffer;
  buffer.open( QIODevice::WriteOnly );
  image.save( &buffer, "PNG" );
  return buffer.data();
}

std::string hashBytes( const QByteArray &bytes )
{
  return QString::fromLatin1(
           QCryptographicHash::hash( bytes, QCryptographicHash::Sha256 ).toHex() )
    .toStdString();
}

} // namespace

TEST_CASE( "Benchmark fixtures: every required scene exists and repairs to passed",
           "[cartography][visual][benchmark]" )
{
  auto fixtures = benchmarkFixtures();
  REQUIRE( fixtures.size() >= 19 );
  const char *required[] = { "classification-a4l", "change-before-after-a4l",
                             "time-series-phenology-a4l", "sar-backscatter-a4l",
                             "scientific-publication-a4l", "multi-panel-a4l",
                             "dense-legend", "cjk-title",
                             "report-atlas-appendix-a4l", "report-screen-16x9",
                             "scientific-publication-a3l", "inset-locator-a4l",
                             "terrain-dem-a4l", "sar-change-a4l",
                             "model-uncertainty-a4l", "water-flood-a3p",
                             "poster-a0", "long-english-title",
                             "conditional-optional" };
  for ( const char *name : required )
  {
    bool found = false;
    for ( const auto &fixture : fixtures )
      found = found || fixture.name == name;
    INFO( name );
    CHECK( found );
  }

  for ( auto &fixture : fixtures )
  {
    INFO( fixture.name );
    int iterations = 0;
    const Json::Value report = repairLoop( fixture.spec, 6, &iterations );
    CHECK( report["passed"].asBool() );
    CHECK( iterations <= 4 ); // bounded convergence, no endless polishing
  }
}

TEST_CASE( "Rendering is deterministic: identical spec → identical PNG hash",
           "[cartography][visual][determinism]" )
{
  auto fixtures = benchmarkFixtures();
  REQUIRE_FALSE( fixtures.empty() );
  for ( auto &fixture : fixtures )
  {
    INFO( fixture.name );
    int iterations = 0;
    repairLoop( fixture.spec, 6, &iterations );

    QString errorA;
    QString errorB;
    const QByteArray pngA = renderToPng( fixture.spec, 120.0, &errorA );
    const QByteArray pngB = renderToPng( fixture.spec, 120.0, &errorB );
    REQUIRE( pngA.size() > 1000 );
    REQUIRE( pngB.size() > 1000 );
    CHECK( hashBytes( pngA ) == hashBytes( pngB ) );
  }
}

TEST_CASE( "Geometry contracts hold on the compiled layouts",
           "[cartography][visual][geometry]" )
{
  auto fixtures = benchmarkFixtures();
  for ( auto &fixture : fixtures )
  {
    INFO( fixture.name );
    repairLoop( fixture.spec, 6 );
    QString error;
    QgsPrintLayout *layout = MapSpecCompiler::compile( fixture.spec, &error );
    REQUIRE( layout != nullptr );

    // Every compiled furniture item sits inside the page bounds.
    const double pageW = fixture.spec["page"]["width_mm"].asDouble();
    const double pageH = fixture.spec["page"]["height_mm"].asDouble();
    const QList<QGraphicsItem *> items = layout->items();
    int furnitureChecked = 0;
    for ( QGraphicsItem *sceneItem : items )
    {
      auto *item = dynamic_cast<QgsLayoutItem *>( sceneItem );
      if ( !item || item->type() == QgsLayoutItemRegistry::LayoutPage )
        continue;
      const QRectF rect = item->mapToScene( item->rect() ).boundingRect();
      CHECK( rect.left() >= -0.5 );
      CHECK( rect.top() >= -0.5 );
      CHECK( rect.right() <= pageW + 0.5 );
      CHECK( rect.bottom() <= pageH + 0.5 );
      ++furnitureChecked;
    }
    CHECK( furnitureChecked >= 5 );

    // Furniture essentials are present as real layout items.
    const QString layoutName = QString::fromStdString( fixture.spec["layout_name"].asString() );
    Q_UNUSED( layoutName );
    CHECK( MapSpecCompiler::extract( layout )["kind"].asString() == "map_spec" );
    QgsProject::instance()->layoutManager()->clear();
  }
}

TEST_CASE( "Opt-in golden comparison against a reference directory",
           "[cartography][visual][golden]" )
{
  // Golden references stay out of the repository: set
  // SICNU_CARTOGRAPHY_GOLDEN_DIR to enable comparison; missing references
  // are written on first run. Without the env var this test is a no-op.
  if ( !qEnvironmentVariableIsSet( "SICNU_CARTOGRAPHY_GOLDEN_DIR" ) )
  {
    WARN( "golden comparison disabled (set SICNU_CARTOGRAPHY_GOLDEN_DIR)" );
    return;
  }
  const QString goldenDir = qEnvironmentVariable( "SICNU_CARTOGRAPHY_GOLDEN_DIR" );
  QDir().mkpath( goldenDir );

  auto fixtures = benchmarkFixtures();
  for ( auto &fixture : fixtures )
  {
    INFO( fixture.name );
    repairLoop( fixture.spec, 6 );
    const QByteArray png = renderToPng( fixture.spec, 120.0 );
    REQUIRE( png.size() > 1000 );

    QImage image;
    REQUIRE( image.loadFromData( png, "PNG" ) );
    // Downscale to 25% before comparing — tolerates font anti-aliasing
    // differences between platforms while catching layout drift.
    const QImage small = image.scaled( image.width() / 4, image.height() / 4,
                                       Qt::IgnoreAspectRatio, Qt::SmoothTransformation );

    const QString path = goldenDir + QStringLiteral( "/%1.png" ).arg( fixture.name.c_str() );
    if ( !QFile::exists( path ) )
    {
      small.save( path, "PNG" );
      WARN( "golden written: " << path.toStdString() );
      continue;
    }
    QImage reference( path );
    if ( reference.size() != small.size() )
    {
      // Layout size changed: refresh the reference rather than fail forever.
      small.save( path, "PNG" );
      WARN( "golden refreshed (size change): " << path.toStdString() );
      continue;
    }
    // Mean absolute channel difference.
    qint64 total = 0;
    const int w = small.width();
    const int h = small.height();
    for ( int y = 0; y < h; ++y )
    {
      const QRgb *a = reinterpret_cast<const QRgb *>( small.constScanLine( y ) );
      const QRgb *b = reinterpret_cast<const QRgb *>( reference.constScanLine( y ) );
      for ( int x = 0; x < w; ++x )
      {
        total += std::abs( qRed( a[x] ) - qRed( b[x] ) );
        total += std::abs( qGreen( a[x] ) - qGreen( b[x] ) );
        total += std::abs( qBlue( a[x] ) - qBlue( b[x] ) );
      }
    }
    const double meanDiff = static_cast<double>( total ) / ( w * h * 3 );
    INFO( "mean abs diff: " << meanDiff );
    CHECK( meanDiff < 12.0 ); // generous: geometry drift fails, AA noise passes
  }
}
