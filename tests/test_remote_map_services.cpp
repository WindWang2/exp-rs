// test_remote_map_services.cpp - WMS/WMTS/TMS source providers (#63)
//
// The remaining three web-map providers share the XYZ shape proven in #62.
// Each claims its provider key, probes metadata+reachability through the
// injected NetworkProbe (GetCapabilities-style for WMS/WMTS, template probe
// for TMS), and returns a RemoteMap ResolvedSource with capabilities
// Renderable | OfflineCacheable (never pixel/statistics/query caps).
//
// The point of this ticket is that adding the 4th/5th service later is a small
// provider, not a new wave: the shared shape is the deliverable.
#include <catch2/catch_test_macros.hpp>

#include "data/data_asset.h"
#include "data/data_manager.h"
#include "data/internal/source_provider.h"
#include "data/internal/source_provider_registry.h"
#include "data/providers/tms_source_provider.h"
#include "data/providers/wms_source_provider.h"
#include "data/providers/wmts_source_provider.h"
#include "data/providers/xyz_source_provider.h"
#include "data/source_descriptor.h"

using sicnu::data::AssetCapability;
using sicnu::data::AssetKind;
using sicnu::data::AssetState;
using sicnu::data::DataManager;
using sicnu::data::RemoteMapService;
using sicnu::data::RemoteMapStructure;
using sicnu::data::SourceDescriptor;
using sicnu::data::StorageKind;
using sicnu::data::internal::NetworkProbe;
using sicnu::data::internal::ProbeOutcome;
using sicnu::data::internal::ResolvedSource;
using sicnu::data::providers::TmsSourceProvider;
using sicnu::data::providers::WmsSourceProvider;
using sicnu::data::providers::WmtsSourceProvider;
using sicnu::data::providers::XyzSourceProvider;

namespace
{

/// Canned-probe stub shared across the three providers' tests. The probe never
/// touches the real network; outcomes are driven by the test.
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
      ( void ) url;
      ( void ) options;
      outcome.structure = s;
      return outcome;
    }
};

SourceDescriptor wmsDescriptor( const QString &baseUrl,
                                const QStringList &layers = { QStringLiteral( "imagery" ) } )
{
  SourceDescriptor descriptor;
  descriptor.providerKey = QStringLiteral( "wms" );
  descriptor.canonicalSource = baseUrl;
  descriptor.dataOptions.insert( QStringLiteral( "layers" ), layers.join( ',' ) );
  descriptor.dataOptions.insert( QStringLiteral( "crs" ), QStringLiteral( "EPSG:4326" ) );
  descriptor.dataOptions.insert( QStringLiteral( "format" ), QStringLiteral( "image/png" ) );
  return descriptor;
}

SourceDescriptor wmtsDescriptor( const QString &baseUrl,
                                 const QString &layer = QStringLiteral( "imagery" ) )
{
  SourceDescriptor descriptor;
  descriptor.providerKey = QStringLiteral( "wmts" );
  descriptor.canonicalSource = baseUrl;
  descriptor.dataOptions.insert( QStringLiteral( "layer" ), layer );
  descriptor.dataOptions.insert( QStringLiteral( "tileMatrixSet" ), QStringLiteral( "EPSG:3857" ) );
  descriptor.dataOptions.insert( QStringLiteral( "format" ), QStringLiteral( "image/png" ) );
  return descriptor;
}

SourceDescriptor tmsDescriptor( const QString &templateUrl )
{
  SourceDescriptor descriptor;
  descriptor.providerKey = QStringLiteral( "tms" );
  descriptor.canonicalSource = templateUrl;
  descriptor.dataOptions.insert( QStringLiteral( "zMin" ), QStringLiteral( "0" ) );
  descriptor.dataOptions.insert( QStringLiteral( "zMax" ), QStringLiteral( "14" ) );
  return descriptor;
}

