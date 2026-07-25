// test_xyz_source_provider.cpp - XyzSourceProvider + NetworkProbe + registration (#62)
//
// The first real Remote Map source provider. XYZ is chosen first because it is
// stateless: no GetCapabilities round-trip, just a tile-URL template + a probe
// of the declared endpoint. This proves the registration pipeline works end to
// end through the unchanged registerSource, with NO new DataManager method.
//
// Network I/O is abstracted behind NetworkProbe so providers are unit-testable
// without touching the network; tests inject a stub returning canned outcomes.
#include <catch2/catch_test_macros.hpp>

#include "data/data_asset.h"
#include "data/data_manager.h"
#include "data/internal/source_provider.h"
#include "data/internal/source_provider_registry.h"
#include "data/providers/xyz_source_provider.h"
#include "data/source_descriptor.h"

using sicnu::data::AssetCapability;
using sicnu::data::AssetKind;
using sicnu::data::AssetState;
using sicnu::data::DataManager;
using sicnu::data::PersistencePolicy;
using sicnu::data::RegisterRequest;
using sicnu::data::RemoteMapService;
using sicnu::data::RemoteMapStructure;
using sicnu::data::SourceDescriptor;
using sicnu::data::StorageKind;
using sicnu::data::internal::NetworkProbe;
using sicnu::data::internal::ProbeOutcome;
using sicnu::data::internal::ResolvedSource;
using sicnu::data::providers::XyzSourceProvider;

namespace
{

/// A NetworkProbe stub whose canned response is set per test. The provider
/// never touches the real network — outcomes (Ready/Offline/AuthRequired) are
/// driven entirely by this stub, mirroring how GDAL fixtures stage files.
class StubNetworkProbe final : public NetworkProbe
{
  public:
    AssetState desiredState = AssetState::Ready;
    RemoteMapStructure desiredStructure;

    ProbeOutcome probe( RemoteMapService service,
                        const QString &url,
                        const QMap<QString, QString> &options ) const override
    {
      ProbeOutcome outcome;
      outcome.state = desiredState;
      RemoteMapStructure s = desiredStructure;
      s.service = service;
      // The stub advertises a single layer derived from the URL for realism.
      if ( s.layerNames.isEmpty() )
        s.layerNames = QStringList{ QStringLiteral( "xyz-tiles" ) };
      ( void ) url;
      ( void ) options;
      outcome.structure = s;
      return outcome;
    }
};

SourceDescriptor xyzDescriptor( const QString &templateUrl )
{
  SourceDescriptor descriptor;
  descriptor.providerKey = QStringLiteral( "xyz" );
  descriptor.canonicalSource = templateUrl;
  descriptor.dataOptions.insert( QStringLiteral( "zMin" ), QStringLiteral( "0" ) );
  descriptor.dataOptions.insert( QStringLiteral( "zMax" ), QStringLiteral( "12" ) );
  return descriptor;
}

} // namespace

TEST_CASE( "XyzSourceProvider claims the xyz provider key",
           "[remote_map][xyz_provider]" )
{
  XyzSourceProvider provider;
  SourceDescriptor supported = xyzDescriptor(
    QStringLiteral( "https://tiles.example.com/{z}/{x}/{y}.png" ) );
  CHECK( provider.supports( supported ) );

  // A non-xyz provider key is not claimed.
  SourceDescriptor other;
  other.providerKey = QStringLiteral( "gdal" );
  other.canonicalSource = QStringLiteral( "/tmp/a.tif" );
  CHECK_FALSE( provider.supports( other ) );

  // An empty key with a tile template URL is NOT silently claimed — XYZ must
  // be explicit so a generic URL does not masquerade as a tile service.
  SourceDescriptor empty;
  empty.canonicalSource =
    QStringLiteral( "https://tiles.example.com/{z}/{x}/{y}.png" );
  CHECK_FALSE( provider.supports( empty ) );
}

TEST_CASE( "A reachable XYZ template resolves Ready with honest capabilities",
           "[remote_map][xyz_provider]" )
{
  StubNetworkProbe probe;
  probe.desiredState = AssetState::Ready;
  probe.desiredStructure.zMin = 0;
  probe.desiredStructure.zMax = 12;
  probe.desiredStructure.imageFormat = QStringLiteral( "image/png" );
  probe.desiredStructure.valid = true;

  XyzSourceProvider provider( &probe );
  const auto result = provider.resolve( xyzDescriptor(
    QStringLiteral( "https://tiles.example.com/{z}/{x}/{y}.png" ) ) );

  REQUIRE( result );
  const ResolvedSource &resolved = result.value();
  CHECK( resolved.kind == AssetKind::RemoteMap );
  CHECK( resolved.storageKind == StorageKind::Remote );
  CHECK( resolved.canonicalProviderKey == QStringLiteral( "xyz" ) );
  // Honest capabilities: renderable + cacheable only — no pixel readback,
  // statistics, or feature query (parent spec line 109).
  CHECK( resolved.capabilities ==
         ( AssetCapability::Renderable | AssetCapability::OfflineCacheable ) );
  CHECK( resolved.state == AssetState::Ready );

  const auto *structure = std::get_if<RemoteMapStructure>( &resolved.structure );
  REQUIRE( structure != nullptr );
  CHECK( structure->service == RemoteMapService::Xyz );
  CHECK( structure->zMin == 0 );
  CHECK( structure->zMax == 12 );
  CHECK( structure->valid );
}

