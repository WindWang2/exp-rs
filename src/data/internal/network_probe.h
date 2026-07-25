#pragma once

#include <QMap>
#include <QString>

#include "../asset_types.h"
#include "../data_asset.h"

namespace sicnu::data::internal
{

/// Outcome of a metadata + reachability probe against a web-map service.
///
/// `state` is the resolution outcome the provider reports: Ready when the
/// service answers, Offline when unreachable, AuthenticationRequired when the
/// service demands credentials and none/bad are configured, Error only for a
/// malformed-service response (not transient failure). `structure` carries the
/// service-derived metadata; it is populated to whatever the probe could learn
/// (possibly partial for a non-Ready outcome).
struct ProbeOutcome
{
  AssetState state = AssetState::Offline;
  RemoteMapStructure structure;
};

/// Injectable I/O seam for the remote-map source providers (WMS/WMTS/TMS/XYZ).
///
/// The probe is metadata + reachability only — it NEVER fetches pixels. A real
/// implementation performs a short-timeout HTTP HEAD/GET (GetCapabilities for
/// WMS/WMTS, a single-tile fetch for XYZ/TMS); tests inject a stub returning
/// canned outcomes so the providers are unit-testable without the network.
///
/// Keeping this in src/data lets the providers (which live in src/data) stay
/// network-free in their own compilation; the concrete Qt-Network-backed probe
/// is provided by the host (src/app), avoiding a Qt Network link in src/data.
class NetworkProbe
{
  public:
    virtual ~NetworkProbe() = default;

    /// Probe `url` (a service base URL or tile template) for `service`, with
    /// the caller's `options` (layer/crs/format/z-range). Returns the outcome
    /// the provider turns into a ResolvedSource.
    virtual ProbeOutcome probe( RemoteMapService service,
                                const QString &url,
                                const QMap<QString, QString> &options ) const = 0;
};

/// A probe that reports every service Offline. This is the conservative
/// default when no host-injected probe is set: an asset still registers
/// (so it survives in the project) but resolves Offline until the host wires a
/// real probe. It keeps src/data free of a Qt Network dependency.
class NoNetworkProbe final : public NetworkProbe
{
  public:
    ProbeOutcome probe( RemoteMapService service,
                        const QString &url,
                        const QMap<QString, QString> &options ) const override
    {
      ( void ) url;
      ( void ) options;
      ProbeOutcome outcome;
      outcome.state = AssetState::Offline;
      outcome.structure.service = service;
      return outcome;
    }
};

} // namespace sicnu::data::internal
