#pragma once

#include <memory>
#include <vector>

#include "source_provider.h"

namespace sicnu::geo
{
struct ConvertProgress;
}

namespace sicnu::data
{

class DataManager;

namespace internal
{

class SourceProviderRegistry
{
  public:
    SourceProviderRegistry() = default;
    ~SourceProviderRegistry() = default;

    SourceProviderRegistry( const SourceProviderRegistry & ) = delete;
    SourceProviderRegistry &operator=( const SourceProviderRegistry & ) = delete;
    SourceProviderRegistry( SourceProviderRegistry && ) noexcept = default;
    SourceProviderRegistry &operator=( SourceProviderRegistry && ) noexcept = default;

    void add( std::unique_ptr<SourceProvider> provider );
    Result<ResolvedSource> resolve( const SourceDescriptor &source ) const;
    /// Progress-aware dispatch (R30-T03 WP04): forwards to the supporting
    /// provider's ConvertProgress overload (default: sink ignored).
    Result<ResolvedSource> resolve( const SourceDescriptor &source,
                                    sicnu::geo::ConvertProgress *progress ) const;

    std::unique_ptr<DataManager> createDataManager();

  private:
    std::vector<std::unique_ptr<SourceProvider>> m_providers;
};

} // namespace internal
} // namespace sicnu::data