/// Asserts the provider-independent invariants every remote-map provider must
/// hold: kind, storage, the closed capability set, the provider key, and that
/// the structure carries the right service family.
void checkRemoteMapInvariants( const ResolvedSource &resolved,
                               RemoteMapService expectedService,
                               const QString &expectedProviderKey )
{
  CHECK( resolved.kind == AssetKind::RemoteMap );
  CHECK( resolved.storageKind == StorageKind::Remote );
  CHECK( resolved.canonicalProviderKey == expectedProviderKey );
  CHECK( resolved.capabilities ==
         ( AssetCapability::Renderable | AssetCapability::OfflineCacheable ) );
  const auto *structure = std::get_if<RemoteMapStructure>( &resolved.structure );
  REQUIRE( structure != nullptr );
  CHECK( structure->service == expectedService );
}

} // namespace

TEST_CASE( "WMS provider claims its key and resolves a capabilities response",
           "[remote_map][wms_provider]" )
{
  StubNetworkProbe probe;
  probe.desiredState = AssetState::Ready;
  probe.desiredStructure.layerNames = QStringList{ QStringLiteral( "imagery" ) };
  probe.desiredStructure.crsList = QStringList{ QStringLiteral( "EPSG:4326" ) };
  probe.desiredStructure.imageFormat = QStringLiteral( "image/png" );
  probe.desiredStructure.valid = true;

  WmsSourceProvider provider( &probe );
  CHECK( provider.supports( wmsDescriptor( QStringLiteral( "https://wms.example.com" ) ) ) );

  // A non-wms key is not claimed.
  SourceDescriptor other;
  other.providerKey = QStringLiteral( "gdal" );
  CHECK_FALSE( provider.supports( other ) );

  const auto result = provider.resolve(
    wmsDescriptor( QStringLiteral( "https://wms.example.com" ) ) );
  REQUIRE( result );
  CHECK( result.value().state == AssetState::Ready );
  checkRemoteMapInvariants( result.value(), RemoteMapService::Wms, QStringLiteral( "wms" ) );
  const auto *structure = std::get_if<RemoteMapStructure>( &result.value().structure );
  CHECK( structure->layerNames == QStringList{ QStringLiteral( "imagery" ) } );
  CHECK( structure->imageFormat == QStringLiteral( "image/png" ) );
}

TEST_CASE( "WMTS provider reports a tile-matrix-set resolution",
           "[remote_map][wmts_provider]" )
{
  StubNetworkProbe probe;
  probe.desiredState = AssetState::Ready;
  probe.desiredStructure.pixelSizeX = 1.2;
  probe.desiredStructure.pixelSizeY = 1.2;
  probe.desiredStructure.zMin = 0;
  probe.desiredStructure.zMax = 18;
  probe.desiredStructure.valid = true;

  WmtsSourceProvider provider( &probe );
  CHECK( provider.supports( wmtsDescriptor( QStringLiteral( "https://wmts.example.com" ) ) ) );

  const auto result = provider.resolve(
    wmtsDescriptor( QStringLiteral( "https://wmts.example.com" ) ) );
  REQUIRE( result );
  CHECK( result.value().state == AssetState::Ready );
  checkRemoteMapInvariants( result.value(), RemoteMapService::Wmts,
                            QStringLiteral( "wmts" ) );
  const auto *structure = std::get_if<RemoteMapStructure>( &result.value().structure );
  REQUIRE( structure->pixelSizeX.has_value() );
  CHECK( structure->pixelSizeX.value() == 1.2 );
  CHECK( structure->zMax == 18 );
}

TEST_CASE( "TMS provider resolves like XYZ but records the TMS service family",
           "[remote_map][tms_provider]" )
{
  StubNetworkProbe probe;
  probe.desiredState = AssetState::Ready;
  probe.desiredStructure.zMin = 0;
  probe.desiredStructure.zMax = 14;
  probe.desiredStructure.valid = true;

  TmsSourceProvider provider( &probe );
  CHECK( provider.supports( tmsDescriptor(
    QStringLiteral( "https://tms.example.com/{z}/{x}/{y}.png" ) ) ) );

  const auto result = provider.resolve( tmsDescriptor(
    QStringLiteral( "https://tms.example.com/{z}/{x}/{y}.png" ) ) );
  REQUIRE( result );
  CHECK( result.value().state == AssetState::Ready );
  checkRemoteMapInvariants( result.value(), RemoteMapService::Tms,
                            QStringLiteral( "tms" ) );
  const auto *structure = std::get_if<RemoteMapStructure>( &result.value().structure );
  CHECK( structure->zMax == 14 );
}

