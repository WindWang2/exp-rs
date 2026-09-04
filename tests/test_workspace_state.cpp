// tests/test_workspace_state.cpp
// Phase B — Workspace Understanding 3.0: stable entity ids + WorkspaceState doc.
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "agent/contracts/spatial_contracts.h"
#include "agent/workspace_state.h"
#include "data/asset_types.h"
#include "data/data_manager.h"
#include "data/data_result.h"
#include "data/internal/source_provider.h"
#include "data/internal/source_provider_registry.h"
#include "data/source_descriptor.h"

#include <qgsapplication.h>
#include <qgsproject.h>

#include <memory>

int main( int argc, char *argv[] )
{
  QgsApplication application( argc, argv, true );
  QgsApplication::initQgis();
  sicnu::agent::WorkspaceEntityRegistry::instance().clearInProcess();
  const int result = Catch::Session().run( argc, argv );
  QgsProject::instance()->clear();
  QgsApplication::exitQgis();
  return result;
}

using sicnu::data::AssetKind;
using sicnu::data::AssetState;
using sicnu::data::AssetCapability;
using sicnu::data::BandRole;
using sicnu::data::DataManager;
using sicnu::data::RasterBandStructure;
using sicnu::data::RasterStructure;
using sicnu::data::RegisterRequest;
using sicnu::data::Result;
using sicnu::data::SourceDescriptor;
using sicnu::data::StorageKind;
using sicnu::data::internal::ResolvedSource;
using sicnu::data::internal::SourceProvider;
using sicnu::data::internal::SourceProviderRegistry;

namespace {

class WsRasterProvider : public SourceProvider
{
  public:
    bool supports( const SourceDescriptor &source ) const override
    {
      return source.providerKey == QStringLiteral( "ws-state-test" );
    }

    Result<ResolvedSource> resolve( const SourceDescriptor &source ) const override
    {
      RasterStructure structure;
      structure.driverName = QStringLiteral( "GTiff" );
      structure.width = 128;
      structure.height = 64;
      structure.bandCount = 4;
      structure.crsWkt = QStringLiteral( "EPSG:32648" );
      const auto addBand = [ &structure ]( BandRole role ) {
        RasterBandStructure band;
        band.number = structure.bands.size() + 1;
        band.dataType = QStringLiteral( "UInt16" );
        band.role = role;
        structure.bands.append( band );
      };
      addBand( BandRole::NIR );
      addBand( BandRole::Red );
      addBand( BandRole::Green );
      addBand( BandRole::Blue );

      return Result<ResolvedSource>::success(
        ResolvedSource{ AssetKind::Raster,
                        AssetState::Ready,
                        AssetCapability::Renderable | AssetCapability::ReadablePixels,
                        StorageKind::Memory,
                        source.canonicalSource,
                        QString(),
                        QString(),
                        structure } );
    }
};

} // namespace

TEST_CASE( "WorkspaceEntityRegistry assigns stable short ids", "[agent][workspace_state]" )
{
  auto &registry = sicnu::agent::WorkspaceEntityRegistry::instance();
  registry.clearInProcess();

  const QString first = registry.idFor( QStringLiteral( "asset" ), QStringLiteral( "/data/a.tif" ) );
  const QString second = registry.idFor( QStringLiteral( "asset" ), QStringLiteral( "/data/b.tif" ) );
  REQUIRE( first == QStringLiteral( "asset-1" ) );
  REQUIRE( second == QStringLiteral( "asset-2" ) );

  // Same natural key → same id (stability across captures).
  CHECK( registry.idFor( QStringLiteral( "asset" ), QStringLiteral( "/data/a.tif" ) ) == first );

  // Reverse lookup.
  CHECK( registry.naturalKeyFor( first ) == QStringLiteral( "/data/a.tif" ) );
  CHECK( registry.naturalKeyFor( QStringLiteral( "asset-99" ) ).isEmpty() );

  // Kinds keep independent counters.
  const QString layer = registry.idFor( QStringLiteral( "layer" ), QStringLiteral( "uuid-x" ) );
  CHECK( layer == QStringLiteral( "layer-1" ) );

  // Empty inputs are rejected.
  CHECK( registry.idFor( QString(), QStringLiteral( "k" ) ).isEmpty() );
}

TEST_CASE( "buildWorkspaceState produces a structured workspace document",
           "[agent][workspace_state]" )
{
  sicnu::agent::WorkspaceEntityRegistry::instance().clearInProcess();

  SourceProviderRegistry providers;
  providers.add( std::make_unique<WsRasterProvider>() );
  auto manager = providers.createDataManager();
  REQUIRE( manager );

  RegisterRequest request;
  request.source.providerKey = QStringLiteral( "ws-state-test" );
  request.source.canonicalSource = QStringLiteral( "/tmp/ws_state_scene.tif" );
  const auto registered = manager->registerSource( request );
  REQUIRE( !registered.assetId.isNull() );

  // No canvas: headless workspace state must still be produced.
  const Json::Value doc = sicnu::agent::buildWorkspaceState( manager.get(), nullptr );
  REQUIRE( doc["kind"].asString() == "workspace_state" );
  REQUIRE( doc["schema_version"].asString() == std::string( sicnu::agent::contracts::kContractsSchemaVersion ) );

  REQUIRE( doc["assets"].isArray() );
  REQUIRE( doc["assets"].size() == 1 );
  const Json::Value &asset = doc["assets"][0];
  CHECK( asset["id"].asString() == "asset-1" );
  CHECK( asset["asset_id"].asString() == registered.assetId.toString().toStdString() );
  CHECK( asset["kind"].asString() == "raster" );
  CHECK( asset["band_count"].asInt() == 4 );
  REQUIRE( asset["band_roles"].size() == 4 );
  CHECK( asset["band_roles"][0].asString() == "nir" );

  // Document carries every Phase-B surface, bounded by construction.
  CHECK( doc.isMember( "layers" ) );
  CHECK( doc.isMember( "temporal_collections" ) );
  CHECK( doc.isMember( "layouts" ) );
  CHECK( doc.isMember( "models" ) );
  CHECK( doc.isMember( "running_tasks" ) );
  CHECK( doc.isMember( "recent_outputs" ) );
  CHECK( doc.isMember( "workflow_runs" ) );
  CHECK( doc.isMember( "charts" ) );

  // Stability: a second capture resolves identical entity ids.
  const Json::Value doc2 = sicnu::agent::buildWorkspaceState( manager.get(), nullptr );
  REQUIRE( doc2["assets"].size() == 1 );
  CHECK( doc2["assets"][0]["id"].asString() == asset["id"].asString() );
}

TEST_CASE( "workflow runs provider seam feeds the workspace document",
           "[agent][workspace_state]" )
{
  Json::Value runs( Json::arrayValue );
  Json::Value run( Json::objectValue );
  run["run_id"] = "run-1";
  run["state"] = "Running";
  runs.append( run );
  sicnu::agent::setWorkflowRunsProvider( [ runs ] { return runs; } );

  const Json::Value doc = sicnu::agent::buildWorkspaceState( nullptr, nullptr );
  REQUIRE( doc["workflow_runs"].size() == 1 );
  CHECK( doc["workflow_runs"][0]["run_id"].asString() == "run-1" );

  // Reset the seam so later tests see the default (empty) behavior.
  sicnu::agent::setWorkflowRunsProvider( {} );
  const Json::Value doc2 = sicnu::agent::buildWorkspaceState( nullptr, nullptr );
  CHECK( doc2["workflow_runs"].size() == 0 );
}
