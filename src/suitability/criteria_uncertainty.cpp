#include "criteria_uncertainty.h"

#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>

namespace sicnu::suitability
{

namespace
{

/// Diagnostic codes that mark a raw measurement itself as invalid (broken
/// numbers: non-finite/non-positive GSD, cloud cover outside [0, 100],
/// non-finite geotransform terms, an unrepresentable AOI ratio). These are
/// the "measurement conflicts": the criterion graded anyway, but the
/// evidence pool was contaminated, so the report may not present itself as
/// merely marginal. Goal-side malformations (e.g. a degenerate AOI extent)
/// and failed comparisons stay ordinary non-blocking diagnostics — their
/// criteria already refuse to grade (Unknown).
bool isMeasurementConflict( const QString &code )
{
    return code == QLatin1String( "suitability.gsd_invalid" )
           || code == QLatin1String( "suitability.cloud_out_of_range" )
           || code == QLatin1String( "suitability.grid_abnormal_input" )
           || code == QLatin1String( "suitability.aoi_area_not_representable" );
}

/// One named uncertainty statement. Ordering by (code, detail) makes the
/// serialized array deterministic regardless of evaluation order.
struct UncertaintySource
{
    QString code;
    QString detail;
    bool blocking = false;

    bool operator<( const UncertaintySource &other ) const
    {
        if ( code != other.code )
            return code < other.code;
        return detail < other.detail;
    }
};

QJsonObject sourceToJson( const UncertaintySource &source )
{
    QJsonObject json;
    json.insert( QStringLiteral( "code" ), source.code );
    json.insert( QStringLiteral( "detail" ), source.detail );
    json.insert( QStringLiteral( "blocking" ), source.blocking );
    return json;
}

} // namespace

SuitabilityCriterion assessUncertaintySources(
    const QVector<SuitabilityCriterion> &priorCriteria,
    const std::optional<DatasetFacts> &facts )
{
    SuitabilityCriterion criterion;
    criterion.id = QStringLiteral( "uncertainty.sources" );
    // Always applicable: "how much of this report is unverified" is asked of
    // every assessment, and the empty answer (no sources) is a real answer.
    criterion.applicable = true;

    QVector<UncertaintySource> sources;

    for ( const SuitabilityCriterion &prior : priorCriteria )
    {
        for ( const QString &note : prior.notes )
        {
            sources.append( UncertaintySource{
                QStringLiteral( "uncertainty.note.%1" ).arg( prior.id ), note, false } );
        }
        for ( const sicnu::data::Diagnostic &diagnostic : prior.diagnostics )
        {
            if ( isMeasurementConflict( diagnostic.code ) )
            {
                sources.append( UncertaintySource{
                    QStringLiteral( "uncertainty.measurement_conflict" ),
                    QStringLiteral( "%1: %2: %3" )
                        .arg( prior.id, diagnostic.code, diagnostic.message ),
                    true } );
                continue;
            }
            sources.append( UncertaintySource{ QStringLiteral( "uncertainty.diagnostic" ),
                                               QStringLiteral( "%1: %2" )
                                                   .arg( prior.id, diagnostic.message ),
                                               false } );
        }
    }

    if ( facts.has_value() && facts->factsTruncated )
    {
        sources.append( UncertaintySource{
            QStringLiteral( "uncertainty.facts_truncated" ),
            QStringLiteral( "dataset facts were truncated by the provider" ), false } );
    }

    // Same-code entries stay (each note and diagnostic is its own statement)
    // but a repeated detail is the same statement twice: collapse it, and
    // keep the whole list sorted for byte-stable reports.
    std::sort( sources.begin(), sources.end() );
    sources.erase( std::unique( sources.begin(), sources.end(),
                                []( const UncertaintySource &a, const UncertaintySource &b )
                                { return a.code == b.code && a.detail == b.detail; } ),
                   sources.end() );

    QJsonArray sourcesArray;
    int blockingCount = 0;
    for ( const UncertaintySource &source : sources )
    {
        sourcesArray.append( sourceToJson( source ) );
        if ( source.blocking )
            ++blockingCount;
    }

    criterion.evidence.insert( QStringLiteral( "sources" ), sourcesArray );
    criterion.evidence.insert( QStringLiteral( "count" ), sources.size() );
    criterion.evidence.insert( QStringLiteral( "blocking_count" ), blockingCount );

    if ( blockingCount > 0 )
    {
        criterion.level = SuitabilityLevel::Unsuitable;
        criterion.summary = QStringLiteral(
            "%1 measurement conflict(s) make part of the assessment untrustworthy." )
                                .arg( blockingCount );
    }
    else if ( !sources.isEmpty() )
    {
        criterion.level = SuitabilityLevel::Marginal;
        criterion.summary = QStringLiteral(
            "%1 undigested assumption source(s) accompany the verdicts; treat them as provisional." )
                                .arg( sources.size() );
    }
    else
    {
        criterion.level = SuitabilityLevel::Suitable;
        criterion.summary = QStringLiteral( "No unresolved assumptions or broken measurements were recorded." );
    }
    return criterion;
}

} // namespace sicnu::suitability
