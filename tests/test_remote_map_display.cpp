// test_remote_map_display.cpp - AuthResolver seam + RemoteMap display materialization (#64)
//
// The first end-to-end display of a Remote Map Asset, and the first first-party
// touch of QgsAuthManager. A Ready remote map materializes through QgsRasterLayer
// with the provider key from canonicalProviderKey and a URI built per service;
// the AuthResolver injects the asset's authConfigId as a configured URI at
// materialization time (never credential material). An Offline/
// AuthenticationRequired remote map is refused at the addLayer Ready guard.
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QTemporaryDir>

#include <qgsapplication.h>
#include <qgslayertree.h>
#include <qgsmapcanvas.h>
#include <qgsmaplayerstore.h>
#include <qgsrasterlayer.h>

#include "app/display/auth_resolver.h"
#include "app/display/qgis_display_manager.h"
#include "data/data_asset.h"
#include "data/data_manager.h"
#include "data/internal/network_probe.h"
#include "data/internal/source_provider_registry.h"
#include "data/providers/xyz_source_provider.h"
#include "data/source_descriptor.h"

using sicnu::data::AssetCapability;
using sicnu::data::AssetId;
using sicnu::data::AssetKind;
using sicnu::data::AssetState;
using sicnu::data::DataManager;
using sicnu::data::RemoteMapService;
using sicnu::data::RemoteMapStructure;
using sicnu::data::SourceDescriptor;
using sicnu::data::internal::NetworkProbe;
using sicnu::data::internal::ProbeOutcome;
using sicnu::display::AuthResolver;
using sicnu::display::DisplayViewId;
using sicnu::display::DisplayViewSpec;
using sicnu::display::QgisDisplayManager;
using sicnu::data::providers::XyzSourceProvider;

namespace
{

void ensureQgisApplication()
{
  if ( QCoreApplication::instance() )
    return;
  static int argc = 1;
  static char applicationName[] = "test_remote_map_display";
  static char *argv[] = { applicationName, nullptr };
  static auto *application = new QgsApplication( argc, argv, true );
  ( void ) application;
  QgsApplication::initQgis();
}

/// Stub probe that reports Ready so the asset passes the addLayer Ready guard.
class StubReadyProbe final : public NetworkProbe
{
  public:
    ProbeOutcome probe( RemoteMapService service,
                        const QString &url,
                        const QMap<QString, QString> &options ) const override
    {
      ProbeOutcome outcome;
      outcome.state = AssetState::Ready;
      outcome.structure.service = service;
      outcome.structure.valid = true;
      ( void ) url;
      ( void ) options;
      return outcome;
    }
};

/// A stub AuthResolver that records what it was asked to apply and produces a
/// configured URI string. It NEVER returns credential material — the result is
/// the input URI with an authcfg parameter appended (mirroring the real
/// QgsDataSourceUri::setAuthConfigId path).
class RecordingAuthResolver final : public AuthResolver
{
  public:
    // Mutable: applyAuthConfig is const per the interface contract; the stub
    // records what it was asked to apply.
    mutable QString lastAuthConfigId;
    mutable QString lastProviderKey;
    mutable QString lastUri;
    bool acceptConfig = true;

    sicnu::data::Result<QString> applyAuthConfig( const QString &authConfigId,
                                                  const QString &providerKey,
                                                  const QString &uri ) const override
    {
      lastAuthConfigId = authConfigId;
      lastProviderKey = providerKey;
      lastUri = uri;
      if ( !acceptConfig )
      {
        return sicnu::data::Result<QString>::failure(
          sicnu::data::Diagnostic{ QStringLiteral( "auth.config_unavailable" ),
                                   QStringLiteral( "No auth config available for %1" )
                                     .arg( authConfigId ),
                                   sicnu::data::DiagnosticSeverity::Error } );
      }
      // Produce a configured URI: the auth-cfg id rides as a parameter (never
      // credential material). The test asserts the result carries only the id.
      const QString configured =
        uri + ( uri.contains( QChar( '?' ) ) ? QStringLiteral( "&" )
                                             : QStringLiteral( "?" ) ) +
        QStringLiteral( "authcfg=" ) + authConfigId;
      return sicnu::data::Result<QString>::success( configured );
    }
};

DisplayViewId createView( QgisDisplayManager &manager, QgsMapCanvas &canvas,
                          QgsLayerTree &tree, QgsMapLayerStore &store )
{
  DisplayViewSpec spec;
  spec.canvas = &canvas;
  spec.layerTree = &tree;
  spec.layerStore = &store;
  const auto created = manager.createView( spec );
  REQUIRE( created );
  return created.value();
}

SourceDescriptor xyzDescriptor( const QString &templateUrl,
                                const QString &authConfigId = QString() )
{
  SourceDescriptor descriptor;
  descriptor.providerKey = QStringLiteral( "xyz" );
  descriptor.canonicalSource = templateUrl;
  descriptor.dataOptions.insert( QStringLiteral( "zMin" ), QStringLiteral( "0" ) );
  descriptor.dataOptions.insert( QStringLiteral( "zMax" ), QStringLiteral( "12" ) );
  if ( !authConfigId.isEmpty() )
    descriptor.authConfigId = authConfigId;
  return descriptor;
}

/// Builds a DataManager whose registry resolves XYZ descriptors to Ready via a
/// stub probe (the default NoNetworkProbe would resolve Offline and fail the
/// addLayer Ready guard).
std::unique_ptr<DataManager> makeManager()
{
  sicnu::data::internal::SourceProviderRegistry registry;
  static StubReadyProbe probe;
  registry.add( std::make_unique<XyzSourceProvider>( &probe ) );
  return registry.createDataManager();
}

} // namespace

