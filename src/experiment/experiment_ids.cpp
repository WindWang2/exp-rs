// experiment_ids.cpp — id implementation.
#include "experiment_ids.h"

#include <QUuid>

namespace sicnu::experiment
{

#define SICNU_EXPERIMENT_ID_IMPL( Name )                                                      \
    Name Name::generate()                                                                     \
    {                                                                                         \
        return Name( QUuid::createUuid().toString( QUuid::WithoutBraces ) );                   \
    }                                                                                         \
                                                                                              \
    std::optional<Name> Name::fromString( const QString &text )                               \
    {                                                                                         \
        const QUuid parsed = QUuid::fromString( text );                                        \
        if ( parsed.isNull() || text.isEmpty() )                                               \
            return std::nullopt;                                                               \
        return Name( parsed.toString( QUuid::WithoutBraces ) );                                \
    }                                                                                         \
                                                                                              \
    bool Name::isNull() const                                                                 \
    {                                                                                         \
        return m_value.isEmpty() || QUuid::fromString( m_value ).isNull();                     \
    }                                                                                         \
                                                                                              \
    QString Name::toString() const                                                            \
    {                                                                                         \
        return m_value;                                                                       \
    }                                                                                         \
                                                                                              \
    Name::Name( QString value )                                                               \
      : m_value( std::move( value ) )                                                         \
    {                                                                                         \
    }

SICNU_EXPERIMENT_ID_IMPL( ExperimentId )

#undef SICNU_EXPERIMENT_ID_IMPL

RunId RunId::generate()
{
    // Filename-safe by construction: uuid without braces/hyphens is
    // [0-9a-f]{32}.
    return RunId( QUuid::createUuid().toString( QUuid::WithoutBraces )
                      .remove( QLatin1Char( '-' ) ) );
}

std::optional<RunId> RunId::fromString( const QString &text )
{
    if ( text.isEmpty() || text.size() > 64 )
        return std::nullopt;
    if ( text.startsWith( QLatin1Char( '.' ) ) )
        return std::nullopt;
    for ( const QChar &ch : text )
    {
        const bool ok = ( ch >= QLatin1Char( 'a' ) && ch <= QLatin1Char( 'z' ) ) ||
                        ( ch >= QLatin1Char( 'A' ) && ch <= QLatin1Char( 'Z' ) ) ||
                        ( ch >= QLatin1Char( '0' ) && ch <= QLatin1Char( '9' ) ) ||
                        ch == QLatin1Char( '.' ) || ch == QLatin1Char( '_' ) ||
                        ch == QLatin1Char( '-' );
        if ( !ok )
            return std::nullopt;
    }
    return RunId( text );
}

RunId::RunId( QString value )
  : m_value( std::move( value ) )
{
}

} // namespace sicnu::experiment
