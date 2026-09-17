// dataset_qa_report.cpp — assemble multi-category QA verdicts.
#include "dataset_qa_report.h"

#include <QJsonArray>

namespace sicnu::dataset
{

namespace
{

int rank( AuditVerdict verdict )
{
    switch ( verdict )
    {
        case AuditVerdict::Fail:
            return 3;
        case AuditVerdict::Warn:
            return 2;
        case AuditVerdict::Unknown:
            return 1;
        case AuditVerdict::Pass:
            return 0;
    }
    return 1;
}

AuditVerdict worse( AuditVerdict a, AuditVerdict b )
{
    return rank( a ) >= rank( b ) ? a : b;
}

} // namespace

AuditVerdict DatasetQaReport::overallVerdict() const
{
    if ( m_categories.isEmpty() )
        return AuditVerdict::Unknown;
    AuditVerdict overall = AuditVerdict::Pass;
    for ( const DatasetQaCategory &category : m_categories )
        overall = worse( overall, category.verdict );
    return overall;
}

QJsonObject DatasetQaReport::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "schema_version" ), kDatasetQaReportSerializationVersion );
    json.insert( QStringLiteral( "dataset_version_id" ), m_datasetVersionId );
    if ( !m_splitManifestId.isEmpty() )
        json.insert( QStringLiteral( "split_manifest_id" ), m_splitManifestId );
    json.insert( QStringLiteral( "overall" ), auditVerdictToString( overallVerdict() ) );
    QJsonArray categories;
    for ( const DatasetQaCategory &category : m_categories )
    {
        QJsonObject obj;
        obj.insert( QStringLiteral( "name" ), category.name );
        obj.insert( QStringLiteral( "verdict" ), auditVerdictToString( category.verdict ) );
        obj.insert( QStringLiteral( "summary" ), category.summary );
        if ( !category.evidence.isEmpty() )
            obj.insert( QStringLiteral( "evidence" ), category.evidence );
        QJsonArray diags;
        for ( const Diagnostic &diag : category.diagnostics )
        {
            QJsonObject d;
            d.insert( QStringLiteral( "code" ), diag.code );
            d.insert( QStringLiteral( "message" ), diag.message );
            d.insert( QStringLiteral( "severity" ),
                      diag.severity == DiagnosticSeverity::Error
                          ? QStringLiteral( "error" )
                          : ( diag.severity == DiagnosticSeverity::Warning
                                  ? QStringLiteral( "warning" )
                                  : QStringLiteral( "info" ) ) );
            diags.append( d );
        }
        if ( !diags.isEmpty() )
            obj.insert( QStringLiteral( "diagnostics" ), diags );
        categories.append( obj );
    }
    json.insert( QStringLiteral( "categories" ), categories );
    return json;
}

AuditVerdict verdictFromLeakageReport( const LeakageReport &report )
{
    if ( report.auditedChecks().isEmpty() && report.findings().isEmpty() )
        return AuditVerdict::Unknown;

    bool sawError = false;
    bool sawWarn = false;
    for ( const LeakageFinding &finding : report.findings() )
    {
        if ( finding.severity == DiagnosticSeverity::Error )
            sawError = true;
        else if ( finding.severity == DiagnosticSeverity::Warning )
            sawWarn = true;
    }
    if ( sawError )
        return AuditVerdict::Fail;
    if ( sawWarn )
        return AuditVerdict::Warn;
    if ( report.auditedChecks().isEmpty() )
        return AuditVerdict::Unknown;
    return AuditVerdict::Pass;
}

