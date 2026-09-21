#include "suitability_goal.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>

namespace sicnu::suitability
{

namespace
{

sicnu::data::Diagnostic invalidGoal( const QString &message )
{
    return sicnu::data::Diagnostic{ QStringLiteral( "suitability.goal_invalid" ),
                                    message,
                                    sicnu::data::DiagnosticSeverity::Error };
}

/// The single validation gate for goal values. Both fromJson and
/// resolveRequirements run it, so a parsed goal and an in-memory goal obey
/// the same contract.
sicnu::data::Result<void> validateGoal( const SuitabilityGoal &goal )
{
    if ( !goal.profileKey.isEmpty() )
    {
        // The named-profile table is a later slice; refuse instead of
        // pretending the default profile matches the requested one.
        return sicnu::data::Result<void>::failure( sicnu::data::Diagnostic{
            QStringLiteral( "suitability.profile_unknown" ),
            QStringLiteral( "unknown suitability profile '%1'" ).arg( goal.profileKey ),
            sicnu::data::DiagnosticSeverity::Error } );
    }
    if ( goal.hasTimeWindow )
    {
        if ( !goal.windowStartUtc.isValid() || !goal.windowEndUtc.isValid() )
            return sicnu::data::Result<void>::failure(
                invalidGoal( QStringLiteral( "time window carries invalid datetimes" ) ) );
        if ( !( goal.windowStartUtc < goal.windowEndUtc ) )
            return sicnu::data::Result<void>::failure(
                invalidGoal( QStringLiteral( "time window start must be before end" ) ) );
    }
    if ( goal.minGsdM > 0.0 && goal.maxGsdM > 0.0 && goal.minGsdM > goal.maxGsdM )
        return sicnu::data::Result<void>::failure(
            invalidGoal( QStringLiteral( "minGsdM must not exceed maxGsdM" ) ) );
    if ( goal.maxCloudCoverPercent < -1.0 || goal.maxCloudCoverPercent > 100.0 )
        return sicnu::data::Result<void>::failure(
            invalidGoal( QStringLiteral( "maxCloudCoverPercent must lie in [-1, 100]" ) ) );
    if ( goal.minSamples < 0 )
        return sicnu::data::Result<void>::failure(
            invalidGoal( QStringLiteral( "minSamples must not be negative" ) ) );
    // 0 means "use the built-in default", any other value must be a fraction.
    if ( goal.minCoverageFraction < 0.0 || goal.minCoverageFraction > 1.0 )
        return sicnu::data::Result<void>::failure(
            invalidGoal( QStringLiteral( "minCoverageFraction must lie in (0, 1] (or 0 for the default)" ) ) );
    if ( goal.minScenesInWindow < 0 )
        return sicnu::data::Result<void>::failure(
            invalidGoal( QStringLiteral( "minScenesInWindow must not be negative" ) ) );
    return sicnu::data::Result<void>::success();
}

} // namespace

QJsonObject SuitabilityGoal::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "schema_version" ), kSuitabilityGoalSerializationVersion );
    json.insert( QStringLiteral( "task_family" ),
                 sicnu::dataset::benchmarkTaskFamilyToString( taskFamily ) );
    json.insert( QStringLiteral( "profile_key" ), profileKey );

    json.insert( QStringLiteral( "has_aoi" ), hasAoi );
    if ( hasAoi )
    {
        QJsonObject aoiJson;
        aoiJson.insert( QStringLiteral( "min_x" ), aoi.minimumX );
        aoiJson.insert( QStringLiteral( "min_y" ), aoi.minimumY );
        aoiJson.insert( QStringLiteral( "max_x" ), aoi.maximumX );
        aoiJson.insert( QStringLiteral( "max_y" ), aoi.maximumY );
        aoiJson.insert( QStringLiteral( "valid" ), aoi.valid );
        json.insert( QStringLiteral( "aoi" ), aoiJson );
        json.insert( QStringLiteral( "aoi_crs_wkt" ), aoiCrsWkt );
    }

    json.insert( QStringLiteral( "has_time_window" ), hasTimeWindow );
    if ( hasTimeWindow )
    {
        json.insert( QStringLiteral( "window_start_utc" ), windowStartUtc.toString( Qt::ISODate ) );
        json.insert( QStringLiteral( "window_end_utc" ), windowEndUtc.toString( Qt::ISODate ) );
    }

    QJsonArray seasonsArray;
    for ( const QString &season : requiredSeasons )
        seasonsArray.append( season );
    json.insert( QStringLiteral( "required_seasons" ), seasonsArray );

    json.insert( QStringLiteral( "min_gsd_m" ), minGsdM );
    json.insert( QStringLiteral( "max_gsd_m" ), maxGsdM );

    QJsonArray bandRolesArray;
    for ( const QString &role : requiredBandRoles )
        bandRolesArray.append( role );
    json.insert( QStringLiteral( "required_band_roles" ), bandRolesArray );

    json.insert( QStringLiteral( "max_cloud_cover_percent" ), maxCloudCoverPercent );
    json.insert( QStringLiteral( "min_samples" ), static_cast< qint64 >( minSamples ) );

    QJsonArray classesArray;
    for ( const QString &className : requiredClasses )
        classesArray.append( className );
    json.insert( QStringLiteral( "required_classes" ), classesArray );
    json.insert( QStringLiteral( "require_labels" ), requireLabels );

    json.insert( QStringLiteral( "has_model" ), hasModel );
    if ( hasModel )
    {
        QJsonArray modelRolesArray;
        for ( const QString &role : modelRequiredBandRoles )
            modelRolesArray.append( role );
        json.insert( QStringLiteral( "model_required_band_roles" ), modelRolesArray );
        json.insert( QStringLiteral( "model_min_gsd_m" ), modelMinGsdM );
        json.insert( QStringLiteral( "model_max_gsd_m" ), modelMaxGsdM );
        json.insert( QStringLiteral( "model_modality" ), modelModality );
    }

    json.insert( QStringLiteral( "min_coverage_fraction" ), minCoverageFraction );
    json.insert( QStringLiteral( "min_scenes_in_window" ), static_cast< qint64 >( minScenesInWindow ) );
    return json;
}

