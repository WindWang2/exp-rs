#include "asset_types.h"

#include <utility>

namespace sicnu::data
{

AssetId::AssetId( QUuid value )
  : m_value( std::move( value ) )
{
}

AssetId AssetId::generate()
{
  return AssetId( QUuid::createUuid() );
}

std::optional<AssetId> AssetId::fromString( const QString &text )
{
  const QUuid value( text );
  if ( value.isNull() )
    return std::nullopt;

  return AssetId( value );
}

bool AssetId::isNull() const
{
  return m_value.isNull();
}

QString AssetId::toString() const
{
  return m_value.toString( QUuid::WithoutBraces );
}

} // namespace sicnu::data
