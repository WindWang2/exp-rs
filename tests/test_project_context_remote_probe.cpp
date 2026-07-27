// test_project_context_remote_probe.cpp - end-to-end proof that the #66 host
// (ProjectContext) wires a NetworkProbe into the DataManager's remote-map
// providers, so a remote-map asset registers Ready (not Offline) when the
// service answers.
//
// This is the integration test that closes #66: the unit tests
// (test_remote_map_capabilities_parser, test_network_probe) prove the parser +
// probe logic in isolation; this test proves the host wiring connects them to
// the DataManager.
#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QFileInfo>
#include <QMap>
#include <QString>

#include <qgsapplication.h>
#include <qgslayertree.h>
#include <qgsmapcanvas.h>
#include <qgsmaplayerstore.h>
#include <qgsproject.h>

#include "app/display/network_probe.h"
#include "app/project_context.h"
#include "data/asset_types.h"
#include "data/data_asset.h"
#include "data/data_manager.h"
#include "data/internal/network_probe.h"
#include "data/source_descriptor.h"

using sicnu::app::ProjectContext;
using sicnu::data::AssetState;
using sicnu::data::RemoteMapService;
using sicnu::data::SourceDescriptor;
using sicnu::display::CapabilitiesFetcher;
using sicnu::display::CapabilitiesResponse;
using sicnu::display::QgisNetworkProbe;

namespace
{

void ensureQgisApplication()
{
  if ( QCoreApplication::instance() )
    return;
  static int argc = 1;
  static char applicationName[] = "test_project_context_remote_probe";
  static char *argv[] = { applicationName, nullptr };
  static auto *application = new QgsApplication( argc, argv, true );
  (void)application;
  QgsApplication::initQgis();
}

const QByteArray kWmsCaps = QByteArrayLiteral(
  "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
  "<WMS_Capabilities version=\"1.3.0\" xmlns=\"http://www.opengis.net/wms\">"
  "  <Capability>"
  "    <Request><GetMap><Format>image/png</Format></GetMap></Request>"
  "    <Layer>"
  "      <CRS>EPSG:4326</CRS>"
  "      <EX_GeographicBoundingBox>-180 -90 180 90</EX_GeographicBoundingBox>"
  "      <Layer><Name>imagery</Name></Layer>"
  "    </Layer>"
  "  </Capability>"
  "</WMS_Capabilities>" );

/// A stub fetcher that always returns the canned WMS capabilities — simulates
/// a reachable, healthy WMS service. The probe built on it is what
/// ProjectContext receives.
class ReachableWmsFetcher final : public CapabilitiesFetcher
{
  public:
    std::optional<CapabilitiesResponse>
    fetch( const QString &url, const QString &authConfigId ) const override
    {
      (void)url;
      (void)authConfigId;
      CapabilitiesResponse response;
      response.httpStatus = 200;
      response.body = kWmsCaps;
      response.contentType = QStringLiteral( "text/xml" );
      return response;
    }
};

SourceDescriptor wmsSource()
{
  SourceDescriptor descriptor;
  descriptor.providerKey = QStringLiteral( "wms" );
  descriptor.canonicalSource = QStringLiteral( "https://demo.example/wms" );
  descriptor.dataOptions.insert( QStringLiteral( "layers" ),
                                 QStringLiteral( "imagery" ) );
  descriptor.dataOptions.insert( QStringLiteral( "crs" ),
                                 QStringLiteral( "EPSG:4326" ) );
  return descriptor;
}

} // namespace

TEST_CASE( "ProjectContext wires the NetworkProbe so a reachable WMS registers Ready",
           "[project_context][remote_probe]" )
{
  ensureQgisApplication();
  QgsProject *project = QgsProject::instance();
  project->clear();
  QgsMapCanvas canvas;
  QgsLayerTree tree;
  QgsMapLayerStore store;
  const sicnu::display::DisplayViewSpec viewSpec{ &canvas, &tree, &store };

  // Build a real probe on a stub fetcher (the host would build a
  // QgsBlockingCapabilitiesFetcher; here we stub to stay hermetic).
  ReachableWmsFetcher fetcher;
  QgisNetworkProbe probe( &fetcher );

  auto created = ProjectContext::createForTesting( viewSpec, &probe );
  REQUIRE( created );
  auto context = created.take();
  sicnu::data::DataManager &manager = context->dataManager();

  // Register a WMS source. With the probe wired, the asset resolves Ready with
  // the parsed structure — NOT Offline (the pre-#66 NoNetworkProbe default).
  const sicnu::data::RegisterResult registered =
      manager.registerSource( sicnu::data::RegisterRequest{ wmsSource() } );
  REQUIRE_FALSE( registered.assetId.isNull() );

  const auto snapshot = manager.asset( registered.assetId );
  REQUIRE( snapshot );
  CHECK( snapshot->state() == AssetState::Ready );
  CHECK( snapshot->kind() == sicnu::data::AssetKind::RemoteMap );
  const auto *structure =
      std::get_if<sicnu::data::RemoteMapStructure>( &snapshot->structure() );
  REQUIRE( structure != nullptr );
  CHECK( structure->valid );
  CHECK( structure->service == RemoteMapService::Wms );
  CHECK( structure->layerNames == QStringList{ QStringLiteral( "imagery" ) } );

  project->clear();
}

TEST_CASE( "ProjectContext default create() builds its own probe (production path)",
           "[project_context][remote_probe]" )
{
  // The production create() (no injected probe) builds a QgisNetworkProbe on a
  // QgsBlockingCapabilitiesFetcher. We can't exercise it against live HTTP
  // here, but we assert the construction succeeds and the context is usable —
  // the wiring is structurally sound (no crash, members initialize in order).
  ensureQgisApplication();
  QgsProject *project = QgsProject::instance();
  project->clear();
  QgsMapCanvas canvas;
  QgsLayerTree tree;
  QgsMapLayerStore store;
  const sicnu::display::DisplayViewSpec viewSpec{ &canvas, &tree, &store };

  auto created = ProjectContext::create( viewSpec );
  REQUIRE( created );
  auto context = created.take();
  // A local raster still registers fine through the production path (the probe
  // is irrelevant to local GDAL providers).
  const QString here = QFileInfo( __FILE__ ).absolutePath();
  SourceDescriptor raster;
  raster.providerKey = QStringLiteral( "gdal" );
  raster.canonicalSource =
      QFileInfo( here + QStringLiteral( "/../data/samples/dem_sample.tif" ) )
          .absoluteFilePath();
  const auto registered =
      context->dataManager().registerSource( sicnu::data::RegisterRequest{ raster } );
  REQUIRE_FALSE( registered.assetId.isNull() );
  CHECK( context->dataManager().asset( registered.assetId )->state() ==
         AssetState::Ready );

  project->clear();
}