sicnu::data::Result<SuitabilityGoal> SuitabilityGoal::fromJson( const QJsonObject &json )
{
    const int version = json.value( QStringLiteral( "schema_version" ) ).toInt( -1 );
    if ( version != kSuitabilityGoalSerializationVersion )
    {
        return sicnu::data::Result<SuitabilityGoal>::failure( sicnu::data::Diagnostic{
            QStringLiteral( "suitability.goal_schema" ),
            QStringLiteral( "unsupported goal schema_version %1" ).arg( version ),
            sicnu::data::DiagnosticSeverity::Error } );
    }

    SuitabilityGoal goal;

    const QString taskFamilyText = json.value( QStringLiteral( "task_family" ) ).toString();
    if ( !taskFamilyText.isEmpty() )
    {
        const auto taskFamily = sicnu::dataset::benchmarkTaskFamilyFromString( taskFamilyText );
        if ( !taskFamily.has_value() )
        {
            return sicnu::data::Result<SuitabilityGoal>::failure( invalidGoal(
                QStringLiteral( "unknown task_family '%1'" ).arg( taskFamilyText ) ) );
        }
        goal.taskFamily = *taskFamily;
    }

    goal.profileKey = json.value( QStringLiteral( "profile_key" ) ).toString();

    goal.hasAoi = json.value( QStringLiteral( "has_aoi" ) ).toBool( false );
    if ( goal.hasAoi )
    {
        const QJsonObject aoiJson = json.value( QStringLiteral( "aoi" ) ).toObject();
        goal.aoi.minimumX = aoiJson.value( QStringLiteral( "min_x" ) ).toDouble();
        goal.aoi.minimumY = aoiJson.value( QStringLiteral( "min_y" ) ).toDouble();
        goal.aoi.maximumX = aoiJson.value( QStringLiteral( "max_x" ) ).toDouble();
        goal.aoi.maximumY = aoiJson.value( QStringLiteral( "max_y" ) ).toDouble();
        goal.aoi.valid = aoiJson.value( QStringLiteral( "valid" ) ).toBool( false );
        goal.aoiCrsWkt = json.value( QStringLiteral( "aoi_crs_wkt" ) ).toString();
    }

    goal.hasTimeWindow = json.value( QStringLiteral( "has_time_window" ) ).toBool( false );
    if ( goal.hasTimeWindow )
    {
        goal.windowStartUtc = QDateTime::fromString(
            json.value( QStringLiteral( "window_start_utc" ) ).toString(), Qt::ISODate );
        goal.windowEndUtc = QDateTime::fromString(
            json.value( QStringLiteral( "window_end_utc" ) ).toString(), Qt::ISODate );
    }

    const QJsonArray seasonsArray = json.value( QStringLiteral( "required_seasons" ) ).toArray();
    for ( const QJsonValue &season : seasonsArray )
        goal.requiredSeasons.append( season.toString() );

    goal.minGsdM = json.value( QStringLiteral( "min_gsd_m" ) ).toDouble();
    goal.maxGsdM = json.value( QStringLiteral( "max_gsd_m" ) ).toDouble();

    const QJsonArray bandRolesArray = json.value( QStringLiteral( "required_band_roles" ) ).toArray();
    for ( const QJsonValue &role : bandRolesArray )
        goal.requiredBandRoles.append( role.toString() );

    goal.maxCloudCoverPercent = json.value( QStringLiteral( "max_cloud_cover_percent" ) ).toDouble( -1.0 );
    goal.minSamples = static_cast< qint64 >(
        json.value( QStringLiteral( "min_samples" ) ).toDouble( 0.0 ) );

    const QJsonArray classesArray = json.value( QStringLiteral( "required_classes" ) ).toArray();
    for ( const QJsonValue &className : classesArray )
        goal.requiredClasses.append( className.toString() );
    goal.requireLabels = json.value( QStringLiteral( "require_labels" ) ).toBool( false );

    goal.hasModel = json.value( QStringLiteral( "has_model" ) ).toBool( false );
    if ( goal.hasModel )
    {
        const QJsonArray modelRolesArray =
            json.value( QStringLiteral( "model_required_band_roles" ) ).toArray();
        for ( const QJsonValue &role : modelRolesArray )
            goal.modelRequiredBandRoles.append( role.toString() );
        goal.modelMinGsdM = json.value( QStringLiteral( "model_min_gsd_m" ) ).toDouble();
        goal.modelMaxGsdM = json.value( QStringLiteral( "model_max_gsd_m" ) ).toDouble();
        goal.modelModality = json.value( QStringLiteral( "model_modality" ) ).toString();
    }

    goal.minCoverageFraction = json.value( QStringLiteral( "min_coverage_fraction" ) ).toDouble();
    goal.minScenesInWindow = static_cast< qint64 >(
        json.value( QStringLiteral( "min_scenes_in_window" ) ).toDouble( 0.0 ) );

    // Unknown fields are ignored (forward compatibility), but the values that
    // did parse obey the same contract as an in-memory goal.
    const auto validated = validateGoal( goal );
    if ( !validated.has_value() )
        return sicnu::data::Result<SuitabilityGoal>::failure( validated.diagnostics() );

    return sicnu::data::Result<SuitabilityGoal>::success( goal );
}

