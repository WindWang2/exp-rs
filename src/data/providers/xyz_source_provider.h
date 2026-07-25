#pragma once

#include "../internal/network_probe.h"
#include "../internal/source_provider.h"

namespace sicnu::data::providers
{

/// Source provider for stateless XYZ tile services (providerKey "xyz"). A tile
/// URL template plus z-range is resolved with a single-tile reachability probe
/// (no GetCapabilities round-trip). The probe is metadata + reachability only;
/// the provider never fetches pixels and advertises only Renderable |
/// OfflineCacheable (parent spec line 109).
///
/// Stateless: no back-reference to the Data Manager (unlike the VRT provider).
/// The probe is injected so tests do not touch the network; when none is given
/// the provider uses a NoNetworkProbe that resolves Offline, keeping src/data
/// free of a Qt Network dependency. The host injects the real HTTP probe.
class XyzSourceProvider final : public internal::SourceProvider
{
  public:
    /// Constructs with `probe` (ownership not taken; must outlive the provider).
    /// A null `probe` uses a NoNetworkProbe (conservative Offline default).
    explicit XyzSourceProvider( const internal::NetworkProbe *probe = nullptr );

    bool supports( const SourceDescriptor &source ) const override;
    Result<internal::ResolvedSource> resolve(
      const SourceDescriptor &source ) const override;

  private:
    const internal::NetworkProbe *m_probe;
};

} // namespace sicnu::data::providers
