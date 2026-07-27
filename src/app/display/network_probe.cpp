#include "network_probe.h"

#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

#include <qgsapplication.h>
#include <qgsauthmanager.h>
#include <qgsnetworkaccessmanager.h>
#include <qgsnetworkreply.h>

namespace sicnu::display
{

namespace
{

/// Builds the WMS/WMTS GetCapabilities URL (KVP form) from a service base URL.
/// The base may already carry a query string; append rather than clobber.
QString getCapabilitiesUrl( const QString &baseUrl, data::RemoteMapService service )
{
  QUrl url( baseUrl );
  QUrlQuery query( url );
  query.addQueryItem( QStringLiteral( "SERVICE" ),
                      service == data::RemoteMapService::Wmts
                          ? QStringLiteral( "WMTS" )
                          : QStringLiteral( "WMS" ) );
  query.addQueryItem( QStringLiteral( "REQUEST" ),
                      QStringLiteral( "GetCapabilities" ) );
  // WMS 1.3.0 for WMS; WMTS 1.0.0 advertises its own version in the response.
  query.addQueryItem( QStringLiteral( "VERSION" ),
                      service == data::RemoteMapService::Wmts
                          ? QStringLiteral( "1.0.0" )
                          : QStringLiteral( "1.3.0" ) );
  url.setQuery( query );
  return url.toString();
}

/// Substitutes the {z}/{x}/{y} placeholders in a tile template with a known
/// center triplet at the declared minimum zoom (or 0 if none). Used to probe a
/// single representative tile for reachability.
QString substitutedTileUrl( const QString &templateUrl,
                            const QMap<QString, QString> &options )
{
  // Probe at the minimum declared zoom (or 0); a center tile at low zoom is
  // the most likely to exist on any reasonable service.
  const int z = options.value( QStringLiteral( "zMin" ), QStringLiteral( "0" ) ).toInt();
  // A rough center for z (the probe only needs SOME valid tile, not a precise
  // geographic center; 2^z / 2 is within bounds for z >= 0).
  const int span = z > 0 ? ( 1 << z ) : 1;
  const int x = span / 2;
  const int y = span / 2;
  QString url = templateUrl;
  url.replace( QStringLiteral( "{z}" ), QString::number( z ) );
  url.replace( QStringLiteral( "{x}" ), QString::number( x ) );
  url.replace( QStringLiteral( "{y}" ), QString::number( y ) );
  return url;
}

/// Maps an HTTP status to the conservative probe state for a 2xx-or-error
/// reply body that could not be parsed as capabilities (or for XYZ/TMS, which
/// has no body to parse).
data::AssetState stateForHttpStatus( int httpStatus )
{
  if ( httpStatus == 401 || httpStatus == 403 )
    return data::AssetState::AuthenticationRequired;
  if ( httpStatus >= 200 && httpStatus < 300 )
    return data::AssetState::Ready;
  // 404, 5xx, etc.: treat as unreachable, not malformed (the service answered,
  // just not with the resource we wanted).
  return data::AssetState::Offline;
}

} // namespace

QgisNetworkProbe::QgisNetworkProbe( const CapabilitiesFetcher *fetcher )
  : m_fetcher( fetcher )
{
}

data::internal::ProbeOutcome
QgisNetworkProbe::probe( data::RemoteMapService service,
                         const QString &url,
                         const QMap<QString, QString> &options ) const
{
  data::internal::ProbeOutcome outcome;
  outcome.structure.service = service;

  // A null fetcher is the conservative default (NoNetworkProbe parity): an
  // unwired host still registers assets Offline rather than crashing.
  if ( m_fetcher == nullptr )
  {
    outcome.state = data::AssetState::Offline;
    return outcome;
  }

  const QString authConfigId = options.value( QStringLiteral( "authConfigId" ) );

  if ( service == data::RemoteMapService::Wms ||
       service == data::RemoteMapService::Wmts )
  {
    const std::optional<CapabilitiesResponse> response =
        m_fetcher->fetch( getCapabilitiesUrl( url, service ), authConfigId );
    if ( !response )
    {
      outcome.state = data::AssetState::Offline; // network error / timeout
      return outcome;
    }
    if ( response->httpStatus != 200 )
    {
      outcome.state = stateForHttpStatus( response->httpStatus );
      return outcome;
    }
    // 200: parse the capabilities body. WMS and WMTS use distinct parsers.
    const data::RemoteMapStructure structure =
        service == data::RemoteMapService::Wmts
            ? parseWmtsCapabilities( response->body )
            : parseWmsCapabilities( response->body );
    if ( !structure.valid )
    {
      // 200 OK but not a capabilities document (e.g. an error page): the
      // service is malformed, which is a distinct failure from unreachable.
      outcome.state = data::AssetState::Error;
      return outcome;
    }
    outcome.state = data::AssetState::Ready;
    outcome.structure = structure;
    outcome.structure.service = service; // parser stamps the family for clarity
    return outcome;
  }

  // XYZ / TMS: probe a single substituted tile for reachability.
  const std::optional<CapabilitiesResponse> response =
      m_fetcher->fetch( substitutedTileUrl( url, options ), authConfigId );
  if ( !response )
  {
    outcome.state = data::AssetState::Offline;
    return outcome;
  }
  outcome.state = stateForHttpStatus( response->httpStatus );
  return outcome;
}

// ---------------------------------------------------------------------------
// Production fetcher: a thin synchronous wrapper over
// QgsNetworkAccessManager::blockingGet, which applies auth (via
// QgsAuthManager) and proxy settings. Short transfer timeout so a slow server
// does not block registration indefinitely.
// ---------------------------------------------------------------------------

/// Short transfer timeout for capabilities/tile probes. The probe runs on the
/// GUI thread inside registerSource, so this caps the worst-case block.
constexpr int kProbeTimeoutMs = 3000;

class QgsBlockingCapabilitiesFetcher final : public CapabilitiesFetcher
{
  public:
    std::optional<CapabilitiesResponse>
    fetch( const QString &url, const QString &authConfigId ) const override
    {
      QNetworkRequest request{ QUrl( url ) };
      request.setTransferTimeout( kProbeTimeoutMs );
      // QgsNetworkAccessManager::blockingGet applies the authConfigId via
      // QgsAuthManager and blocks until the reply returns (safe on the GUI
      // thread). It returns a content snapshot, not a live QNetworkReply.
      const QgsNetworkReplyContent reply =
          QgsNetworkAccessManager::instance()->blockingGet( request, authConfigId );
      if ( reply.error() != QNetworkReply::NoError &&
           reply.attribute( QNetworkRequest::HttpStatusCodeAttribute ).isNull() )
      {
        // No HTTP status means no reply arrived (timeout / DNS / connection
        // refused) — the service is unreachable, not erroring.
        return std::nullopt;
      }
      CapabilitiesResponse response;
      response.httpStatus =
          reply.attribute( QNetworkRequest::HttpStatusCodeAttribute ).toInt();
      response.body = reply.content();
      response.contentType =
          QString::fromUtf8( reply.rawHeader( QByteArrayLiteral( "Content-Type" ) ) );
      return response;
    }
};

std::unique_ptr<CapabilitiesFetcher> makeProductionCapabilitiesFetcher()
{
  return std::make_unique<QgsBlockingCapabilitiesFetcher>();
}

} // namespace sicnu::display