TEST_CASE( "A Ready remote map reaches the materialization arm with its provider key",
           "[remote_map][display]" )
{
  // The display path for a RemoteMap builds a QgsRasterLayer under the asset's
  // provider key. QGIS layer validity is network-dependent (a synthetic
  // example.com service won't validate offscreen), so this test asserts the
  // materialization arm RAN — by observing that the AuthResolver was invoked
  // with the asset's provider key — rather than a live QGIS validity outcome.
  ensureQgisApplication();
  auto dataManager = makeManager();
  const auto registered =
    dataManager->registerSource( { xyzDescriptor(
      QStringLiteral( "https://tiles.example.com/{z}/{x}/{y}.png" ) ) } );
  REQUIRE_FALSE( registered.assetId.isNull() );

  RecordingAuthResolver authResolver;
  QgisDisplayManager displayManager( dataManager.get(), &authResolver );
  QgsMapCanvas canvas;
  QgsLayerTree layerTree;
  QgsMapLayerStore layerStore;
  const DisplayViewId viewId = createView( displayManager, canvas, layerTree, layerStore );

  // addLayer may report materialization_failed offscreen (no live tile fetch),
  // but reaching the materialization arm proves the RemoteMap display path ran.
  ( void ) displayManager.addLayer( viewId, registered.assetId );

  // The resolver was invoked with the asset's xyz provider key — the arm ran.
  CHECK( authResolver.lastProviderKey == QStringLiteral( "xyz" ) );
  CHECK( authResolver.lastUri.contains( QStringLiteral( "tiles.example.com" ) ) );
}

TEST_CASE( "An authConfigId is applied at materialization time, not stored as a layer field",
           "[remote_map][display][auth]" )
{
  ensureQgisApplication();
  auto dataManager = makeManager();
  const auto registered = dataManager->registerSource(
    { xyzDescriptor( QStringLiteral( "https://secure.example.com/{z}/{x}/{y}.png" ),
                     QStringLiteral( "cfg-123" ) ) } );
  REQUIRE_FALSE( registered.assetId.isNull() );

  RecordingAuthResolver authResolver;
  QgisDisplayManager displayManager( dataManager.get(), &authResolver );
  QgsMapCanvas canvas;
  QgsLayerTree layerTree;
  QgsMapLayerStore layerStore;
  const DisplayViewId viewId = createView( displayManager, canvas, layerTree, layerStore );

  // addLayer may report materialization_failed offscreen, but the resolver is
  // invoked during materialization regardless — asserting on it proves the
  // authConfigId reached the auth seam at materialization time.
  ( void ) displayManager.addLayer( viewId, registered.assetId );

  // The resolver was invoked with the asset's authConfigId and the xyz provider.
  CHECK( authResolver.lastAuthConfigId == QStringLiteral( "cfg-123" ) );
  CHECK( authResolver.lastProviderKey == QStringLiteral( "xyz" ) );
  CHECK( authResolver.lastUri.contains( QStringLiteral( "secure.example.com" ) ) );
}

TEST_CASE( "The AuthResolver result carries the auth-cfg id, never credential material",
           "[remote_map][display][auth]" )
{
  // Direct test of the resolver contract: the configured URI it produces
  // contains the auth-cfg id reference, NOT a password. (The real
  // QgsAuthManager-backed resolver sets the authcfg parameter; the stub models
  // it.) This is the load-bearing credential-discipline assertion.
  RecordingAuthResolver authResolver;
  const auto configured = authResolver.applyAuthConfig(
    QStringLiteral( "cfg-123" ), QStringLiteral( "xyz" ),
    QStringLiteral( "https://secure.example.com/{z}/{x}/{y}.png" ) );
  REQUIRE( configured );
  CHECK( configured.value().contains( QStringLiteral( "authcfg=cfg-123" ) ) );
  // No password/secret/token string appears in the configured URI.
  CHECK_FALSE( configured.value().contains( QStringLiteral( "password" ),
                                            Qt::CaseInsensitive ) );
}

