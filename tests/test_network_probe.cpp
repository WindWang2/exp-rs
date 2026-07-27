// test_network_probe.cpp - the #66 concrete QgisNetworkProbe with an injected
// stub fetcher (no live HTTP).
//
// The probe delegates all I/O to a CapabilitiesFetcher interface so its
// state-mapping logic (HTTP status / XML body -> AssetState + structure) is
// unit-testable without the network. The production QgsBlockingCapabilitiesFetcher
// (QgsNetworkAccessManager::blockingGet) is a thin wrapper verified by the
// end-to-end ProjectContext test, not here.
#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QMap>
#include <QString>

#include "app/display/network_probe.h"
#include "data/asset_types.h"
#include "data/data_asset.h"

using sicnu::data::AssetState;
using sicnu::data::RemoteMapService;
using sicnu::display::CapabilitiesFetcher;
using sicnu::display::CapabilitiesResponse;
using sicnu::display::QgisNetworkProbe;

namespace
{

/// A minimal WMS 1.3.0 GetCapabilities body the parser can extract fields from.
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

/// Records what the probe asked for (URL + authConfigId) and returns a canned
/// response. Mirrors how the production fetcher is consumed.
class StubFetcher final : public CapabilitiesFetcher
{
  public:
    CapabilitiesResponse response;
    bool returnNothing = false; // simulate a network error / timeout
    mutable QString lastUrl;
    mutable QString lastAuthConfigId;

    std::optional<CapabilitiesResponse>
    fetch( const QString &url, const QString &authConfigId ) const override
    {
      lastUrl = url;
      lastAuthConfigId = authConfigId;
      if ( returnNothing )
        return std::nullopt;
      return response;
    }
};

QMap<QString, QString> wmsOptions()
{
  // The probe receives the caller's options; for WMS it advertises layers/crs.
  return { { QStringLiteral( "layers" ), QStringLiteral( "imagery" ) },
           { QStringLiteral( "crs" ), QStringLiteral( "EPSG:4326" ) } };
}

} // namespace

TEST_CASE( "QgisNetworkProbe reports Ready for a reachable WMS service",
           "[remote_map][network_probe]" )
{
  StubFetcher fetcher;
  fetcher.response.httpStatus = 200;
  fetcher.response.body = kWmsCaps;
  fetcher.response.contentType = QStringLiteral( "text/xml" );

  QgisNetworkProbe probe( &fetcher );
  const auto outcome = probe.probe( RemoteMapService::Wms,
                                    QStringLiteral( "https://demo/wms" ),
                                    wmsOptions() );

  CHECK( outcome.state == AssetState::Ready );
  REQUIRE( outcome.structure.valid );
  CHECK( outcome.structure.service == RemoteMapService::Wms );
  CHECK( outcome.structure.layerNames == QStringList{ QStringLiteral( "imagery" ) } );
  CHECK( outcome.structure.imageFormat == QStringLiteral( "image/png" ) );
  // The probe built a GetCapabilities URL from the base.
  CHECK( fetcher.lastUrl.contains( QStringLiteral( "GetCapabilities" ) ) );
  CHECK( fetcher.lastUrl.startsWith( QStringLiteral( "https://demo/wms" ) ) );
}

TEST_CASE( "QgisNetworkProbe reports AuthenticationRequired on 401/403",
           "[remote_map][network_probe]" )
{
  StubFetcher fetcher;
  fetcher.response.httpStatus = 401;
  fetcher.response.body = QByteArrayLiteral( "Unauthorized" );

  QgisNetworkProbe probe( &fetcher );
  const auto outcome = probe.probe( RemoteMapService::Wms,
                                    QStringLiteral( "https://demo/wms" ),
                                    wmsOptions() );

  // A 401/403 means the service demands credentials; the probe surfaces it as
  // AuthenticationRequired (not Offline/Error) so the host can prompt.
  CHECK( outcome.state == AssetState::AuthenticationRequired );
  CHECK( outcome.structure.service == RemoteMapService::Wms );
}

