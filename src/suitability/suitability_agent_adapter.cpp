#include "suitability_agent_adapter.h"

#include "../dataset/dataset_store.h"
#include "store_data_provider.h"
#include "suitability_assessor.h"
#include "suitability_level.h"
#include "suitability_profiles.h"
#include "suitability_report.h"
#include "suitability_teaching.h"
#include "scene_candidate.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <stdexcept>

namespace sicnu::suitability::agent_adapter
{

namespace
{

[[noreturn]] void fail( const QString &message )
{
    throw std::runtime_error( message.toStdString() );
}

QJsonArray diagnosticsArray( const QVector<sicnu::data::Diagnostic> &diagnostics )
{
    QJsonArray array;
    for ( const sicnu::data::Diagnostic &diagnostic : diagnostics )
    {
        QJsonObject item;
        item.insert( QStringLiteral( "code" ), diagnostic.code );
        item.insert( QStringLiteral( "message" ), diagnostic.message );
        item.insert( QStringLiteral( "severity" ),
                     diagnostic.severity == sicnu::data::DiagnosticSeverity::Error
                         ? QStringLiteral( "error" )
                         : ( diagnostic.severity == sicnu::data::DiagnosticSeverity::Warning
                                 ? QStringLiteral( "warning" )
                                 : QStringLiteral( "info" ) ) );
        array.append( item );
    }
    return array;
}

QJsonObject suitabilityProfileJson( const SuitabilityProfile &profile )
{
    // Machine-readable projection of the built-in profile table: every
    // default the profile sets becomes a field; unset optionals stay absent
    // (a profile default of "not set" is not a zero).
    QJsonObject json;
    json.insert( QStringLiteral( "key" ), profile.key );
    json.insert( QStringLiteral( "display_name" ), profile.displayName );
    json.insert( QStringLiteral( "task_family" ), profile.taskFamily );
    if ( profile.requireLabels.has_value() )
        json.insert( QStringLiteral( "require_labels" ), *profile.requireLabels );
    if ( profile.minSamples.has_value() )
        json.insert( QStringLiteral( "min_samples" ), static_cast<double>( *profile.minSamples ) );
    if ( profile.minCoverageFraction.has_value() )
        json.insert( QStringLiteral( "min_coverage_fraction" ), *profile.minCoverageFraction );
    if ( profile.minScenesInWindow.has_value() )
        json.insert( QStringLiteral( "min_scenes_in_window" ),
                     static_cast<double>( *profile.minScenesInWindow ) );
    if ( profile.minGsdM.has_value() )
        json.insert( QStringLiteral( "min_gsd_m" ), *profile.minGsdM );
    if ( profile.maxGsdM.has_value() )
        json.insert( QStringLiteral( "max_gsd_m" ), *profile.maxGsdM );
    if ( !profile.requiredSeasons.isEmpty() )
        json.insert( QStringLiteral( "required_seasons" ),
                     QJsonArray::fromStringList( profile.requiredSeasons ) );
    if ( profile.pseudoLabelsAllowed.has_value() )
        json.insert( QStringLiteral( "pseudo_labels_allowed" ), *profile.pseudoLabelsAllowed );
    if ( profile.gridStrict.has_value() )
        json.insert( QStringLiteral( "grid_strict" ), *profile.gridStrict );
    return json;
}

QVariantMap toVariant( const QJsonObject &json )
{
    return json.toVariantMap();
}

} // namespace

QVariantMap suitabilityProfiles( const QVariantMap &args )
{
    const QString requested = args.value( QStringLiteral( "profile" ) ).toString();
    QJsonArray rows;
    if ( !requested.isEmpty() )
    {
        const auto profile = builtinProfile( requested );
        if ( !profile.has_value() )
            fail( QStringLiteral( "unknown suitability profile '%1'" ).arg( requested ) );
        rows.append( suitabilityProfileJson( profile.value() ) );
    }
    else
    {
        for ( const QString &key : builtinProfileKeys() )
            rows.append( suitabilityProfileJson( builtinProfile( key ).value() ) );
    }
    QJsonObject data;
    data.insert( QStringLiteral( "profiles" ), rows );
    data.insert( QStringLiteral( "count" ), rows.size() );
    return toVariant( data );
}

QVariantMap suitabilityAssess( const QVariantMap &args )
{
    const QString goalText = args.value( QStringLiteral( "goal" ) ).toString();
    if ( goalText.isEmpty() )
        fail( QStringLiteral( "goal is required" ) );
    // Two distinct failure layers, mirroring dataset:validate's honesty:
    // a syntactically broken goal document is a TOOL error (the caller sent
    // garbage — throw); a well-formed document with invalid CONTENT is a
    // completed inspection reported as valid=false + diagnostics.
    const QJsonDocument goalDocument = QJsonDocument::fromJson( goalText.toUtf8() );
    if ( goalDocument.isNull() || !goalDocument.isObject() )
        fail( QStringLiteral( "goal is not a JSON object document" ) );
    const auto goal = SuitabilityGoal::fromJson( goalDocument.object() );
    if ( !goal.has_value() )
    {
        QJsonObject data;
        data.insert( QStringLiteral( "valid" ), false );
        data.insert( QStringLiteral( "diagnostics" ), diagnosticsArray( goal.diagnostics() ) );
        return toVariant( data );
    }

    SuitabilityAssessor::Inputs inputs;
    inputs.goal = goal.value();

    const QString versionId = args.value( QStringLiteral( "dataset_version_id" ) ).toString();
    const QString dbPath = args.value( QStringLiteral( "dataset_db" ) ).toString();
    // This channel is READ-ONLY: DatasetStore::open CREATES the file when
    // the path does not exist (the dataset: tools' precedent for writers),
    // so a typo'd path would silently assess an empty store and leave
    // droppings on disk. Refuse a missing file instead — an assessment of a
    // store that does not exist has no legitimate use.
    std::unique_ptr<sicnu::dataset::DatasetStore> store;
    std::unique_ptr<StoreDataProvider> provider;
    if ( !dbPath.isEmpty() )
    {
        if ( !QFileInfo::exists( dbPath ) )
            fail( QStringLiteral( "dataset_db does not exist at '%1' "
                                  "(assess never creates a store)" )
                      .arg( dbPath ) );
        store = std::make_unique<sicnu::dataset::DatasetStore>();
        QString error;
        if ( !store->open( dbPath, &error ) )
            fail( QStringLiteral( "cannot open dataset_db at '%1'%2" )
                      .arg( dbPath,
                            error.isEmpty() ? QString() : QStringLiteral( ": " ) + error ) );
        provider = std::make_unique<StoreDataProvider>( store.get() );
        inputs.provider = provider.get();
    }
    else if ( !versionId.isEmpty() )
    {
        // Naming a dataset without any way to read its facts would silently
        // assess a fraction of the named subject: refuse, never guess.
        fail( QStringLiteral(
            "dataset_version_id requires dataset_db (no facts provider available)" ) );
    }
    inputs.datasetVersionId = versionId;

    const QString sceneText = args.value( QStringLiteral( "scenes" ) ).toString();
    if ( !sceneText.isEmpty() )
    {
        const QJsonDocument sceneDocument = QJsonDocument::fromJson( sceneText.toUtf8() );
        if ( sceneDocument.isNull() || !sceneDocument.isArray() )
            fail( QStringLiteral( "scenes is not a JSON array document" ) );
        for ( const QJsonValue &value : sceneDocument.array() )
        {
            const auto scene = SceneCandidate::fromJson( value.toObject() );
            if ( !scene.has_value() )
                fail( QStringLiteral( "scenes carries an invalid scene candidate: %1" )
                          .arg( scene.diagnostics().isEmpty()
                                    ? QStringLiteral( "unknown error" )
                                    : scene.diagnostics().first().message ) );
            inputs.scenes.append( scene.value() );
        }
    }

    const auto result = SuitabilityAssessor::assess( inputs );
    if ( !result.has_value() )
    {
        // The assessment itself did not run (empty subject, unknown profile,
        // too many scenes): a completed call with valid=false, not a throw —
        // the caller asked "does this fit?" and the typed answer is on record.
        QJsonObject data;
        data.insert( QStringLiteral( "valid" ), false );
        data.insert( QStringLiteral( "diagnostics" ), diagnosticsArray( result.diagnostics() ) );
        return toVariant( data );
    }

    const SuitabilityReport report = result.value();
    // "Callable and verifiable": the versioned report JSON rides the result
    // verbatim (fromJson + contentDigest re-verification is the caller's
    // check), next to the derived overall level, gap list and teaching lines.
    QJsonObject data;
    data.insert( QStringLiteral( "valid" ), true );
    data.insert( QStringLiteral( "overall_level" ),
                 suitabilityLevelToString( report.overallLevel() ) );
    data.insert( QStringLiteral( "report" ), report.toJson() );
    data.insert( QStringLiteral( "report_digest" ), report.contentDigest() );
    QJsonArray gaps;
    for ( const SuitabilityGap &gap : report.allGaps() )
    {
        QJsonObject item;
        item.insert( QStringLiteral( "id" ), gap.id );
        item.insert( QStringLiteral( "criterion_id" ), gap.criterionId );
        item.insert( QStringLiteral( "description" ), gap.description );
        gaps.append( item );
    }
    data.insert( QStringLiteral( "gaps" ), gaps );
    data.insert( QStringLiteral( "teaching" ),
                 QJsonArray::fromStringList( teachingExplanation( report ) ) );
    return toVariant( data );
}

} // namespace sicnu::suitability::agent_adapter
