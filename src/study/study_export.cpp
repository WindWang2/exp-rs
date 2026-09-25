// study_export.cpp — report assembly, versioned JSON, atomic write.
#include "study_export.h"

#include "experiment/experiment_store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include <cmath>

namespace sicnu::study
{
namespace
{

Diagnostic exportError( const QString &code, const QString &message )
{
    Diagnostic d;
    d.code = code;
    d.message = message;
    d.severity = sicnu::data::DiagnosticSeverity::Error;
    return d;
}

QJsonObject curvePointToJson( const CurvePoint &point )
{
    QJsonObject json;
    json.insert( QStringLiteral( "dimension_value" ), point.dimensionValue );
    json.insert( QStringLiteral( "run_count" ), static_cast<double>( point.runCount ) );
    json.insert( QStringLiteral( "mean" ), point.mean );
    json.insert( QStringLiteral( "population_std_dev" ), point.populationStdDev );
    json.insert( QStringLiteral( "min" ), point.min );
    json.insert( QStringLiteral( "max" ), point.max );
    return json;
}

QJsonObject envelopeBandToJson( const UncertaintyBand &band )
{
    QJsonObject json;
    QJsonObject assignments;
    for ( auto it = band.parameterAssignments.constBegin();
          it != band.parameterAssignments.constEnd(); ++it )
        assignments.insert( it.key(), it.value() );
    json.insert( QStringLiteral( "parameter_assignments" ), assignments );
    json.insert( QStringLiteral( "run_count" ), static_cast<double>( band.runCount ) );
    json.insert( QStringLiteral( "mean" ), band.mean );
    json.insert( QStringLiteral( "population_std_dev" ), band.populationStdDev );
    json.insert( QStringLiteral( "min" ), band.min );
    json.insert( QStringLiteral( "max" ), band.max );
    return json;
}

QString formatValue( double value )
{
    return std::floor( value ) == value ? QString::number( value, 'f', 0 )
                                        : QString::number( value, 'g', 6 );
}

} // namespace

QJsonObject StudyRunRow::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "point_id" ), pointId );
    json.insert( QStringLiteral( "replicate_index" ), static_cast<double>( replicateIndex ) );
    json.insert( QStringLiteral( "run_id" ), runId );
    json.insert( QStringLiteral( "status" ), status );
    QJsonObject assignmentsJson;
    for ( auto it = parameterAssignments.constBegin();
          it != parameterAssignments.constEnd(); ++it )
        assignmentsJson.insert( it.key(), it.value() );
    json.insert( QStringLiteral( "parameter_assignments" ), assignmentsJson );
    json.insert( QStringLiteral( "seed" ), static_cast<double>( seed ) );
    // Exact seed: a JSON double silently corrupts quint64 widths above 2^53
    // (and flips sign above 2^63). The decimal string is authoritative; the
    // legacy "seed" member stays lossless-compatible for small seeds.
    json.insert( QStringLiteral( "seed_u64" ), QString::number( seed ) );
    json.insert( QStringLiteral( "metrics" ), metrics );
    json.insert( QStringLiteral( "error_summary" ), errorSummary );
    json.insert( QStringLiteral( "output_asset_path" ), outputAssetPath );
    return json;
}

