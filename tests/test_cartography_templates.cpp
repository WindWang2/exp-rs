// tests/test_cartography_templates.cpp
// Design System 4.0 Milestone C — template inheritance, page-family
// variants, the template drift gate (instantiate → validate → repair →
// passed), the agent acceptance intents, and the catalog index drift test.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include "agent/cartography/composition.h"
#include "agent/cartography/cartography_tools.h"
#include "agent/cartography/design_tokens.h"
#include "agent/cartography/registry.h"
#include "agent/mapspec/mapspec.h"
#include "agent/mapspec/mapspec_compiler.h"

#include <qgsapplication.h>
#include <qgslayoutmanager.h>
#include <qgsprintlayout.h>
#include <qgsproject.h>

#include <QDir>
#include <QFile>
#include <QString>
#include <QStringList>

#ifndef SICNU_CARTOGRAPHY_DATA_DIR
#define SICNU_CARTOGRAPHY_DATA_DIR "data/cartography"
#endif

using namespace sicnu::agent::cartography;
using namespace sicnu::agent::mapspec;

namespace {

Json::Value readJson( const QString &path )
{
  QFile file( path );
  if ( !file.open( QIODevice::ReadOnly ) )
    return Json::Value();
  const QByteArray bytes = file.readAll();
  Json::Value out;
  Json::Reader reader;
  reader.parse( std::string( bytes.constData(), bytes.size() ), out );
  return out;
}

void writeJson( const QString &path, const Json::Value &value )
{
  QFile file( path );
  if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
    return;
  const std::string serialized = value.toStyledString();
  file.write( serialized.data(), static_cast<qint64>( serialized.size() ) );
}

bool hasIssue( const Json::Value &report, const std::string &code )
{
  for ( const auto &issue : report["issues"] )
    if ( issue.isObject() && issue["code"].asString() == code )
      return true;
  return false;
}

/// The full agent loop: instantiate → preflight → repair (bounded) → verdict.
Json::Value composeRepairLoop( const Json::Value &draft, int maxIterations = 6 )
{
  Json::Value spec = draft;
  // Solve the composition once before looping repairs (repair contract).
  resolveComposition( spec, tokenNumber( resolveTokenSet( spec ), "spacing.margin_mm", 12.0 ) );
  Json::Value quality = preflightMapSpec( spec );
  int iterations = 0;
  while ( iterations < maxIterations && !quality["passed"].asBool() )
  {
    const int repairs = repairMapSpec( spec, quality );
    if ( repairs == 0 )
      break;
    ++iterations;
    quality = preflightMapSpec( spec );
  }
  Json::Value out( Json::objectValue );
  out["spec"] = spec;
  out["quality"] = quality;
  out["iterations"] = iterations;
  return out;
}

} // namespace

TEST_CASE( "Template inheritance resolves extends chains deterministically",
           "[cartography][templates][inheritance]" )
{
  auto &registry = TemplateRegistry::instance();
  CHECK( registry.loadProblems().isEmpty() );

  // Page variant inherits style/recs/tasks from the A4L base but carries
  // its own page + re-laid-out slots.
  const Json::Value base = registry.find( QLatin1String( "classification-a4l" ) );
  REQUIRE( !base.isNull() );
  CHECK( base["page"]["width_mm"].asDouble() == Catch::Approx( 297.0 ) );
  const Json::Value variant = registry.find( QLatin1String( "classification-a4p" ) );
  REQUIRE( !variant.isNull() );
  CHECK( variant["extends"].asString() == "classification-a4l" );
  CHECK( variant["page"]["width_mm"].asDouble() == Catch::Approx( 210.0 ) );
  CHECK( variant["page"]["height_mm"].asDouble() == Catch::Approx( 297.0 ) );
  CHECK( variant["style"]["token_set"].asString() == base["style"]["token_set"].asString() );
  CHECK( variant["suitable_tasks"].size() == base["suitable_tasks"].size() );
  // slots were re-laid-out: map frame moved left for the portrait page
  REQUIRE( variant["slots"].size() == base["slots"].size() );
  CHECK( variant["slots"][0]["rect_mm"][2].asDouble() <
         base["slots"][0]["rect_mm"][2].asDouble() );
}

