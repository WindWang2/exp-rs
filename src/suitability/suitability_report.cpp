#include "suitability_report.h"

#include <algorithm>

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>

namespace sicnu::suitability
{

void SuitabilityReport::addCriterion( SuitabilityCriterion criterion )
{
    for ( int i = 0; i < m_criteria.size(); ++i )
    {
        if ( m_criteria.at( i ).id == criterion.id )
        {
            m_criteria[i] = std::move( criterion );
            std::sort( m_criteria.begin(), m_criteria.end(), criterionIdLessThan );
            return;
        }
    }
    auto position = std::lower_bound( m_criteria.begin(), m_criteria.end(), criterion,
                                      []( const SuitabilityCriterion &existing,
                                         const SuitabilityCriterion &value )
                                      { return criterionIdLessThan( existing, value ); } );
    m_criteria.insert( position, std::move( criterion ) );
}

SuitabilityLevel SuitabilityReport::overallLevel() const
{
    QVector<SuitabilityLevel> applicableLevels;
    applicableLevels.reserve( m_criteria.size() );
    for ( const SuitabilityCriterion &criterion : m_criteria )
    {
        if ( criterion.applicable )
            applicableLevels.append( criterion.level );
    }
    return aggregateSuitabilityLevels( applicableLevels );
}

QVector<SuitabilityGap> SuitabilityReport::allGaps() const
{
    QVector<SuitabilityGap> gaps;
    for ( const SuitabilityCriterion &criterion : m_criteria )
        gaps += criterion.gaps;

    std::sort( gaps.begin(), gaps.end(),
               []( const SuitabilityGap &a, const SuitabilityGap &b ) { return a.id < b.id; } );
    // Exact duplicates (same id, criterion, description and evidence)
    // collapse: a repeated input line must not read as two findings.
    gaps.erase( std::unique( gaps.begin(), gaps.end(),
                             []( const SuitabilityGap &a, const SuitabilityGap &b )
                             {
                                 return a.id == b.id && a.criterionId == b.criterionId
                                        && a.description == b.description
                                        && a.evidence == b.evidence;
                             } ),
                gaps.end() );
    return gaps;
}

QString SuitabilityReport::contentDigest() const
{
    const QJsonDocument document( toJson() );
    const QByteArray canonical = document.toJson( QJsonDocument::Compact );
    return QString::fromLatin1(
        QCryptographicHash::hash( canonical, QCryptographicHash::Sha256 ).toHex() );
}

QJsonObject SuitabilityReport::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "schema_version" ), kSuitabilityReportSerializationVersion );
    if ( !m_datasetVersionId.isEmpty() )
        json.insert( QStringLiteral( "dataset_version_id" ), m_datasetVersionId );
    if ( !m_sceneIds.isEmpty() )
    {
        QJsonArray scenesArray;
        for ( const QString &sceneId : m_sceneIds )
            scenesArray.append( sceneId );
        json.insert( QStringLiteral( "scene_ids" ), scenesArray );
    }
    if ( !m_goalDigest.isEmpty() )
        json.insert( QStringLiteral( "goal_digest" ), m_goalDigest );

    QJsonArray criteriaArray;
    for ( const SuitabilityCriterion &criterion : m_criteria )
        criteriaArray.append( criterion.toJson() );
    json.insert( QStringLiteral( "criteria" ), criteriaArray );
    return json;
}

sicnu::data::Result<SuitabilityReport> SuitabilityReport::fromJson( const QJsonObject &json )
{
    const int version = json.value( QStringLiteral( "schema_version" ) ).toInt( -1 );
    if ( version != kSuitabilityReportSerializationVersion )
    {
        return sicnu::data::Result<SuitabilityReport>::failure( sicnu::data::Diagnostic{
            QStringLiteral( "suitability.report_schema" ),
            QStringLiteral( "unsupported report schema_version %1" ).arg( version ),
            sicnu::data::DiagnosticSeverity::Error } );
    }

    SuitabilityReport report;
    report.setDatasetVersionId(
        json.value( QStringLiteral( "dataset_version_id" ) ).toString() );

    const QJsonArray scenesArray = json.value( QStringLiteral( "scene_ids" ) ).toArray();
    QStringList sceneIds;
    for ( const QJsonValue &sceneValue : scenesArray )
        sceneIds.append( sceneValue.toString() );
    report.setSceneIds( sceneIds );

    report.setGoalDigest( json.value( QStringLiteral( "goal_digest" ) ).toString() );

    if ( !json.value( QStringLiteral( "criteria" ) ).isArray() )
    {
        return sicnu::data::Result<SuitabilityReport>::failure( sicnu::data::Diagnostic{
            QStringLiteral( "suitability.report_invalid" ),
            QStringLiteral( "report criteria must be an array" ),
            sicnu::data::DiagnosticSeverity::Error } );
    }
    const QJsonArray criteriaArray = json.value( QStringLiteral( "criteria" ) ).toArray();
    for ( const QJsonValue &criterionValue : criteriaArray )
    {
        auto criterion = SuitabilityCriterion::fromJson( criterionValue.toObject() );
        if ( !criterion.has_value() )
        {
            return sicnu::data::Result<SuitabilityReport>::failure( sicnu::data::Diagnostic{
                QStringLiteral( "suitability.report_invalid" ),
                QStringLiteral( "report carries an unparsable criterion: %1" )
                    .arg( criterion.diagnostics().first().message ),
                sicnu::data::DiagnosticSeverity::Error } );
        }
        report.addCriterion( criterion.value() );
    }

    return sicnu::data::Result<SuitabilityReport>::success( report );
}

} // namespace sicnu::suitability