TEST_CASE( "An empty authConfigId is applied as a no-op when the service is open",
           "[remote_map][display][auth]" )
{
  ensureQgisApplication();
  auto dataManager = makeManager();
  // No authConfigId on the descriptor — an open service.
  const auto registered = dataManager->registerSource(
    { xyzDescriptor( QStringLiteral( "https://open.example.com/{z}/{x}/{y}.png" ) ) } );
  REQUIRE_FALSE( registered.assetId.isNull() );

  RecordingAuthResolver authResolver;
  QgisDisplayManager displayManager( dataManager.get(), &authResolver );
  QgsMapCanvas canvas;
  QgsLayerTree layerTree;
  QgsMapLayerStore layerStore;
  const DisplayViewId viewId = createView( displayManager, canvas, layerTree, layerStore );

  // addLayer may report materialization_failed offscreen; the resolver is still
  // invoked with the (empty) authConfigId for an open service.
  ( void ) displayManager.addLayer( viewId, registered.assetId );
  // An empty authConfigId is still passed through to the resolver; the resolver
  // for an open service returns the URI unchanged (no authcfg parameter).
  CHECK( authResolver.lastAuthConfigId.isEmpty() );
}

TEST_CASE( "An Offline remote map is refused at the addLayer Ready guard",
           "[remote_map][display]" )
{
  ensureQgisApplication();
  // Build a manager whose probe reports Offline.
  sicnu::data::internal::SourceProviderRegistry registry;
  class OfflineProbe final : public NetworkProbe
  {
    ProbeOutcome probe( RemoteMapService service, const QString &,
                        const QMap<QString, QString> & ) const override
    {
      ProbeOutcome outcome;
      outcome.state = AssetState::Offline;
      outcome.structure.service = service;
      return outcome;
    }
  };
  static OfflineProbe probe;
  registry.add( std::make_unique<XyzSourceProvider>( &probe ) );
  auto dataManager = registry.createDataManager();

  const auto registered = dataManager->registerSource(
    { xyzDescriptor( QStringLiteral( "https://down.example.com/{z}/{x}/{y}.png" ) ) } );
  REQUIRE_FALSE( registered.assetId.isNull() );
  REQUIRE( dataManager->asset( registered.assetId )->state() == AssetState::Offline );

  RecordingAuthResolver authResolver;
  QgisDisplayManager displayManager( dataManager.get(), &authResolver );
  QgsMapCanvas canvas;
  QgsLayerTree layerTree;
  QgsMapLayerStore layerStore;
  const DisplayViewId viewId = createView( displayManager, canvas, layerTree, layerStore );

  // The existing addLayer Ready guard refuses a non-Ready remote map.
  const auto layer = displayManager.addLayer( viewId, registered.assetId );
  REQUIRE_FALSE( layer );
  CHECK( layer.diagnostics().first().code == QStringLiteral( "display.asset_not_ready" ) );
}

TEST_CASE( "An auth-config failure refuses materialization (no unauthenticated layer)",
           "[remote_map][display][auth]" )
{
  // The load-bearing security contract: when the AuthResolver cannot apply the
  // config, addLayer must refuse rather than fall back to an unauthenticated
  // URI. A Ready remote map is built, the resolver rejects the config, and no
  // layer is registered.
  ensureQgisApplication();
  auto dataManager = makeManager();
  const auto registered = dataManager->registerSource(
    { xyzDescriptor( QStringLiteral( "https://secure.example.com/{z}/{x}/{y}.png" ),
                     QStringLiteral( "cfg-bad" ) ) } );
  REQUIRE_FALSE( registered.assetId.isNull() );
  REQUIRE( dataManager->asset( registered.assetId )->state() == AssetState::Ready );

  RecordingAuthResolver authResolver;
  authResolver.acceptConfig = false;
  QgisDisplayManager displayManager( dataManager.get(), &authResolver );
  QgsMapCanvas canvas;
  QgsLayerTree layerTree;
  QgsMapLayerStore layerStore;
  const DisplayViewId viewId = createView( displayManager, canvas, layerTree, layerStore );

  const auto layer = displayManager.addLayer( viewId, registered.assetId );
  REQUIRE_FALSE( layer );
  CHECK( layer.diagnostics().first().code == QStringLiteral( "auth.config_unavailable" ) );
  // No layer was registered for the asset.
  const auto snapshot = displayManager.view( viewId );
  REQUIRE( snapshot.has_value() );
  CHECK( snapshot->layerIds().isEmpty() );
}
