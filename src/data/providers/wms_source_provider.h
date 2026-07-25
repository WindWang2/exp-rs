#pragma once

#include "../internal/network_probe.h"
#include "../internal/source_provider.h"

namespace sicnu::data::providers
{

/// Source provider for WMS (providerKey "wms"). Resolves via a
/// GetCapabilities-style metadata+reachability probe (the injected
/// NetworkProbe owns the capabilities parsing). Stateless. Capabilities are
/// the closed Renderable | OfflineCacheable set (parent spec line 109). The
/// probe is injected so tests do not touch the network.
class WmsSourceProvider final : public internal::SourceProvider
{
  public:
    /// Constructs with `probe` (ownership not taken; must outlive the provider).
    /// A null `probe` uses a NoNetworkProbe (conservative Offline default).
    explicit WmsSourceProvider( const internal::NetworkProbe *probe = nullptr );

    bool supports( const SourceDescriptor &source ) const override;
    Result<internal::ResolvedSource> resolve(
      const SourceDescriptor &source ) const override;

  private:
    const internal::NetworkProbe *m_probe;
};

} // namespace sicnu::data::providers