TEST_CASE( "An unreachable XYZ endpoint resolves Offline, not Error",
           "[remote_map][xyz_provider]" )
{
  StubNetworkProbe probe;
  probe.desiredState = AssetState::Offline;
  XyzSourceProvider provider( &probe );

  const auto result = provider.resolve( xyzDescriptor(
    QStringLiteral( "https://down.example.com/{z}/{x}/{y}.png" ) ) );

  REQUIRE( result );
  CHECK( result.value().state == AssetState::Offline );
  CHECK( result.value().kind == AssetKind::RemoteMap );
  // The asset still declares renderability; an Offline state is a transient
  // reachability outcome, not a permanent capability loss.
  CHECK( result.value().capabilities.testFlag( AssetCapability::Renderable ) );
}

TEST_CASE( "A credentials-gated XYZ endpoint resolves AuthenticationRequired",
           "[remote_map][xyz_provider]" )
{
  StubNetworkProbe probe;
  probe.desiredState = AssetState::AuthenticationRequired;
  XyzSourceProvider provider( &probe );

  const auto result = provider.resolve( xyzDescriptor(
    QStringLiteral( "https://secure.example.com/{z}/{x}/{y}.png" ) ) );

  REQUIRE( result );
  CHECK( result.value().state == AssetState::AuthenticationRequired );
  CHECK( result.value().kind == AssetKind::RemoteMap );
}

TEST_CASE( "A registered XYZ asset dedups by template + options and is queryable",
           "[remote_map][xyz_provider][registration]" )
{
  // The wave's central claim: registerSource is already kind-agnostic and
  // provider-driven, so registering a remote-map descriptor through the normal
  // pipeline dedups, persists-ready, and is queryable — with NO new
  // DataManager method. The XYZ provider must be in the registry for this to
  // work; the test uses a custom registry so it does not depend on the app's
  // default-probe wiring.
  sicnu::data::internal::SourceProviderRegistry registry;
  StubNetworkProbe probe;
  probe.desiredState = AssetState::Ready;
  probe.desiredStructure.valid = true;
  registry.add( std::make_unique<XyzSourceProvider>( &probe ) );
  std::unique_ptr<DataManager> manager = registry.createDataManager();

  const SourceDescriptor first = xyzDescriptor(
    QStringLiteral( "https://tiles.example.com/{z}/{x}/{y}.png" ) );
  const auto registered = manager->registerSource( { first } );
  REQUIRE_FALSE( registered.assetId.isNull() );

  const auto snapshot = manager->asset( registered.assetId );
  REQUIRE( snapshot.has_value() );
  CHECK( snapshot->kind() == AssetKind::RemoteMap );
  CHECK( snapshot->state() == AssetState::Ready );
  CHECK( snapshot->capabilities().testFlag( AssetCapability::Renderable ) );

  // The same template + options dedups to one asset.
  const auto duplicate = manager->registerSource( { first } );
  CHECK( duplicate.assetId == registered.assetId );
  CHECK( duplicate.reusedExisting );

  // A different template is a distinct asset.
  const SourceDescriptor other = xyzDescriptor(
    QStringLiteral( "https://other.example.com/{z}/{x}/{y}.png" ) );
  const auto distinct = manager->registerSource( { other } );
  CHECK_FALSE( distinct.assetId == registered.assetId );
  CHECK_FALSE( distinct.reusedExisting );
}

TEST_CASE( "An XYZ asset with no injected probe resolves Offline by default",
           "[remote_map][xyz_provider]" )
{
  // When no probe is injected (the app hasn't wired the real HTTP probe), the
  // provider's default behavior is conservative-Offline rather than a hard
  // failure — the asset registers and can be re-resolved once the host wires
  // a probe. This keeps src/data free of a Qt Network dependency.
  XyzSourceProvider provider;
  const auto result = provider.resolve( xyzDescriptor(
    QStringLiteral( "https://tiles.example.com/{z}/{x}/{y}.png" ) ) );

  REQUIRE( result );
  CHECK( result.value().state == AssetState::Offline );
  CHECK( result.value().kind == AssetKind::RemoteMap );
}