Result<StudyRunRow> StudyRunRow::fromJson( const QJsonObject &json )
{
    StudyRunRow row;
    row.pointId = json.value( QStringLiteral( "point_id" ) ).toString();
    row.replicateIndex = json.value( QStringLiteral( "replicate_index" ) ).toInt();
    row.runId = json.value( QStringLiteral( "run_id" ) ).toString();
    row.status = json.value( QStringLiteral( "status" ) ).toString();
    const QJsonObject assignments = json.value( QStringLiteral( "parameter_assignments" ) ).toObject();
    for ( auto it = assignments.constBegin(); it != assignments.constEnd(); ++it )
        row.parameterAssignments.insert( it.key(), it.value().toString() );
    if ( json.contains( QStringLiteral( "seed_u64" ) ) )
    {
        bool seedOk = false;
        row.seed = json.value( QStringLiteral( "seed_u64" ) )
                       .toString()
                       .toULongLong( &seedOk );
        if ( !seedOk )
            return Result<StudyRunRow>::failure( exportError(
                QStringLiteral( "study.report_invalid_row" ),
                QStringLiteral( "report row seed_u64 must be an unsigned 64-bit"
                                " decimal string" ) ) );
    }
    else
    {
        // Legacy documents: the double form (lossless below 2^53).
        row.seed = static_cast<quint64>( json.value( QStringLiteral( "seed" ) ).toDouble() );
    }
    row.metrics = json.value( QStringLiteral( "metrics" ) ).toObject();
    row.errorSummary = json.value( QStringLiteral( "error_summary" ) ).toString();
    row.outputAssetPath = json.value( QStringLiteral( "output_asset_path" ) ).toString();
    if ( row.pointId.isEmpty() )
        return Result<StudyRunRow>::failure( exportError(
            QStringLiteral( "study.report_invalid_row" ),
            QStringLiteral( "report row requires a point id" ) ) );
    return Result<StudyRunRow>::success( row );
}

QJsonObject TrendTriple::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "parameter" ), parameterPath );
    json.insert( QStringLiteral( "metric" ), metricName );
    json.insert( QStringLiteral( "trend" ), trend );
    json.insert( QStringLiteral( "observation" ), observation );
    return json;
}

QJsonObject StudyReport::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "document_type" ), QStringLiteral( "sicnu.studyreport.v1" ) );
    json.insert( QStringLiteral( "schema_version" ), kStudyReportSchemaVersion );
    json.insert( QStringLiteral( "study_id" ), studyId );
    json.insert( QStringLiteral( "experiment_id" ), experimentId );
    json.insert( QStringLiteral( "algorithm_id" ), algorithmId );
    json.insert( QStringLiteral( "strategy" ), strategy );
    json.insert( QStringLiteral( "spec" ), specJson );
    json.insert( QStringLiteral( "stopped_reason" ), stoppedReason );
    json.insert( QStringLiteral( "elapsed_ms" ), static_cast<double>( elapsedMs ) );
    json.insert( QStringLiteral( "generated_at_utc" ),
                 generatedAtUtc.toString( Qt::ISODateWithMs ) );

    QJsonArray rows;
    for ( const StudyRunRow &row : runTable )
        rows.append( row.toJson() );
    json.insert( QStringLiteral( "run_table" ), rows );

    QJsonArray curvesJson;
    for ( const SensitivityCurve &curve : curves )
    {
        QJsonObject curveJson;
        curveJson.insert( QStringLiteral( "parameter" ), curve.parameterPath );
        curveJson.insert( QStringLiteral( "metric" ), curve.metricName );
        curveJson.insert( QStringLiteral( "trend" ), curve.trend );
        QJsonArray points;
        for ( const CurvePoint &point : curve.points )
            points.append( curvePointToJson( point ) );
        curveJson.insert( QStringLiteral( "points" ), points );
        curvesJson.append( curveJson );
    }
    json.insert( QStringLiteral( "curves" ), curvesJson );

    QJsonArray envelopesJson;
    for ( const UncertaintyEnvelope &envelope : envelopes )
    {
        QJsonObject envelopeJson;
        envelopeJson.insert( QStringLiteral( "metric" ), envelope.metricName );
        QJsonArray bands;
        for ( const UncertaintyBand &band : envelope.bands )
            bands.append( envelopeBandToJson( band ) );
        envelopeJson.insert( QStringLiteral( "bands" ), bands );
        envelopesJson.append( envelopeJson );
    }
    json.insert( QStringLiteral( "uncertainty_envelopes" ), envelopesJson );

    QJsonArray pareto;
    for ( const QString &pointId : paretoPointIds )
        pareto.append( pointId );
    json.insert( QStringLiteral( "pareto_point_ids" ), pareto );
    json.insert( QStringLiteral( "declared_best" ), declaredBest );

    QJsonArray spatial;
    for ( const SpatialDifferenceSummary &summary : spatialSummaries )
        spatial.append( summary.toJson() );
    json.insert( QStringLiteral( "spatial_summaries" ), spatial );

    QJsonArray narrativeJson;
    for ( const TrendTriple &triple : narrative )
        narrativeJson.append( triple.toJson() );
    json.insert( QStringLiteral( "narrative" ), narrativeJson );

    QJsonObject accounting;
    accounting.insert( QStringLiteral( "recorded" ), recordedCount );
    accounting.insert( QStringLiteral( "failed" ), failedCount );
    accounting.insert( QStringLiteral( "cancelled" ), cancelledCount );
    accounting.insert( QStringLiteral( "missing" ), missingCount );
    accounting.insert( QStringLiteral( "total_points" ), runTable.size() );
    json.insert( QStringLiteral( "status_accounting" ), accounting );

    // How an agent should read this document — machine-readable honesty.
    json.insert(
        QStringLiteral( "usage_notes" ),
        QStringLiteral( "Observational evidence only. 'declared_best' exists ONLY when the "
                         "study spec declared an objective metric with direction; 'pareto_point_ids' "
                         "lists non-dominated points under the declared directions. Nothing in this "
                         "document is a recommendation; failed and cancelled runs are part of the "
                         "evidence and were never retried or dropped." ) );
    return json;
}

