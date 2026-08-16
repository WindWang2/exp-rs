#include "remote_map_provider_common.h"

#include <utility>

namespace sicnu::data::providers
{

namespace
{

Diagnostic malformedDescriptorDiagnostic( const QString &providerKey )
{
  return Diagnostic{ QStringLiteral( "source.invalid" ),
                     QStringLiteral( "A %1 source requires a service URL" ).arg( providerKey ),
                     DiagnosticSeverity::Error };
}

} // namespace

Result<internal::ResolvedSource> assembleRemoteMapResolved(
  const SourceDescriptor &source,
  RemoteMapService service,
  const QString &providerKey,
  const QString &displayName,
  const internal::NetworkProbe *probe )
{
  if ( source.canonicalSource.isEmpty() )
  {
    return Result<internal::ResolvedSource>::failure(
      malformedDescriptorDiagnostic( providerKey ) );
  }

  // Use the injected probe, or the conservative Offline default when the host
  // has not wired one (keeps src/data network-free).
  const internal::NoNetworkProbe noNetworkFallback;
  const internal::NetworkProbe &activeProbe =
    probe != nullptr ? *probe : noNetworkFallback;

  const internal::ProbeOutcome outcome =
    activeProbe.probe( service, source.canonicalSource, source.dataOptions );

  internal::ResolvedSource resolved;
  resolved.kind = AssetKind::RemoteMap;
  resolved.state = outcome.state;
  resolved.storageKind = StorageKind::Remote;
  // Canonical identity is the URL/template as given; the provider key is fixed
  // per provider (not read from the descriptor).
  resolved.canonicalSource = source.canonicalSource;
  resolved.canonicalProviderKey = providerKey;
  resolved.capabilities = kRemoteMapCapabilities;
  resolved.displayName = displayName;

  RemoteMapStructure structure = outcome.structure;
  structure.service = service;
  // z-range: for the stateless tiled services (XYZ/TMS) the caller declares it
  // in dataOptions; for WMS/WMTS the service publishes it and the probe fills
  // it in. Override each field ONLY when the caller supplied that specific key,
  // so a service-discovered range (WMTS tile matrix) survives even if the
  // caller set only one bound (a partial zMin-without-zMax must not clobber the
  // probe's zMax to 0).
  if ( source.dataOptions.contains( QStringLiteral( "zMin" ) ) )
  {
    bool ok = false;
    const int val = source.dataOptions.value( QStringLiteral( "zMin" ) ).toInt( &ok );
    if ( ok )
      structure.zMin = val;
  }
  if ( source.dataOptions.contains( QStringLiteral( "zMax" ) ) )
  {
    bool ok = false;
    const int val = source.dataOptions.value( QStringLiteral( "zMax" ) ).toInt( &ok );
    if ( ok )
      structure.zMax = val;
  }
  resolved.structure = structure;

  return Result<internal::ResolvedSource>::success( std::move( resolved ) );
}

} // namespace sicnu::data::providers