QString SuitabilityGoal::contentDigest() const
{
    const QJsonDocument document( toJson() );
    const QByteArray canonical = document.toJson( QJsonDocument::Compact );
    return QString::fromLatin1(
        QCryptographicHash::hash( canonical, QCryptographicHash::Sha256 ).toHex() );
}

sicnu::data::Result<ResolvedRequirements> resolveRequirements( const SuitabilityGoal &goal )
{
    const auto validated = validateGoal( goal );
    if ( !validated.has_value() )
        return sicnu::data::Result<ResolvedRequirements>::failure( validated.diagnostics() );

    ResolvedRequirements resolved;
    resolved.hasAoi = goal.hasAoi;
    resolved.aoi = goal.aoi;
    resolved.aoiCrsWkt = goal.aoiCrsWkt;
    resolved.minCoverageFractionResolved = goal.minCoverageFraction > 0.0
                                               ? goal.minCoverageFraction
                                               : kDefaultMinCoverageFraction;
    resolved.minGsdM = goal.minGsdM;
    resolved.maxGsdM = goal.maxGsdM;
    resolved.requiredBandRoles = goal.requiredBandRoles;
    resolved.maxCloudCoverPercent = goal.maxCloudCoverPercent;
    resolved.hasTimeWindow = goal.hasTimeWindow;
    resolved.windowStartUtc = goal.windowStartUtc;
    resolved.windowEndUtc = goal.windowEndUtc;
    resolved.requiredSeasons = goal.requiredSeasons;
    resolved.minScenesInWindow = goal.minScenesInWindow;
    return sicnu::data::Result<ResolvedRequirements>::success( resolved );
}

} // namespace sicnu::suitability