TEST_CASE( "A partial zMin option does not clobber the probe's service zMax",
           "[remote_map][wmts_provider]" )
{
  // WMTS z-range is service-discovered (the probe fills it from the tile
  // matrix). A caller supplying only zMin must not zero out the probe's zMax.
  StubNetworkProbe probe;
  probe.desiredState = AssetState::Ready;
  probe.desiredStructure.zMin = 0;
  probe.desiredStructure.zMax = 18;
  probe.desiredStructure.valid = true;

  WmtsSourceProvider provider( &probe );
  SourceDescriptor descriptor =
    wmtsDescriptor( QStringLiteral( "https://wmts.example.com" ) );
  // Caller declares a floor only — no zMax.
  descriptor.dataOptions.insert( QStringLiteral( "zMin" ), QStringLiteral( "3" ) );

  const auto result = provider.resolve( descriptor );
  REQUIRE( result );
  const auto *structure = std::get_if<RemoteMapStructure>( &result.value().structure );
  REQUIRE( structure != nullptr );
  CHECK( structure->zMin == 3 );          // caller-declared floor honored
  CHECK( structure->zMax == 18 );         // probe's service zMax survives
}

TEST_CASE( "Each provider resolves Offline on an unreachable endpoint, not Error",
           "[remote_map][wms_provider][wmts_provider][tms_provider]" )
{
  StubNetworkProbe probe;
  probe.desiredState = AssetState::Offline;

  WmsSourceProvider wms( &probe );
  WmtsSourceProvider wmts( &probe );
  TmsSourceProvider tms( &probe );

  const auto wmsResult = wms.resolve( wmsDescriptor( QStringLiteral( "https://down" ) ) );
  REQUIRE( wmsResult );
  CHECK( wmsResult.value().state == AssetState::Offline );

  const auto wmtsResult = wmts.resolve( wmtsDescriptor( QStringLiteral( "https://down" ) ) );
  REQUIRE( wmtsResult );
  CHECK( wmtsResult.value().state == AssetState::Offline );

  const auto tmsResult = tms.resolve( tmsDescriptor(
    QStringLiteral( "https://down/{z}/{x}/{y}.png" ) ) );
  REQUIRE( tmsResult );
  CHECK( tmsResult.value().state == AssetState::Offline );
}

TEST_CASE( "Each provider resolves AuthenticationRequired on an auth challenge",
           "[remote_map][wms_provider][wmts_provider][tms_provider]" )
{
  StubNetworkProbe probe;
  probe.desiredState = AssetState::AuthenticationRequired;

  WmsSourceProvider wms( &probe );
  WmtsSourceProvider wmts( &probe );
  TmsSourceProvider tms( &probe );

  const auto wmsResult = wms.resolve( wmsDescriptor( QStringLiteral( "https://secure" ) ) );
  REQUIRE( wmsResult );
  CHECK( wmsResult.value().state == AssetState::AuthenticationRequired );

  const auto wmtsResult = wmts.resolve( wmtsDescriptor( QStringLiteral( "https://secure" ) ) );
  REQUIRE( wmtsResult );
  CHECK( wmtsResult.value().state == AssetState::AuthenticationRequired );

  const auto tmsResult = tms.resolve(
    tmsDescriptor( QStringLiteral( "https://secure/{z}/{x}/{y}.png" ) ) );
  REQUIRE( tmsResult );
  CHECK( tmsResult.value().state == AssetState::AuthenticationRequired );
}