Result<StudyReport> StudyReport::fromJson( const QJsonObject &json )
{
    if ( json.value( QStringLiteral( "document_type" ) ).toString()
             != QStringLiteral( "sicnu.studyreport.v1" )
         || json.value( QStringLiteral( "schema_version" ) ).toInt()
             != kStudyReportSchemaVersion )
        return Result<StudyReport>::failure( exportError(
            QStringLiteral( "study.report_unsupported_version" ),
            QStringLiteral( "report requires document_type sicnu.studyreport.v1 with "
                             "schema_version %1" )
                .arg( kStudyReportSchemaVersion ) ) );

    StudyReport report;
    report.studyId = json.value( QStringLiteral( "study_id" ) ).toString();
    report.experimentId = json.value( QStringLiteral( "experiment_id" ) ).toString();
    report.algorithmId = json.value( QStringLiteral( "algorithm_id" ) ).toString();
    report.strategy = json.value( QStringLiteral( "strategy" ) ).toString();
    report.specJson = json.value( QStringLiteral( "spec" ) ).toObject();
    report.stoppedReason = json.value( QStringLiteral( "stopped_reason" ) ).toString();
    report.elapsedMs = static_cast<qint64>(
        json.value( QStringLiteral( "elapsed_ms" ) ).toDouble() );
    report.generatedAtUtc =
        QDateTime::fromString( json.value( QStringLiteral( "generated_at_utc" ) ).toString(),
                               Qt::ISODateWithMs );

    const QJsonArray rows = json.value( QStringLiteral( "run_table" ) ).toArray();
    for ( const auto &entry : rows )
    {
        const auto row = StudyRunRow::fromJson( entry.toObject() );
        if ( !row )
            return Result<StudyReport>::failure( row.diagnostics() );
        report.runTable.append( row.value() );
    }

    const QJsonArray curves = json.value( QStringLiteral( "curves" ) ).toArray();
    for ( const auto &entry : curves )
    {
        const QJsonObject curveJson = entry.toObject();
        SensitivityCurve curve;
        curve.parameterPath = curveJson.value( QStringLiteral( "parameter" ) ).toString();
        curve.metricName = curveJson.value( QStringLiteral( "metric" ) ).toString();
        curve.trend = curveJson.value( QStringLiteral( "trend" ) ).toString();
        const QJsonArray points = curveJson.value( QStringLiteral( "points" ) ).toArray();
        for ( const auto &pointEntry : points )
        {
            const QJsonObject p = pointEntry.toObject();
            CurvePoint point;
            point.dimensionValue = p.value( QStringLiteral( "dimension_value" ) ).toDouble();
            point.runCount = static_cast<qint64>(
                p.value( QStringLiteral( "run_count" ) ).toDouble() );
            point.mean = p.value( QStringLiteral( "mean" ) ).toDouble();
            point.populationStdDev =
                p.value( QStringLiteral( "population_std_dev" ) ).toDouble();
            point.min = p.value( QStringLiteral( "min" ) ).toDouble();
            point.max = p.value( QStringLiteral( "max" ) ).toDouble();
            curve.points.append( point );
        }
        report.curves.append( curve );
    }

    const QJsonArray envelopes = json.value( QStringLiteral( "uncertainty_envelopes" ) ).toArray();
    for ( const auto &entry : envelopes )
    {
        const QJsonObject envelopeJson = entry.toObject();
        UncertaintyEnvelope envelope;
        envelope.metricName = envelopeJson.value( QStringLiteral( "metric" ) ).toString();
        const QJsonArray bands = envelopeJson.value( QStringLiteral( "bands" ) ).toArray();
        for ( const auto &bandEntry : bands )
        {
            const QJsonObject bandJson = bandEntry.toObject();
            UncertaintyBand band;
            const QJsonObject assignments =
                bandJson.value( QStringLiteral( "parameter_assignments" ) ).toObject();
            for ( auto it = assignments.constBegin(); it != assignments.constEnd(); ++it )
                band.parameterAssignments.insert( it.key(), it.value().toString() );
            band.runCount = static_cast<qint64>(
                bandJson.value( QStringLiteral( "run_count" ) ).toDouble() );
            band.mean = bandJson.value( QStringLiteral( "mean" ) ).toDouble();
            band.populationStdDev =
                bandJson.value( QStringLiteral( "population_std_dev" ) ).toDouble();
            band.min = bandJson.value( QStringLiteral( "min" ) ).toDouble();
            band.max = bandJson.value( QStringLiteral( "max" ) ).toDouble();
            envelope.bands.append( band );
        }
        report.envelopes.append( envelope );
    }

    const QJsonArray pareto = json.value( QStringLiteral( "pareto_point_ids" ) ).toArray();
    for ( const auto &entry : pareto )
        report.paretoPointIds.append( entry.toString() );
    report.declaredBest = json.value( QStringLiteral( "declared_best" ) ).toObject();

    const QJsonArray spatial = json.value( QStringLiteral( "spatial_summaries" ) ).toArray();
    for ( const auto &entry : spatial )
    {
        const auto summary = SpatialDifferenceSummary::fromJson( entry.toObject() );
        if ( !summary )
            return Result<StudyReport>::failure( summary.diagnostics() );
        report.spatialSummaries.append( summary.value() );
    }

    const QJsonArray narrative = json.value( QStringLiteral( "narrative" ) ).toArray();
    for ( const auto &entry : narrative )
    {
        const QJsonObject tripleJson = entry.toObject();
        TrendTriple triple;
        triple.parameterPath = tripleJson.value( QStringLiteral( "parameter" ) ).toString();
        triple.metricName = tripleJson.value( QStringLiteral( "metric" ) ).toString();
        triple.trend = tripleJson.value( QStringLiteral( "trend" ) ).toString();
        triple.observation = tripleJson.value( QStringLiteral( "observation" ) ).toString();
        report.narrative.append( triple );
    }

    const QJsonObject accounting = json.value( QStringLiteral( "status_accounting" ) ).toObject();
    report.recordedCount = accounting.value( QStringLiteral( "recorded" ) ).toInt();
    report.failedCount = accounting.value( QStringLiteral( "failed" ) ).toInt();
    report.cancelledCount = accounting.value( QStringLiteral( "cancelled" ) ).toInt();
    report.missingCount = accounting.value( QStringLiteral( "missing" ) ).toInt();

    return Result<StudyReport>::success( report );
}

