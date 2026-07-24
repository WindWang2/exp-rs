#include "source_descriptor.h"

#include <utility>

namespace sicnu::data
{

SourceKey::SourceKey( QString providerKey,
                      QString canonicalSource,
                      QString subdataset,
                      QMap<QString, QString> dataOptions )
  : m_providerKey( std::move( providerKey ) )
  , m_canonicalSource( std::move( canonicalSource ) )
  , m_subdataset( std::move( subdataset ) )
  , m_dataOptions( std::move( dataOptions ) )
{
}

SourceKey SourceDescriptor::sourceKey() const
{
  return SourceKey( providerKey, canonicalSource, subdataset, dataOptions );
}

} // namespace sicnu::data