TEST_CASE( "QgisNetworkProbe reports Offline on network error / timeout",
           "[remote_map][network_probe]" )
{
  StubFetcher fetcher;
  fetcher.returnNothing = true; // fetcher returns nullopt (no reply / timeout)

  QgisNetworkProbe probe( &fetcher );
  const auto outcome = probe.probe( RemoteMapService::Wms,
                                    QStringLiteral( "https://demo/wms" ),
                                    wmsOptions() );

  // An unreachable service is Offline (the conservative NoNetworkProbe contract),
  // NOT Error (which is reserved for malformed-service responses).
  CHECK( outcome.state == AssetState::Offline );
  CHECK( outcome.structure.service == RemoteMapService::Wms );
}

TEST_CASE( "QgisNetworkProbe reports Error on a non-capabilities 200 body",
           "[remote_map][network_probe]" )
{
  StubFetcher fetcher;
  fetcher.response.httpStatus = 200;
  // 200 OK but the body is not a WMS Capabilities document (e.g. an error
  // page). The parser marks it invalid; the probe surfaces Error.
  fetcher.response.body = QByteArrayLiteral( "<html>error</html>" );

  QgisNetworkProbe probe( &fetcher );
  const auto outcome = probe.probe( RemoteMapService::Wms,
                                    QStringLiteral( "https://demo/wms" ),
                                    wmsOptions() );

  CHECK( outcome.state == AssetState::Error );
  CHECK_FALSE( outcome.structure.valid );
}

TEST_CASE( "QgisNetworkProbe forwards authConfigId to the fetcher (credential discipline)",
           "[remote_map][network_probe]" )
{
  StubFetcher fetcher;
  fetcher.response.httpStatus = 200;
  fetcher.response.body = kWmsCaps;

  QgisNetworkProbe probe( &fetcher );
  QMap<QString, QString> options = wmsOptions();
  options.insert( QStringLiteral( "authConfigId" ),
                  QStringLiteral( "abc123" ) );
  ( void )probe.probe( RemoteMapService::Wms,
                       QStringLiteral( "https://demo/wms" ), options );

  // The probe NEVER handles credential material; it carries the authConfigId
  // through to the fetcher, which applies it via QgsAuthManager.
  CHECK( fetcher.lastAuthConfigId == QStringLiteral( "abc123" ) );
}

TEST_CASE( "QgisNetworkProbe reports Ready for a reachable XYZ tile template",
           "[remote_map][network_probe]" )
{
  StubFetcher fetcher;
  fetcher.response.httpStatus = 200;
  fetcher.response.body = QByteArrayLiteral( "tile-bytes" );

  QgisNetworkProbe probe( &fetcher );
  QMap<QString, QString> options;
  options.insert( QStringLiteral( "zMin" ), QStringLiteral( "0" ) );
  options.insert( QStringLiteral( "zMax" ), QStringLiteral( "18" ) );
  const auto outcome =
      probe.probe( RemoteMapService::Xyz,
                   QStringLiteral( "https://demo/{z}/{x}/{y}.png" ), options );

  CHECK( outcome.state == AssetState::Ready );
  CHECK( outcome.structure.service == RemoteMapService::Xyz );
  // The probe substituted a real z/x/y triplet to fetch a single tile.
  CHECK( fetcher.lastUrl.contains( QStringLiteral( ".png" ) ) );
  CHECK_FALSE( fetcher.lastUrl.contains( QStringLiteral( "{z}" ) ) );
}

TEST_CASE( "QgisNetworkProbe reports Offline when XYZ tile returns 404",
           "[remote_map][network_probe]" )
{
  StubFetcher fetcher;
  fetcher.response.httpStatus = 404;

  QgisNetworkProbe probe( &fetcher );
  const auto outcome =
      probe.probe( RemoteMapService::Xyz,
                   QStringLiteral( "https://demo/{z}/{x}/{y}.png" ), {} );

  // A 404 on the probe tile is treated as unreachable (the template may be
  // wrong, or the z-range off) rather than an auth issue.
  CHECK( outcome.state == AssetState::Offline );
}

TEST_CASE( "QgisNetworkProbe with no fetcher reports Offline (NoNetworkProbe parity)",
           "[remote_map][network_probe]" )
{
  // A null fetcher is the conservative default: behave like NoNetworkProbe so an
  // unwired host still registers assets Offline rather than crashing.
  QgisNetworkProbe probe( nullptr );
  const auto outcome = probe.probe( RemoteMapService::Wms,
                                    QStringLiteral( "https://demo/wms" ),
                                    wmsOptions() );

  CHECK( outcome.state == AssetState::Offline );
  CHECK( outcome.structure.service == RemoteMapService::Wms );
}