StudyReport buildStudyReport( experiment::ExperimentStore &store,
                              experiment::MatrixLedger &ledger, const ParameterStudySpec &spec,
                              const QVector<StudyPoint> &points,
                              const QVector<SpatialDifferenceSummary> &spatialSummaries,
                              const StudyRunSummary *runnerSummary,
                              QDateTime generatedAtUtc )
{
    const StudyAnalysis analysis = analyzeStudy( store, ledger, spec, points );

    StudyReport report;
    report.studyId = spec.studyId;
    report.experimentId = spec.experimentId;
    report.algorithmId = spec.algorithmId;
    report.strategy = samplingStrategyToString( spec.strategy );
    report.specJson = spec.toJson();
    report.generatedAtUtc = generatedAtUtc;
    report.curves = analysis.curves;
    report.envelopes = analysis.envelopes;
    report.paretoPointIds = analysis.paretoPointIds;
    report.spatialSummaries = spatialSummaries;

    // Error evidence per point: failed/cancelled runs self-describe through
    // the recorder's metrics["error"] / cancel_reason.
    QHash<QString, QString> errorEvidence; // runId → human-readable summary
    for ( const PointAggregate &aggregate : analysis.points )
    {
        for ( const QString &runId : aggregate.runIds )
        {
            const auto run = store.runById( runId );
            if ( !run )
                continue;
            QString summary;
            const QJsonObject metrics = run.value().metrics();
            const QJsonObject error = metrics.value( QStringLiteral( "error" ) ).toObject();
            if ( !error.isEmpty() )
                summary = QStringLiteral( "%1: %2" )
                              .arg( error.value( QStringLiteral( "error_code" ) ).toString(),
                                    error.value( QStringLiteral( "message" ) ).toString() );
            const QString cancel =
                metrics.value( QStringLiteral( "cancel_reason" ) ).toString();
            if ( summary.isEmpty() && !cancel.isEmpty() )
                summary = QStringLiteral( "cancelled: %1" ).arg( cancel );
            errorEvidence.insert( runId, summary );
        }
    }

    // pointId → sampled point (replicate index, output path). Indexed once:
    // the per-aggregate std::find_if this replaces rescanned the FULL point
    // list for every run-table row — quadratic in the sweep-cap bound, with
    // the whole cost paid inside the Studio's live worker thread.
    QHash<QString, const StudyPoint *> pointsById;
    pointsById.reserve( points.size() );
    for ( const StudyPoint &point : points )
    {
        if ( !pointsById.contains( point.pointId ) ) // first match wins, as find_if did
            pointsById.insert( point.pointId, &point );
    }

    // Run table: one row per sampled point, sample order, no drops.
    for ( const PointAggregate &aggregate : analysis.points )
    {
        StudyRunRow row;
        row.pointId = aggregate.pointId;
        row.status = aggregate.status;
        row.parameterAssignments = aggregate.assignments;
        row.parameterAssignments.remove( QStringLiteral( "seed" ) );
        if ( !aggregate.runIds.isEmpty() )
            row.runId = aggregate.runIds.first();
        const auto pointIt = pointsById.constFind( aggregate.pointId );
        const StudyPoint *point =
            pointIt != pointsById.constEnd() ? pointIt.value() : nullptr;
        if ( point )
        {
            row.replicateIndex = point->replicateIndex;
            row.seed = point->seed;
        }
        row.metrics = [ & ] {
            QJsonObject metrics;
            for ( auto it = aggregate.metrics.constBegin();
                  it != aggregate.metrics.constEnd(); ++it )
            {
                if ( it->runCount == 0 )
                    continue; // never impute an unreported metric
                QJsonObject stat;
                stat.insert( QStringLiteral( "mean" ), it->mean );
                stat.insert( QStringLiteral( "population_std_dev" ),
                             it->populationStdDev );
                stat.insert( QStringLiteral( "min" ), it->min );
                stat.insert( QStringLiteral( "max" ), it->max );
                stat.insert( QStringLiteral( "run_count" ),
                             static_cast<double>( it->runCount ) );
                metrics.insert( it.key(), stat );
            }
            return metrics;
        }();
        if ( !row.runId.isEmpty() )
            row.errorSummary = errorEvidence.value( row.runId );
        // Output path from the run's recorded parameter ("output") — the
        // runner-assigned stable location.
        if ( point )
        {
            row.outputAssetPath = point->parameters.value( QStringLiteral( "output" ) )
                                      .toString();
        }
        report.runTable.append( row );
    }

    // Status accounting from the rows themselves (self-consistent evidence).
    for ( const StudyRunRow &row : report.runTable )
    {
        if ( row.status == QStringLiteral( "recorded" )
             || row.status == QStringLiteral( "partial" ) )
            ++report.recordedCount;
        else if ( row.status == QStringLiteral( "failed" ) )
            ++report.failedCount;
        else if ( row.status == QStringLiteral( "cancelled" ) )
            ++report.cancelledCount;
        else
            ++report.missingCount; // missing / in_progress with no evidence
    }

    // Declared best (already gated by the spec contract in analysis).
    if ( !analysis.declaredBestPointId.isEmpty() )
    {
        QJsonObject best;
        best.insert( QStringLiteral( "point_id" ), analysis.declaredBestPointId );
        best.insert( QStringLiteral( "value" ), analysis.declaredBestValue );
        best.insert( QStringLiteral( "basis" ), analysis.declaredBestBasis );
        report.declaredBest = best;
    }

    // Mechanical teaching triples from the curves.
    for ( const SensitivityCurve &curve : analysis.curves )
    {
        if ( curve.points.size() < 2 )
            continue;
        TrendTriple triple;
        triple.parameterPath = curve.parameterPath;
        triple.metricName = curve.metricName;
        triple.trend = curve.trend;
        const CurvePoint &first = curve.points.first();
        const CurvePoint &last = curve.points.last();
        if ( curve.trend == QStringLiteral( "increasing" )
             || curve.trend == QStringLiteral( "decreasing" ) )
            triple.observation =
                QStringLiteral( "%1 %2s from %3 (at %4 = %5) to %6 (at %4 = %7) on the "
                                "reference slice" )
                    .arg( curve.metricName,
                          curve.trend == QStringLiteral( "increasing" )
                              ? QStringLiteral( "rise" )
                              : QStringLiteral( "fall" ),
                          formatValue( first.mean ), curve.parameterPath,
                          formatValue( first.dimensionValue ), formatValue( last.mean ),
                          formatValue( last.dimensionValue ) );
        else
            triple.observation =
                QStringLiteral( "%1 varies non-monotonically between %2 and %3 across %4 = "
                                "%5 … %6 on the reference slice" )
                    .arg( curve.metricName,
                          formatValue( std::min( first.mean, last.mean ) ),
                          formatValue( std::max( first.mean, last.mean ) ),
                          curve.parameterPath, formatValue( first.dimensionValue ),
                          formatValue( last.dimensionValue ) );
        report.narrative.append( triple );
    }

    if ( runnerSummary )
    {
        report.stoppedReason = runnerSummary->stoppedReason;
        report.elapsedMs = runnerSummary->elapsedMs;
    }
    return report;
}

