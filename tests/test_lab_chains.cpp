// tests/test_lab_chains.cpp
// D3 lab content expansion track — lab-chain integrity tests.
//
// Pins the structural guarantees the four new labs (docs/labs/lab8..11)
// promise, without depending on generated fixtures:
//   1. every pipeline JSON parses and every operator id resolves in the
//      RSOperatorRegistry (completion-gate check, executable);
//   2. every LabSpec validates against the key contract and its declared
//      artifacts (pipeline/data-spec/grading intent) exist in-tree;
//   3. the lab11 MapSpec document validates, passes the cartography
//      compliance preflight (title/legend/scale bar/north arrow/source
//      note) and compiles to a QGIS print layout offscreen.
//
// Deliberately a standalone target: the platform-wide test_mapspec
// aggregate is owned by the visual-cartography track (READINESS.md:38) —
// this file must stay green regardless of that suite's state.
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include "operators/rs/rs_operators_init.h"
#include "operators/framework/rs_operator_registry.h"
#include "agent/cartography/quality.h"
#include "agent/mapspec/mapspec.h"
#include "agent/mapspec/mapspec_compiler.h"

#include <qgsapplication.h>
#include <qgsprintlayout.h>
#include <qgsproject.h>

#include <QDir>
#include <QFile>
#include <QString>

#include <fstream>
#include <set>
#include <string>
#include <vector>

#ifndef CMAKE_SOURCE_DIR
#define CMAKE_SOURCE_DIR "."
#endif

int main( int argc, char *argv[] )
{
  qputenv( "SICNU_CARTOGRAPHY_DIR", SICNU_CARTOGRAPHY_DATA_DIR );
  QgsApplication application( argc, argv, false );
  QgsApplication::initQgis();
  const int result = Catch::Session().run( argc, argv );
  QgsProject::instance()->clear();
  QgsApplication::exitQgis();
  return result;
}

using namespace sicnu::agent::mapspec;

namespace
{

#ifndef SICNU_CARTOGRAPHY_DATA_DIR
#define SICNU_CARTOGRAPHY_DATA_DIR "data/cartography"
#endif

std::string repoPath( const char *relative )
{
  return std::string( CMAKE_SOURCE_DIR ) + "/" + relative;
}

Json::Value loadJson( const std::string &path, bool *ok = nullptr )
{
  std::ifstream stream( path );
  Json::Value value;
  const bool parsed = stream.is_open() && stream >> value;
  if ( ok )
    *ok = parsed;
  return value;
}

const char *const kLabIds[] = {
  "temporal_analysis", "sar_processing",
  "hyperspectral_analysis", "cartographic_mapping",
};

const char *const kPipelines[] = {
  "data/labs/pipelines/lab8_temporal_analysis.pipeline.json",
  "data/labs/pipelines/lab9_sar_processing.pipeline.json",
  "data/labs/pipelines/lab10_hyperspectral_analysis.pipeline.json",
  "data/labs/pipelines/lab11_cartographic_mapping.pipeline.json",
};

const char *const kLabSpecs[] = {
  "data/labs/lab8_temporal_analysis.labspec.json",
  "data/labs/lab9_sar_processing.labspec.json",
  "data/labs/lab10_hyperspectral_analysis.labspec.json",
  "data/labs/lab11_cartographic_mapping.labspec.json",
};

const char *const kDataSpecs[] = {
  "data/labs/data-specs/lab8_temporal_analysis.json",
  "data/labs/data-specs/lab9_sar_processing.json",
  "data/labs/data-specs/lab10_hyperspectral_analysis.json",
  "data/labs/data-specs/lab11_cartographic_mapping.json",
};

const char *const kGradingIntents[] = {
  "data/labs/grading/lab8_temporal_analysis.intent.json",
  "data/labs/grading/lab9_sar_processing.intent.json",
  "data/labs/grading/lab10_hyperspectral_analysis.intent.json",
  "data/labs/grading/lab11_cartographic_mapping.intent.json",
};

} // namespace

