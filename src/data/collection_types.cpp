#include "collection_types.h"

#include <utility>

namespace sicnu::data
{

CollectionId::CollectionId( QUuid value )
  : m_value( std::move( value ) )
{
}

CollectionId CollectionId::generate()
{
  return CollectionId( QUuid::createUuid() );
}

std::optional<CollectionId> CollectionId::fromString( const QString &text )
{
  const QUuid value( text );
  if ( value.isNull() )
    return std::nullopt;

  return CollectionId( value );
}

bool CollectionId::isNull() const
{
  return m_value.isNull();
}

QString CollectionId::toString() const
{
  return m_value.toString( QUuid::WithoutBraces );
}

} // namespace sicnu::data
