#pragma once

#include "../asset_types.h"
#include "../data_asset.h"
#include "../data_result.h"
#include "../internal/network_probe.h"
#include "../internal/source_provider.h"
#include "../source_descriptor.h"

namespace sicnu::data::providers
{

/// The closed capability set every web-map provider advertises: renderable +
/// cacheable, never pixel readback / statistics / feature query (parent spec
/// line 109) even though QGIS represents a remote map with a QgsRasterLayer.
inline const AssetCapabilities kRemoteMapCapabilities =
  AssetCapability::Renderable | AssetCapability::OfflineCacheable;

/// Assemble the common RemoteMap ResolvedSource shared by the WMS/WMTS/TMS/XYZ
/// providers. Runs the injected `probe` (or the conservative NoNetworkProbe
/// fallback when null), stamps the result with the provider's fixed `service`,
/// `providerKey`, and a display name, and folds the caller's z-range option
/// (tiled services) into the structure. The provider key is NOT taken from the
/// descriptor — each provider owns its identity. Returns failure only for a
/// malformed descriptor (empty canonical source); every probe outcome (incl.
/// Offline / AuthenticationRequired) produces a ResolvedSource so the asset
/// records and can be re-resolved.
Result<internal::ResolvedSource> assembleRemoteMapResolved(
  const SourceDescriptor &source,
  RemoteMapService service,
  const QString &providerKey,
  const QString &displayName,
  const internal::NetworkProbe *probe );

} // namespace sicnu::data::providers
