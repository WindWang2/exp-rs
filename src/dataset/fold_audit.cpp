// fold_audit.cpp — see fold_audit.h for the contract.
#include "fold_audit.h"
#include <QJsonArray>

#include <QSet>

namespace sicnu::dataset
{

namespace
{

QJsonObject hashToJson( const QHash<QString, qint64> &counts )
{
    QJsonObject json;
    for ( auto it = counts.constBegin(); it != counts.constEnd(); ++it )
        json.insert( it.key(), it.value() );
    return json;
}

/// Fingerprint of a manifest with presentation-only fields cleared so a
/// regenerated manifest compares on CONTENT (assignments/config).
QString contentFingerprint( const SplitManifest &manifest )
{
    SplitManifest copy = manifest;
    copy.setFingerprint( QString() );
    copy.setManifestId( QString() );
    copy.setCreatedAtUtc( QDateTime() );
    copy.setNote( QString() );
    copy.setLeakageSummary( QJsonObject() );
    return splitManifestFingerprint( copy );
}

} // namespace

QJsonObject FoldAuditItem::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "fold" ), fold );
    json.insert( QStringLiteral( "train_count" ), trainCount );
    json.insert( QStringLiteral( "test_count" ), testCount );
    json.insert( QStringLiteral( "train_by_class" ), hashToJson( trainByClass ) );
    json.insert( QStringLiteral( "test_by_class" ), hashToJson( testByClass ) );
    json.insert( QStringLiteral( "classes_missing_in_test" ),
                 QJsonArray::fromStringList( classesMissingInTest ) );
    json.insert( QStringLiteral( "classes_missing_in_train" ),
                 QJsonArray::fromStringList( classesMissingInTrain ) );
    json.insert( QStringLiteral( "leakage_report" ), report.toJson() );
    return json;
}

QJsonObject FoldComparabilitySummary::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "fold_count" ), foldCount );
    json.insert( QStringLiteral( "replay_matches" ), replayMatches );
    json.insert( QStringLiteral( "replay_fingerprint" ), replayFingerprint );
    json.insert( QStringLiteral( "notes" ), QJsonArray::fromStringList( notes ) );
    QJsonArray foldArray;
    for ( const auto &fold : folds )
        foldArray.append( fold.toJson() );
    json.insert( QStringLiteral( "folds" ), foldArray );
    return json;
}

sicnu::data::Result<FoldComparabilitySummary> FoldAuditor::auditFolds(
    const SplitManifest &manifest, const QVector<SplitInput> &inputs,
    const LeakageAuditConfig &config, const QHash<QString, QString> &contentDigests )
{
    using Result = sicnu::data::Result<FoldComparabilitySummary>;
    if ( !SplitEngine::methodUsesFolds( manifest.config().method ) )
        return Result::failure( Diagnostic{
            QStringLiteral( "dataset.split_not_fold_based" ),
            QStringLiteral( "manifest %1 uses method %2 which does not persist folds" )
                .arg( manifest.manifestId(),
                      splitMethodToString( manifest.config().method ) ),
            DiagnosticSeverity::Error } );

    // Fast lookup of the engine-facing view by sample id.
    QHash<QString, SplitInput> inputById;
    QSet<QString> allClasses;
    for ( const auto &input : inputs )
    {
        inputById.insert( input.sampleId, input );
        if ( !input.classCode.isEmpty() )
            allClasses.insert( input.classCode );
    }
    if ( allClasses.isEmpty() )
        ; // no class evidence — zero-ratio checks degrade to notes below

    FoldComparabilitySummary summary;
    summary.foldCount = manifest.config().foldCount;

    const int foldTotal = int( manifest.config().foldCount );
    for ( int fold = 0; fold < foldTotal; ++fold )
    {
        const auto materialized = manifest.materializeFold( fold );
        if ( !materialized )
        {
            summary.notes.append(
                QStringLiteral( "fold %1 could not be materialized" ).arg( fold ) );
            continue;
        }

        // Audit view: this fold's Test pool vs everything else as Train.
        QVector<AuditSample> samples;
        samples.reserve( materialized->size() );
        FoldAuditItem item;
        item.fold = fold;
        for ( const auto &assignment : materialized.value() )
        {
            AuditSample sample;
            const auto inputIt = inputById.constFind( assignment.sampleId );
            if ( inputIt != inputById.constEnd() )
                sample.input = inputIt.value();
            else
                sample.input.sampleId = assignment.sampleId;
            sample.role = assignment.role;
            sample.contentDigest = contentDigests.value( assignment.sampleId );
            samples.append( sample );
            if ( assignment.role == SplitRole::Test )
                ++item.testCount;
            else
                ++item.trainCount;
            const QString classCode = sample.input.classCode;
            if ( classCode.isEmpty() )
                continue;
            if ( assignment.role == SplitRole::Test )
                ++item.testByClass[classCode];
            else
                ++item.trainByClass[classCode];
        }

        const auto report = LeakageAuditor::audit( manifest.datasetVersionId(),
                                                   manifest.manifestId(), samples, config );
        if ( !report )
            return Result::failure( report.diagnostics() );
        item.report = report.value();

        // Zero-ratio flags over classes seen anywhere in the input.
        for ( const QString &classCode : allClasses )
        {
            if ( !item.testByClass.contains( classCode ) )
                item.classesMissingInTest.append( classCode );
            if ( !item.trainByClass.contains( classCode ) )
                item.classesMissingInTrain.append( classCode );
        }
        item.classesMissingInTest.sort();
        item.classesMissingInTrain.sort();
        summary.folds.append( item );
    }

    if ( allClasses.isEmpty() )
        summary.notes.append( QStringLiteral(
            "inputs carry no class codes; zero-ratio checks were not run" ) );

    const auto replay = verifyDeterministicReplay( manifest, inputs );
    if ( replay )
    {
        summary.replayMatches = replay.value();
        summary.replayFingerprint = contentFingerprint( manifest );
    }
    else
    {
        summary.notes.append( QStringLiteral( "deterministic replay could not be verified: %1" )
                                  .arg( replay.diagnostics().isEmpty()
                                            ? QStringLiteral( "unknown error" )
                                            : replay.diagnostics().first().message ) );
    }
    return sicnu::data::Result<FoldComparabilitySummary>::success( summary );
}

sicnu::data::Result<bool> FoldAuditor::verifyDeterministicReplay(
    const SplitManifest &manifest, const QVector<SplitInput> &inputs )
{
    using Result = sicnu::data::Result<bool>;
    if ( !SplitEngine::methodUsesFolds( manifest.config().method ) )
        return Result::failure( Diagnostic{
            QStringLiteral( "dataset.split_not_fold_based" ),
            QStringLiteral( "manifest %1 is not fold based" ).arg( manifest.manifestId() ),
            DiagnosticSeverity::Error } );
    const auto regenerated =
        SplitEngine::generate( manifest.config(), manifest.datasetVersionId(), inputs );
    if ( !regenerated )
        return Result::failure( regenerated.diagnostics() );
    const bool matches =
        contentFingerprint( regenerated.value() ) == contentFingerprint( manifest );
    return Result::success( matches );
}

} // namespace sicnu::dataset
