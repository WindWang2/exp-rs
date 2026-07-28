// tests/test_agent_context_resolver.cpp
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "agent/agent_context_resolver.h"
#include "data/data_manager.h"

#include <QCoreApplication>

using namespace sicnu::agent;
using namespace sicnu::data;

static void ensureQtApp()
{
  if ( !QCoreApplication::instance() )
  {
    static int argc = 1;
    static char appName[] = "test_agent_context_resolver";
    static char *argv[] = { appName, nullptr };
    new QCoreApplication( argc, argv );
  }
}

TEST_CASE( "AgentContextResolver generates workspace snapshot and formats prompt", "[agent][context]" )
{
  ensureQtApp();

  DataManager dataMgr;

  // Register a raster source
  RegisterRequest rasterReq;
  rasterReq.source.canonicalSource = QStringLiteral( "/tmp/landsat_sample.tif" );
  rasterReq.persistence = PersistencePolicy::TaskTemporary;
  auto res1 = dataMgr.registerSource( rasterReq );
  REQUIRE( res1.reusedExisting == false );

  // Register a vector source
  RegisterRequest vectorReq;
  vectorReq.source.canonicalSource = QStringLiteral( "/tmp/boundaries.geojson" );
  vectorReq.persistence = PersistencePolicy::ProjectPersistent;
  auto res2 = dataMgr.registerSource( vectorReq );
  REQUIRE( res2.reusedExisting == false );

  QJsonObject snapshot = AgentContextResolver::buildContextSnapshot( &dataMgr, nullptr );
  REQUIRE( snapshot.contains( QStringLiteral( "assets" ) ) );

  QJsonArray assets = snapshot[QStringLiteral( "assets" )].toArray();
  REQUIRE( assets.size() == 2 );

  QString prompt = AgentContextResolver::formatSystemContextPrompt( snapshot );
  REQUIRE( prompt.contains( QStringLiteral( "[WORKSPACE CONTEXT]" ) ) );
  REQUIRE( prompt.contains( QStringLiteral( "/tmp/landsat_sample.tif" ) ) );
  REQUIRE( prompt.contains( QStringLiteral( "/tmp/boundaries.geojson" ) ) );
}
