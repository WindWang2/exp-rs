/***************************************************************************
 * availability_facts.cpp — availability explanation presentation
 ***************************************************************************/
#include "help/availability_facts.h"

namespace sicnu::help
{

QStringList AvailabilityExplanation::factLines() const
{
    QStringList lines;
    for ( const AvailabilityFact &fact : facts )
        lines << QStringLiteral( "%1 %2" ).arg( fact.satisfied ? QStringLiteral( "✓" )
                                                               : QStringLiteral( "✗" ),
                                                 fact.label );
    return lines;
}

QString AvailabilityExplanation::toText() const
{
    QString text;
    if ( available )
        text += QStringLiteral( "可用" );
    else
        text += QStringLiteral( "不可用" );

    if ( !facts.isEmpty() ) {
        text += u'\n';
        text += factLines().join( u'\n' );
    }
    if ( !available && !suggestedCommandTitle.isEmpty() ) {
        text += QStringLiteral( "\n建议下一步 → %1" ).arg( suggestedCommandTitle );
    } else if ( !available && !flatReason.isEmpty() ) {
        text += u'\n';
        text += flatReason;
    }
    return text;
}

QString AvailabilityExplanation::toConciseLine() const
{
    if ( available )
        return QString();
    if ( !flatReason.isEmpty() )
        return flatReason;
    QStringList unsatisfied;
    for ( const AvailabilityFact &fact : facts ) {
        if ( !fact.satisfied )
            unsatisfied << fact.label;
    }
    if ( unsatisfied.isEmpty() )
        return QStringLiteral( "当前不可用" );
    return QStringLiteral( "需要：%1" ).arg( unsatisfied.join( QStringLiteral( "；" ) ) );
}

} // namespace sicnu::help