Result<void> writeStudyReport( const StudyReport &report, const QString &path )
{
    const QJsonDocument document( report.toJson() );
    QSaveFile file( path );
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Text ) )
        return Result<void>::failure( exportError(
            QStringLiteral( "study.report_write_failed" ),
            QStringLiteral( "cannot open %1 for writing: %2" )
                .arg( path, file.errorString() ) ) );
    file.write( document.toJson( QJsonDocument::Indented ) );
    if ( !file.commit() )
        return Result<void>::failure( exportError(
            QStringLiteral( "study.report_write_failed" ),
            QStringLiteral( "cannot commit %1: %2" ).arg( path, file.errorString() ) ) );
    return Result<void>::success();
}

Result<QVector<SpatialDifferenceSummary>> summarizeStudyOutputs(
    experiment::ExperimentStore &store, experiment::MatrixLedger &ledger,
    const ParameterStudySpec &spec, const QVector<StudyPoint> &points,
    const QString &studyOutputDir, const ISpatialDifferenceSummarizer &summarizer )
{
    // The spec field is the DECLARATION; composition is this explicit call so
    // the report builder stays a pure projection of what it is handed.
    if ( !spec.spatialComparison )
        return Result<QVector<SpatialDifferenceSummary>>::success( {} );

    const QDir outputDir( studyOutputDir );
    const auto outputOf = [&outputDir]( const StudyPoint &point ) {
        return outputDir.filePath( point.pointId + QStringLiteral( "/output.tif" ) );
    };
    const auto hasRecordedOutput = [&]( const StudyPoint &point ) {
        const QStringList runs = ledger.runsForCell( point.pointId );
        for ( const QString &runId : runs )
        {
            const auto run = store.runById( runId );
            if ( run && run.value().status() == dataset::RunStatus::Completed
                 && QFile::exists( outputOf( point ) ) )
                return true;
        }
        return false;
    };

    // Baseline: the reference point (every dimension at its reference ladder
    // value — the same median rule as the OAT sampler); for LHS, the first
    // recorded output in sample order.
    QString baselineOutput;
    if ( spec.strategy != SamplingStrategy::LatinHypercube )
    {
        for ( const StudyPoint &point : points )
        {
            bool atReference = true;
            for ( const ParameterDimension &dimension : spec.dimensions )
            {
                const auto ladder = dimensionLadder( dimension );
                const QString referenceText =
                    canonicalValueText( ladder.at( ( dimension.stepCount - 1 ) / 2 ) );
                if ( point.assignments.value( dimension.parameterPath ) != referenceText )
                {
                    atReference = false;
                    break;
                }
            }
            if ( atReference && hasRecordedOutput( point ) )
            {
                baselineOutput = outputOf( point );
                break;
            }
        }
    }
    if ( baselineOutput.isEmpty() )
    {
        for ( const StudyPoint &point : points )
        {
            if ( hasRecordedOutput( point ) )
            {
                baselineOutput = outputOf( point );
                break;
            }
        }
    }
    if ( baselineOutput.isEmpty() )
        return Result<QVector<SpatialDifferenceSummary>>::failure( exportError(
            QStringLiteral( "study.spatial_no_baseline" ),
            QStringLiteral( "spatial comparison declared but no recorded output exists "
                             "to serve as the baseline" ) ) );

    QVector<SpatialDifferenceSummary> summaries;
    for ( const StudyPoint &point : points )
    {
        const QString output = outputOf( point );
        if ( QFileInfo( output ).absoluteFilePath()
             == QFileInfo( baselineOutput ).absoluteFilePath() )
            continue;
        if ( !hasRecordedOutput( point ) )
            continue; // no evidence to compare — never fabricated
        const auto summary = summarizer.summarize( baselineOutput, output );
        if ( !summary )
            return Result<QVector<SpatialDifferenceSummary>>::failure( summary.diagnostics() );
        summaries.append( summary.value() );
    }
    return Result<QVector<SpatialDifferenceSummary>>::success( summaries );
}

} // namespace sicnu::study
