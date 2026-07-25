#pragma once

#include "../internal/network_probe.h"
#include "../internal/source_provider.h"

namespace sicnu::data::providers
{

/// Source provider for TMS (providerKey "tms"). Stateless template probe like
/// XYZ (#62) but TMS uses a y-origin-flipped tile scheme; the probe owns that
/// detail. Capabilities are the closed Renderable | OfflineCacheable set.
class TmsSourceProvider final : public internal::SourceProvider
{
  public:
    /// Constructs with `probe` (ownership not taken; must outlive the provider).
    /// A null `probe` uses a NoNetworkProbe (conservative Offline default).
    explicit TmsSourceProvider( const internal::NetworkProbe *probe = nullptr );

    bool supports( const SourceDescriptor &source ) const override;
    Result<internal::ResolvedSource> resolve(
      const SourceDescriptor &source ) const override;

  private:
    const internal::NetworkProbe *m_probe;
};

} // namespace sicnu::data::providers