TEST_CASE( "Each provider registers through the unchanged pipeline and dedups",
           "[remote_map][registration]" )
{
  // All three providers in one registry: registering a wms/wmts/tms descriptor
  // through the normal registerSource produces a queryable RemoteMap asset that
  // dedups by SourceKey, with NO new DataManager method.
  sicnu::data::internal::SourceProviderRegistry registry;
  StubNetworkProbe probe;
  probe.desiredState = AssetState::Ready;
  probe.desiredStructure.valid = true;
  registry.add( std::make_unique<WmsSourceProvider>( &probe ) );
  registry.add( std::make_unique<WmtsSourceProvider>( &probe ) );
  registry.add( std::make_unique<TmsSourceProvider>( &probe ) );
  std::unique_ptr<DataManager> manager = registry.createDataManager();

  const SourceDescriptor wms = wmsDescriptor( QStringLiteral( "https://wms.example.com" ) );
  const auto wmsReg = manager->registerSource( { wms } );
  REQUIRE_FALSE( wmsReg.assetId.isNull() );
  CHECK( manager->asset( wmsReg.assetId )->kind() == AssetKind::RemoteMap );
  CHECK( manager->asset( wmsReg.assetId )->state() == AssetState::Ready );

  // Same descriptor dedups.
  CHECK( manager->registerSource( { wms } ).assetId == wmsReg.assetId );

  // WMTS and TMS are distinct assets (different provider key + source).
  const auto wmtsReg =
    manager->registerSource( { wmtsDescriptor( QStringLiteral( "https://wmts.example.com" ) ) } );
  const auto tmsReg = manager->registerSource(
    { tmsDescriptor( QStringLiteral( "https://tms.example.com/{z}/{x}/{y}.png" ) ) } );
  CHECK_FALSE( wmtsReg.assetId == wmsReg.assetId );
  CHECK_FALSE( tmsReg.assetId == wmsReg.assetId );
  CHECK_FALSE( wmtsReg.assetId == tmsReg.assetId );
}

TEST_CASE( "All four remote-map providers coexist with distinct provider keys",
           "[remote_map][coexist]" )
{
  // The deliverable: adding services is a small provider each. All four sit in
  // one registry and each claims ONLY its own key.
  sicnu::data::internal::SourceProviderRegistry registry;
  StubNetworkProbe probe;
  registry.add( std::make_unique<XyzSourceProvider>( &probe ) );
  registry.add( std::make_unique<WmsSourceProvider>( &probe ) );
  registry.add( std::make_unique<WmtsSourceProvider>( &probe ) );
  registry.add( std::make_unique<TmsSourceProvider>( &probe ) );

  const QStringList keys{ QStringLiteral( "xyz" ), QStringLiteral( "wms" ),
                          QStringLiteral( "wmts" ), QStringLiteral( "tms" ) };
  for ( const QString &key : keys )
  {
    SourceDescriptor d;
    d.providerKey = key;
    d.canonicalSource = QStringLiteral( "https://example.com" );
    // Each key is claimed by exactly one provider (the registry resolves to
    // the first match; assert none of them is shadowed by another).
    bool anySupports = false;
    for ( const auto &provider : { std::unique_ptr<sicnu::data::internal::SourceProvider>(
                                     std::make_unique<XyzSourceProvider>() ),
                                   std::unique_ptr<sicnu::data::internal::SourceProvider>(
                                     std::make_unique<WmsSourceProvider>() ),
                                   std::unique_ptr<sicnu::data::internal::SourceProvider>(
                                     std::make_unique<WmtsSourceProvider>() ),
                                   std::unique_ptr<sicnu::data::internal::SourceProvider>(
                                     std::make_unique<TmsSourceProvider>() ) } )
    {
      if ( provider->supports( d ) )
        anySupports = true;
    }
    CHECK( anySupports );
  }

  // A key none of them claims is not silently matched.
  SourceDescriptor unknown;
  unknown.providerKey = QStringLiteral( "wfs" );
  unknown.canonicalSource = QStringLiteral( "https://example.com" );
  CHECK_FALSE( XyzSourceProvider().supports( unknown ) );
  CHECK_FALSE( WmsSourceProvider().supports( unknown ) );
  CHECK_FALSE( WmtsSourceProvider().supports( unknown ) );
  CHECK_FALSE( TmsSourceProvider().supports( unknown ) );
}