TEST_CASE( "Programmatic extends cycles are detected, not hung",
           "[cartography][templates][inheritance]" )
{
  // Write a cyclic extends chain into a temp catalog and load it through the
  // real directory path; the cycle must surface in loadProblems and the
  // templates must be absent, then the shipped catalog is restored.
  TemplateRegistry &registry = TemplateRegistry::instance();
  const QString tempDir = QDir::temp().filePath( QStringLiteral( "sicnu-template-cycle" ) );
  QDir().mkpath( tempDir );
  Json::Value a( Json::objectValue );
  a["id"] = "cycle-a";
  a["extends"] = "cycle-b";
  a["slots"] = Json::Value( Json::arrayValue );
  Json::Value b( Json::objectValue );
  b["id"] = "cycle-b";
  b["extends"] = "cycle-a";
  b["slots"] = Json::Value( Json::arrayValue );
  writeJson( tempDir + "/cycle-a.json", a );
  writeJson( tempDir + "/cycle-b.json", b );

  registry.setDirectory( tempDir );
  registry.reload();
  CHECK_FALSE( registry.loadProblems().isEmpty() );
  CHECK( registry.find( QLatin1String( "cycle-a" ) ).isNull() );
  CHECK( registry.find( QLatin1String( "cycle-b" ) ).isNull() );

  // Unknown parents are recorded too.
  Json::Value orphan( Json::objectValue );
  orphan["id"] = "orphan";
  orphan["extends"] = "ghost-base";
  orphan["slots"] = Json::Value( Json::arrayValue );
  writeJson( tempDir + "/orphan.json", orphan );
  registry.reload();
  bool ghostReported = false;
  for ( const QString &problem : registry.loadProblems() )
    ghostReported = ghostReported || problem.contains( QLatin1String( "ghost-base" ) );
  CHECK( ghostReported );

  // Restore the shipped catalog for the remaining tests.
  registry.setDirectory( QStringLiteral( SICNU_CARTOGRAPHY_DATA_DIR ) );
  registry.reload();
  CHECK( registry.loadProblems().isEmpty() );
  CHECK_FALSE( registry.find( QLatin1String( "classification-a4l" ) ).isNull() );
}

TEST_CASE( "Template drift: every shipped template instantiates, validates, repairs to passed",
           "[cartography][templates][drift]" )
{
  auto &registry = TemplateRegistry::instance();
  const Json::Value templates = registry.templates();
  REQUIRE( templates.isArray() );
  CHECK( templates.size() >= 50 ); // expanded from the 8 seed templates

  std::vector<std::string> failures;
  int passed = 0;
  for ( const auto &tmpl : templates )
  {
    const std::string id = tmpl["id"].asString();
    Json::Value params( Json::objectValue );
    params["title"] = "Drift check";
    QString error;
    Json::Value draft = registry.instantiateTemplate( QString::fromStdString( id ), params, &error );
    if ( draft.isNull() )
    {
      failures.push_back( id + ": instantiate failed: " + error.toStdString() );
      continue;
    }
    const auto problems = validateMapSpec( draft );
    if ( !problems.empty() )
    {
      failures.push_back( id + ": invalid draft: " + problems.front() );
      continue;
    }
    const Json::Value loop = composeRepairLoop( draft );
    if ( !loop["quality"]["passed"].asBool() )
    {
      std::string codes;
      for ( const auto &issue : loop["quality"]["issues"] )
        codes += issue["code"].asString() + "[" + issue["item_id"].asString() + "]/" +
                 issue["message"].asString() + " ";
      failures.push_back( id + ": preflight not passed after repair (score " +
                          std::to_string( loop["quality"]["quality_score"].asInt() ) + "): " +
                          codes );
      continue;
    }
    // The resolved draft must compile into a layout.
    QString compileError;
    QgsPrintLayout *layout = MapSpecCompiler::compile( loop["spec"], &compileError );
    if ( !layout )
      failures.push_back( id + ": compile failed: " + compileError.toStdString() );
    else
      ++passed;
    QgsProject::instance()->layoutManager()->clear();
  }
  for ( const auto &failure : failures )
    WARN( failure );
  CHECK( failures.empty() );
  CHECK( passed == static_cast<int>( templates.size() ) );
}

TEST_CASE( "Acceptance intent: publication land-cover map with locator and class chart",
           "[cartography][acceptance]" )
{
  // "Create a publication-quality land-cover result map with title, grouped
  // class legend, scale bar, north arrow, source note, inset locator and a
  // class-area chart." — discover → bind → compose → preflight → repair.
  auto &registry = TemplateRegistry::instance();
  bool discovered = false;
  for ( const auto &tmpl : registry.templates() )
    for ( const auto &task : tmpl["suitable_tasks"] )
      discovered = discovered || task.asString() == "land-cover";
  CHECK( discovered );

  Json::Value params( Json::objectValue );
  params["layout_name"] = "acceptance-land-cover";
  params["title"] = "研究区地表覆盖 / Land Cover";
  params["source_note"] = "数据来源: S2 · 2025-06";
  const Json::Value draft = registry.instantiateTemplate( QLatin1String( "land-cover-a4l" ),
                                                          params );
  REQUIRE( !draft.isNull() );

  const Json::Value loop = composeRepairLoop( draft );
  CHECK( loop["quality"]["passed"].asBool() );

  const Json::Value &spec = loop["spec"];
  CHECK( spec["titles"].size() >= 1 );
  CHECK( spec["legends"].size() >= 1 );
  CHECK( spec["scale_bars"].size() >= 1 );
  CHECK( spec["north_arrows"].size() >= 1 );
  CHECK( spec["source_notes"].size() >= 1 );
  CHECK( spec["inset_maps"].size() == 1 );
  bool hasClassChart = false;
  for ( const auto &chart : spec["charts"] )
    hasClassChart = hasClassChart || chart["chart"]["kind"].asString() == "bar";
  CHECK( hasClassChart );

  // The token set travels from the template into the draft.
  CHECK( spec["style"]["token_set"].asString() == "scientific-light" );
}

