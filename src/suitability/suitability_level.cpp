#include "suitability_level.h"

namespace sicnu::suitability
{

QString suitabilityLevelToString( SuitabilityLevel level )
{
    switch ( level )
    {
        case SuitabilityLevel::Suitable:
            return QStringLiteral( "suitable" );
        case SuitabilityLevel::Marginal:
            return QStringLiteral( "marginal" );
        case SuitabilityLevel::Unsuitable:
            return QStringLiteral( "unsuitable" );
        case SuitabilityLevel::Unknown:
            return QStringLiteral( "unknown" );
    }
    return QStringLiteral( "unknown" );
}

std::optional<SuitabilityLevel> suitabilityLevelFromString( const QString &text )
{
    if ( text == QLatin1String( "suitable" ) )
        return SuitabilityLevel::Suitable;
    if ( text == QLatin1String( "marginal" ) )
        return SuitabilityLevel::Marginal;
    if ( text == QLatin1String( "unsuitable" ) )
        return SuitabilityLevel::Unsuitable;
    if ( text == QLatin1String( "unknown" ) )
        return SuitabilityLevel::Unknown;
    return std::nullopt;
}

int suitabilitySeverityRank( SuitabilityLevel level )
{
    switch ( level )
    {
        case SuitabilityLevel::Suitable:
            return 0;
        case SuitabilityLevel::Unknown:
            return 1;
        case SuitabilityLevel::Marginal:
            return 2;
        case SuitabilityLevel::Unsuitable:
            return 3;
    }
    return 1;
}

SuitabilityLevel aggregateSuitabilityLevels( const QVector<SuitabilityLevel> &levels )
{
    SuitabilityLevel worst = SuitabilityLevel::Unknown;
    bool haveAny = false;
    for ( const SuitabilityLevel level : levels )
    {
        if ( !haveAny || suitabilitySeverityRank( level ) > suitabilitySeverityRank( worst ) )
            worst = level;
        haveAny = true;
    }
    return worst;
}

} // namespace sicnu::suitability
