#pragma once

#include <optional>

#include <QByteArray>
#include <QMap>
#include <QString>

#include "data/internal/network_probe.h"
#include "remote_map_capabilities_parser.h"

namespace sicnu::display
{

/// A fetched capabilities / tile response. `httpStatus` is the HTTP status code
/// (0 when no reply arrived — network error / timeout); `body` is the raw
/// response bytes; `contentType` is the Content-Type header value.
struct CapabilitiesResponse
{
  int httpStatus = 0;
  QByteArray body;
  QString contentType;
};

/// Injectable I/O seam for the #66 NetworkProbe. The concrete probe delegates
/// every HTTP request here so its state-mapping logic is unit-testable without
/// the network. Production uses QgsBlockingCapabilitiesFetcher (a thin wrapper
/// over QgsNetworkAccessManager::blockingGet); tests inject a stub returning
/// canned responses.
///
/// `authConfigId` is carried verbatim — the fetcher (via QgsAuthManager) applies
/// the credentials; the probe never handles credential material.
class CapabilitiesFetcher
{
  public:
    virtual ~CapabilitiesFetcher() = default;

    /// Fetch `url` with the given authConfigId (possibly empty for an open
    /// service). Returns nullopt when no reply arrived (network error /
    /// timeout); otherwise the response (including non-2xx statuses, which the
    /// probe maps to AuthenticationRequired/Offline).
    virtual std::optional<CapabilitiesResponse>
    fetch( const QString &url, const QString &authConfigId ) const = 0;
};

/// The host-side (src/app) NetworkProbe: probes a web-map service for metadata
/// + reachability by fetching GetCapabilities (WMS/WMTS) or a single tile
/// (XYZ/TMS) through an injectable CapabilitiesFetcher.
///
/// State mapping:
/// - WMS/WMTS: 200 + parseable capabilities XML -> Ready (structure populated);
///   200 + unparseable body -> Error; 401/403 -> AuthenticationRequired;
///   network error / other non-2xx -> Offline.
/// - XYZ/TMS: 200 -> Ready; 401/403 -> AuthenticationRequired; else Offline.
///
/// A null fetcher behaves like NoNetworkProbe (Offline) so an unwired host
/// still registers assets rather than crashing.
class QgisNetworkProbe final : public data::internal::NetworkProbe
{
  public:
    /// `fetcher` is non-owning and must outlive the probe (or be null). Tests
    /// inject a stub; production injects a QgsBlockingCapabilitiesFetcher.
    explicit QgisNetworkProbe( const CapabilitiesFetcher *fetcher );

    data::internal::ProbeOutcome probe( data::RemoteMapService service,
                                        const QString &url,
                                        const QMap<QString, QString> &options ) const override;

  private:
    const CapabilitiesFetcher *m_fetcher;
};

/// Constructs the production CapabilitiesFetcher (QgsBlockingCapabilitiesFetcher,
/// a synchronous wrapper over QgsNetworkAccessManager::blockingGet with a short
/// transfer timeout). The host owns the returned fetcher and injects it into a
/// QgisNetworkProbe, which it then injects into the DataManager's remote-map
/// providers.
std::unique_ptr<CapabilitiesFetcher> makeProductionCapabilitiesFetcher();

} // namespace sicnu::display
