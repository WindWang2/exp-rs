#include "suitability_teaching.h"

#include "suitability_level.h"

#include <QHash>

#include <algorithm>

namespace sicnu::suitability
{

namespace
{

/// Human-facing criterion names for the teaching narrative. Unknown ids
/// (a future criterion, a foreign report) fall back to the machine id —
/// the narrative degrades to honest, not broken.
QString criterionDisplayName( const QString &id )
{
    static const QHash<QString, const char *> names = {
        { QStringLiteral( "grid.compatibility" ), "Pixel grid compatibility" },
        { QStringLiteral( "labels.availability" ), "Label availability" },
        { QStringLiteral( "model.compatibility" ), "Model input compatibility" },
        { QStringLiteral( "quality.cloud" ), "Cloud cover" },
        { QStringLiteral( "spatial.coverage" ), "Spatial coverage" },
        { QStringLiteral( "spatial.resolution" ), "Spatial resolution" },
        { QStringLiteral( "spectral.bands" ), "Spectral bands" },
        { QStringLiteral( "temporal.coverage" ), "Temporal coverage" },
        { QStringLiteral( "temporal.density" ), "Temporal density" },
        { QStringLiteral( "temporal.seasonality" ), "Seasonal coverage" },
        { QStringLiteral( "uncertainty.sources" ), "Assessment uncertainty" },
    };
    const auto it = names.constFind( id );
    return it != names.constEnd() ? QString::fromLatin1( it.value() ) : id;
}

/// The gaps a paragraph quotes, sorted by id so the sentence is stable for
/// identical evidence regardless of the order gaps were appended in.
QVector<SuitabilityGap> sortedGaps( const SuitabilityCriterion &criterion )
{
    QVector<SuitabilityGap> gaps = criterion.gaps;
    std::sort( gaps.begin(), gaps.end(),
               []( const SuitabilityGap &a, const SuitabilityGap &b ) { return a.id < b.id; } );
    return gaps;
}

} // namespace

QString explainCriterion( const SuitabilityCriterion &criterion )
{
    const QString name = criterionDisplayName( criterion.id );
    const QString level = suitabilityLevelToString( criterion.level );

    if ( !criterion.applicable )
    {
        // "unknown, not applicable": the serialized level of a skipped
        // criterion is unknown; the sentence says it was skipped and why.
        return QStringLiteral( "%1 (%2): %3, not applicable. Not assessed: %4" )
            .arg( name, criterion.id, level, criterion.summary );
    }

    QString paragraph = QStringLiteral( "%1 (%2): %3." ).arg( name, criterion.id, level );

    if ( criterion.level == SuitabilityLevel::Unknown )
    {
        // An unknown verdict must say what could not be judged and why.
        paragraph += QStringLiteral( " Cannot judge: %1" ).arg( criterion.summary );
        for ( const QString &note : criterion.notes )
            paragraph += QStringLiteral( " (because: %1)" ).arg( note );
        return paragraph;
    }

    paragraph += QStringLiteral( " %1" ).arg( criterion.summary );
    if ( criterion.level == SuitabilityLevel::Unsuitable
         || criterion.level == SuitabilityLevel::Marginal )
    {
        const QVector<SuitabilityGap> gaps = sortedGaps( criterion );
        if ( !gaps.isEmpty() )
        {
            paragraph += QLatin1String( " Gaps:" );
            for ( const SuitabilityGap &gap : gaps )
                paragraph += QStringLiteral( " %1 (%2);" ).arg( gap.description, gap.id );
            // Swap the trailing ';' for a full stop.
            paragraph.chop( 1 );
            paragraph += QLatin1Char( '.' );
        }
    }
    return paragraph;
}

QStringList teachingExplanation( const SuitabilityReport &report )
{
    QStringList lines;

    // --- Overview ----------------------------------------------------------
    const SuitabilityLevel overall = report.overallLevel();
    const QVector<SuitabilityGap> gaps = report.allGaps();

    QString overview = QStringLiteral( "Overall: %1." ).arg( suitabilityLevelToString( overall ) );
    if ( gaps.isEmpty() )
        overview += QLatin1String( " No gaps reported." );
    else
        overview += QStringLiteral( " %1 gap(s) reported." ).arg( gaps.size() );

    // The most pressing problems: the worst criteria first (severity rank,
    // ties by criterion id), up to three, each named by its first gap.
    QVector<const SuitabilityCriterion *> problems;
    for ( const SuitabilityCriterion &criterion : report.criteria() )
    {
        if ( criterion.gaps.isEmpty() )
            continue;
        problems.append( &criterion );
    }
    std::sort( problems.begin(), problems.end(),
               []( const SuitabilityCriterion *a, const SuitabilityCriterion *b )
               {
                   const int rankA = suitabilitySeverityRank( a->level );
                   const int rankB = suitabilitySeverityRank( b->level );
                   if ( rankA != rankB )
                       return rankA > rankB;
                   return a->id < b->id;
               } );

    int named = 0;
    for ( const SuitabilityCriterion *criterion : problems )
    {
        if ( named >= 3 )
            break;
        ++named;
        QVector<SuitabilityGap> criterionGaps = sortedGaps( *criterion );
        const SuitabilityGap &first = criterionGaps.first();
        overview += named == 1 ? QStringLiteral( " Most pressing: %1: %2 (%3)" )
                                     .arg( criterionDisplayName( criterion->id ),
                                           first.description, first.id )
                               : QStringLiteral( "; %1: %2 (%3)" )
                                     .arg( criterionDisplayName( criterion->id ),
                                           first.description, first.id );
    }
    if ( named > 0 )
        overview += QLatin1Char( '.' );

    lines.append( overview );

    // --- One paragraph per criterion, canonical order -----------------------
    for ( const SuitabilityCriterion &criterion : report.criteria() )
        lines.append( explainCriterion( criterion ) );

    return lines;
}

} // namespace sicnu::suitability