TEST_CASE( "Lab pipelines reference registered operators only", "[lab_chains][registry]" )
{
  sicnu::operators::initBuiltinRsOperators();
  auto &registry = sicnu::operators::RSOperatorRegistry::instance();

  for ( const char *pipelinePath : kPipelines )
  {
    bool parsed = false;
    const Json::Value pipeline = loadJson( repoPath( pipelinePath ), &parsed );
    REQUIRE( parsed );
    REQUIRE( pipeline.isMember( "steps" ) );
    REQUIRE( pipeline["steps"].isArray() );
    REQUIRE( pipeline["steps"].size() >= 3 );

    for ( const auto &step : pipeline["steps"] )
    {
      const std::string op = step["operator"].asString();
      INFO( pipelinePath << " step " << step["id"].asString() << " -> " << op );
      CHECK( registry.create( op ) != nullptr );
    }
  }
}

TEST_CASE( "LabSpecs and their declared artifacts are consistent", "[lab_chains][labspec]" )
{
  for ( const char *labSpecPath : kLabSpecs )
  {
    bool parsed = false;
    const Json::Value spec = loadJson( repoPath( labSpecPath ), &parsed );
    REQUIRE( parsed );
    CHECK( spec["schema"].asString() == "sicnu.labspec.v1" );
    CHECK_FALSE( spec["id"].asString().empty() );
    CHECK( spec["data"]["offline"].asBool() );

    // Declared artifacts must exist in-tree (zero drift on paths).
    CHECK( QFile::exists( QString::fromStdString( repoPath(
      spec["pipeline"]["ref"].asString().c_str() ) ) ) );
    CHECK( QFile::exists( QString::fromStdString( repoPath(
      spec["data"]["spec_ref"].asString().c_str() ) ) ) );
    CHECK( QFile::exists( QString::fromStdString( repoPath(
      spec["grading_ref"]["intent_ref"].asString().c_str() ) ) ) );
    CHECK( spec["operators"].size() >= 1 );
    CHECK( spec["questions"].size() >= 2 );
    CHECK( spec["expected_results"].size() >= 1 );
    CHECK( spec["principles"].size() >= 1 );
  }
}

TEST_CASE( "Lab track artifact sets are complete per lab", "[lab_chains][inventory]" )
{
  for ( const char *path : kPipelines )
    CHECK( QFile::exists( repoPath( path ) ) );
  for ( const char *path : kDataSpecs )
    CHECK( QFile::exists( repoPath( path ) ) );
  for ( const char *path : kGradingIntents )
    CHECK( QFile::exists( repoPath( path ) ) );

  // Grading intents are intent-only: the grader is owned by D4.
  for ( const char *path : kGradingIntents )
  {
    bool parsed = false;
    const Json::Value intent = loadJson( repoPath( path ), &parsed );
    REQUIRE( parsed );
    CHECK( intent["grader_owner"].asString().find( "D4" ) != std::string::npos );
    CHECK( intent["assertions"].size() >= 4 );
  }
}

TEST_CASE( "Lab11 MapSpec validates and passes compliance preflight",
           "[lab_chains][cartography]" )
{
  bool parsed = false;
  const Json::Value spec = loadJson(
    repoPath( "data/labs/mapspecs/lab11_thematic_map.mapspec.json" ), &parsed );
  REQUIRE( parsed );
  CHECK( spec["spec_version"].asInt() == 5 );

  const std::vector<std::string> problems = validateMapSpec( spec );
  for ( const auto &problem : problems )
    INFO( "validateMapSpec: " << problem );
  CHECK( problems.empty() );

  const Json::Value report = sicnu::agent::cartography::preflightMapSpec( spec );
  CHECK( report["passed"].asBool() );

  static const char *const kComplianceCodes[] = {
    "MAP_MISSING_TITLE", "MAP_MISSING_LEGEND", "MAP_MISSING_SCALE_BAR",
    "MAP_MISSING_NORTH_ARROW", "MAP_MISSING_SOURCE_NOTE",
  };
  const Json::Value issues = report["issues"];
  for ( const auto &issue : issues )
  {
    const std::string code = issue["code"].asString();
    for ( const char *banned : kComplianceCodes )
    {
      INFO( "compliance code reported: " << code );
      CHECK( code != banned );
    }
  }

  QString error;
  QgsPrintLayout *layout = MapSpecCompiler::compile( spec, &error );
  INFO( "compile error: " << error.toStdString() );
  CHECK( layout != nullptr );
  if ( layout )
    QgsProject::instance()->layoutManager()->removeLayout( layout );
}