DatasetQaReport buildDatasetQaReport( const DatasetQaInputs &inputs )
{
    DatasetQaReport report;
    report.setDatasetVersionId( inputs.datasetVersionId );
    report.setSplitManifestId( inputs.splitManifestId );

    // identity / freeze
    {
        DatasetQaCategory category;
        category.name = QStringLiteral( "identity" );
        if ( !inputs.versionFrozen )
        {
            category.verdict = AuditVerdict::Warn;
            category.summary = QStringLiteral( "dataset version is not frozen (still draft)" );
        }
        else if ( inputs.duplicateSampleIds > 0 )
        {
            category.verdict = AuditVerdict::Fail;
            category.summary =
                QStringLiteral( "duplicate sample ids: %1" ).arg( inputs.duplicateSampleIds );
            category.evidence.insert( QStringLiteral( "duplicate_sample_ids" ),
                                      inputs.duplicateSampleIds );
        }
        else if ( inputs.scanCapped || inputs.totalSamples > inputs.scannedSamples )
        {
            // #1004: uniqueness was computed over a bounded scan window —
            // duplicates may exist beyond it; never claim Pass on partial
            // evidence.
            category.verdict = AuditVerdict::Unknown;
            category.summary = QStringLiteral( "uniqueness evidence incomplete: scanned %1 of %2 sample(s)" )
                                   .arg( inputs.scannedSamples )
                                   .arg( inputs.totalSamples );
            category.evidence.insert( QStringLiteral( "scanned" ), inputs.scannedSamples );
            category.evidence.insert( QStringLiteral( "sample_count" ), inputs.totalSamples );
            category.evidence.insert( QStringLiteral( "scan_capped" ), inputs.scanCapped );
        }
        else
        {
            category.verdict = AuditVerdict::Pass;
            category.summary = QStringLiteral( "version frozen; sample ids unique in evidence" );
        }
        report.categories().append( category );
    }

    // crs (#1007): silent CRS gaps misplace downstream patch/geometry work.
    {
        DatasetQaCategory category;
        category.name = QStringLiteral( "crs" );
        if ( inputs.schemaCrs.isEmpty() )
        {
            category.verdict = AuditVerdict::Unknown;
            category.summary =
                QStringLiteral( "schema CRS empty (mixed/unspecified)" );
        }
        else
        {
            category.evidence.insert( QStringLiteral( "schema_crs" ), inputs.schemaCrs );
            QStringList conflicts;
            for ( const QString &crs : inputs.distinctSampleCrs )
                if ( crs != inputs.schemaCrs )
                    conflicts.append( crs );
            if ( !conflicts.isEmpty() )
            {
                category.verdict = AuditVerdict::Warn;
                category.summary = QStringLiteral( "%1 sample CRS string(s) disagree with schema CRS" )
                                       .arg( conflicts.size() );
                category.evidence.insert( QStringLiteral( "conflicting_sample_crs" ),
                                          QJsonArray::fromStringList( conflicts ) );
            }
            else
            {
                category.verdict = AuditVerdict::Pass;
                category.summary =
                    QStringLiteral( "schema CRS declared; scanned sample CRS agrees or is unspecified" );
            }
        }
        report.categories().append( category );
    }

    // composition / imbalance
    {
        DatasetQaCategory category;
        category.name = QStringLiteral( "composition" );
        category.evidence = inputs.composition.toJson();
        if ( inputs.composition.sampleCount == 0 )
        {
            category.verdict = AuditVerdict::Unknown;
            category.summary = QStringLiteral( "no composition rows provided" );
        }
        else if ( !inputs.imbalances.isEmpty() )
        {
            category.verdict = AuditVerdict::Warn;
            category.summary =
                QStringLiteral( "%1 imbalance finding(s)" ).arg( inputs.imbalances.size() );
            QJsonArray arr;
            for ( const ImbalanceFinding &finding : inputs.imbalances )
            {
                QJsonObject obj;
                obj.insert( QStringLiteral( "dimension" ), finding.dimension );
                obj.insert( QStringLiteral( "detail" ), finding.detail );
                arr.append( obj );
            }
            category.evidence.insert( QStringLiteral( "imbalances" ), arr );
        }
        else
        {
            category.verdict = AuditVerdict::Pass;
            category.summary = QStringLiteral( "composition within imbalance policy" );
        }
        report.categories().append( category );
    }

    // labels
    {
        DatasetQaCategory category;
        category.name = QStringLiteral( "labels" );
        bool sawError = false;
        bool sawWarn = false;
        for ( const LabelQualityFinding &finding : inputs.labelFindings )
        {
            if ( finding.severity == DiagnosticSeverity::Error )
                sawError = true;
            else if ( finding.severity == DiagnosticSeverity::Warning )
                sawWarn = true;
            category.diagnostics.append( Diagnostic{ finding.code, finding.message, finding.severity } );
        }
        if ( sawError )
        {
            category.verdict = AuditVerdict::Fail;
            category.summary = QStringLiteral( "label QA errors present" );
        }
        else if ( sawWarn )
        {
            category.verdict = AuditVerdict::Warn;
            category.summary = QStringLiteral( "label QA warnings present" );
        }
        else if ( !inputs.labelsAudited
                  || ( inputs.labelFindings.isEmpty() && inputs.composition.sampleCount == 0 ) )
        {
            category.verdict = AuditVerdict::Unknown;
            category.summary = QStringLiteral( "label QA not run" );
        }
        else
        {
            category.verdict = AuditVerdict::Pass;
            category.summary = QStringLiteral( "label QA clean" );
        }
        report.categories().append( category );
    }

    // leakage
    {
        DatasetQaCategory category;
        category.name = QStringLiteral( "leakage" );
        if ( !inputs.leakage )
        {
            category.verdict = AuditVerdict::Unknown;
            category.summary = QStringLiteral( "leakage audit not provided" );
        }
        else
        {
            category.verdict = verdictFromLeakageReport( *inputs.leakage );
            category.summary =
                QStringLiteral( "%1 finding(s); checks=%2" )
                    .arg( inputs.leakage->findings().size() )
                    .arg( inputs.leakage->auditedChecks().size() );
            category.evidence.insert( QStringLiteral( "finding_count" ),
                                      inputs.leakage->findings().size() );
            category.evidence.insert( QStringLiteral( "checks_run" ),
                                      QJsonArray::fromStringList( inputs.leakage->auditedChecks() ) );
        }
        report.categories().append( category );
    }

    // pseudo labels
    {
        DatasetQaCategory category;
        category.name = QStringLiteral( "pseudo_labels" );
        const qint64 pseudo = inputs.catalogSummary.pseudoLabelCount;
        category.evidence.insert( QStringLiteral( "pseudo_label_count" ), pseudo );
        category.evidence.insert( QStringLiteral( "total" ), inputs.catalogSummary.total );
        if ( inputs.catalogSummary.total == 0 )
        {
            category.verdict = AuditVerdict::Unknown;
            category.summary = QStringLiteral( "catalog summary empty" );
        }
        else if ( pseudo > 0 )
        {
            category.verdict = AuditVerdict::Warn;
            category.summary =
                QStringLiteral( "%1 pseudo/weak labels present — filter before protected test" )
                    .arg( pseudo );
        }
        else
        {
            category.verdict = AuditVerdict::Pass;
            category.summary = QStringLiteral( "no pseudo labels in catalog summary" );
        }
        report.categories().append( category );
    }

    // provenance
    {
        DatasetQaCategory category;
        category.name = QStringLiteral( "provenance" );
        if ( inputs.provenanceComplete )
        {
            category.verdict = AuditVerdict::Pass;
            category.summary = QStringLiteral( "caller reported provenance complete" );
        }
        else
        {
            category.verdict = AuditVerdict::Warn;
            category.summary = QStringLiteral( "provenance incomplete or not asserted" );
        }
        report.categories().append( category );
    }

    return report;
}

} // namespace sicnu::dataset