TEST_CASE( "Acceptance intent: before/after change page with transition legend",
           "[cartography][acceptance]" )
{
  // "Create a before/after change-detection page with two synchronized map
  // frames, a transition legend, change statistics and uncertainty note."
  auto &registry = TemplateRegistry::instance();
  const Json::Value draft = registry.instantiateTemplate( QLatin1String( "change-before-after-a4l" ),
                                                          Json::Value() );
  REQUIRE( !draft.isNull() );

  const Json::Value loop = composeRepairLoop( draft );
  CHECK( loop["quality"]["passed"].asBool() );

  const Json::Value &spec = loop["spec"];
  CHECK( spec["map_frames"].size() == 2 );
  bool hasTransitionLegend = false;
  for ( const auto &legend : spec["legends"] )
    hasTransitionLegend = hasTransitionLegend ||
                          legend["semantic_role"].asString() == "legend.change";
  CHECK( hasTransitionLegend );
  CHECK( spec["charts"].size() >= 1 ); // change statistics (matrix)
  bool hasUncertaintyNote = false;
  for ( const auto &note : spec["source_notes"] )
    hasUncertaintyNote = hasUncertaintyNote ||
                         note["semantic_role"].asString() == "note.uncertainty" ||
                         note["id"].asString().find( "uncertainty" ) != std::string::npos;
  CHECK( hasUncertaintyNote );
}

TEST_CASE( "Catalog index drift: generated index matches the committed file",
           "[cartography][catalog]" )
{
  // Other tests register programmatic descriptors into the global
  // registries; the index describes the *shipped* catalog, so reload from
  // the data directory first.
  ComponentRegistry::instance().setDirectory( QStringLiteral( SICNU_CARTOGRAPHY_DATA_DIR ) );
  ComponentRegistry::instance().reload();
  TemplateRegistry::instance().setDirectory( QStringLiteral( SICNU_CARTOGRAPHY_DATA_DIR ) );
  TemplateRegistry::instance().reload();
  TokenSetRegistry::instance().setDirectory( QStringLiteral( SICNU_CARTOGRAPHY_DATA_DIR ) );
  TokenSetRegistry::instance().reload();

  const Json::Value generated = buildCatalogIndex();
  REQUIRE( generated["kind"].asString() == "cartography_catalog_index" );

  const QString path = QDir( SICNU_CARTOGRAPHY_DATA_DIR ).filePath( "index.json" );
  const Json::Value committed = readJson( path );

  if ( qEnvironmentVariableIsSet( "SICNU_CARTOGRAPHY_REGENERATE_INDEX" ) )
    writeJson( path, generated );

  REQUIRE( !committed.isNull() );
  CHECK( committed.toStyledString() == generated.toStyledString() );
  CHECK( generated["templates"].size() >= 50 );
  CHECK( generated["components"].size() >= 40 );
  CHECK( generated["token_sets"].size() >= 2 );

  // Gallery doc drift: every shipped template and component id must be
  // listed in docs/cartography/gallery.md (the data dir points into the
  // source tree, so the docs path is stable regardless of the build dir).
  const QString galleryPath =
    QDir( QDir::cleanPath( SICNU_CARTOGRAPHY_DATA_DIR + QStringLiteral( "/../.." ) ) )
      .filePath( QStringLiteral( "docs/cartography/gallery.md" ) );
  QFile gallery( galleryPath );
  if ( gallery.open( QIODevice::ReadOnly ) )
  {
    const QString markdown = QString::fromUtf8( gallery.readAll() );
    for ( const auto &tmpl : generated["templates"] )
      CHECK( markdown.contains( QString::fromStdString( tmpl["id"].asString() ) ) );
    for ( const auto &component : generated["components"] )
      CHECK( markdown.contains( QString::fromStdString( component["id"].asString() ) ) );
  }
  else
  {
    WARN( "gallery doc not found: " << galleryPath.toStdString() );
  }
}
