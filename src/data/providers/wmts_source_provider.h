#pragma once

#include "../internal/network_probe.h"
#include "../internal/source_provider.h"

namespace sicnu::data::providers
{

/// Source provider for WMTS (providerKey "wmts"). Tile-matrix-set aware: the
/// probe populates pixel size + z-range from the advertised tile matrix.
/// Stateless. Capabilities are the closed Renderable | OfflineCacheable set.
class WmtsSourceProvider final : public internal::SourceProvider
{
  public:
    /// Constructs with `probe` (ownership not taken; must outlive the provider).
    /// A null `probe` uses a NoNetworkProbe (conservative Offline default).
    explicit WmtsSourceProvider( const internal::NetworkProbe *probe = nullptr );

    bool supports( const SourceDescriptor &source ) const override;
    Result<internal::ResolvedSource> resolve(
      const SourceDescriptor &source ) const override;

  private:
    const internal::NetworkProbe *m_probe;
};

} // namespace sicnu::data::providers
