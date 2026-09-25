#include "suitability_goal.h"

#include "suitability_time.h"

#include "suitability_profiles.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>

#include <cmath>
#include <limits>

namespace sicnu::suitability
{

namespace
{

/// Doubles below 2^53 represent integers exactly; anything beyond cannot be
/// a faithful qint64 and a cast would be undefined behavior (e.g. JSON
/// 1e300 or an infinity from 1e999).
inline constexpr double kMaxExactIntegralDouble = 9007199254740992.0; // 2^53

sicnu::data::Diagnostic invalidGoal( const QString &message )
{
    return sicnu::data::Diagnostic{ QStringLiteral( "suitability.goal_invalid" ),
                                    message,
                                    sicnu::data::DiagnosticSeverity::Error };
}

/// Parses a floating goal field: the value must be a JSON number when
/// present. A wrong type (e.g. "40" as a string) is a typed failure —
/// silently reading it as "unset" would apply limits the operator never
/// gave (or drop the ones they did).
sicnu::data::Result<double> boundedDouble( const QJsonObject &json, const QString &key,
                                           double defaultValue )
{
    const QJsonValue value = json.value( key );
    if ( value.isUndefined() || value.isNull() )
        return sicnu::data::Result<double>::success( defaultValue );
    if ( !value.isDouble() )
        return sicnu::data::Result<double>::failure(
            invalidGoal( QStringLiteral( "field '%1' must be a number" ).arg( key ) ) );
    return sicnu::data::Result<double>::success( value.toDouble() );
}

/// Parses an integral goal field from JSON without UB: the value must be a
/// finite double in exact-integer range. Missing fields keep their default.
sicnu::data::Result<qint64> boundedIntegral( const QJsonObject &json, const QString &key,
                                             qint64 defaultValue )
{
    const QJsonValue value = json.value( key );
    if ( !value.isDouble() )
        return sicnu::data::Result<qint64>::success( defaultValue );
    const double raw = value.toDouble();
    if ( !std::isfinite( raw ) || raw < -kMaxExactIntegralDouble
         || raw > kMaxExactIntegralDouble )
    {
        return sicnu::data::Result<qint64>::failure( invalidGoal(
            QStringLiteral( "%1 is not a representable integer" ).arg( key ) ) );
    }
    return sicnu::data::Result<qint64>::success( static_cast< qint64 >( raw ) );
}

/// The single validation gate for goal values. Both fromJson and
/// resolveRequirements run it, so a parsed goal and an in-memory goal obey
/// the same contract.
sicnu::data::Result<void> validateGoal( const SuitabilityGoal &goal )
{
    if ( !goal.profileKey.isEmpty() )
    {
        // A profileKey must name a built-in profile; "phenology" is an
        // extra profile that only exists under the TemporalPrediction
        // family. Anything else is refused instead of pretending the
        // default profile matches the requested one.
        const bool selectable =
            isBuiltinProfile( goal.profileKey )
            && ( goal.profileKey != QLatin1String( "phenology" )
                 || goal.taskFamily == sicnu::dataset::BenchmarkTaskFamily::TemporalPrediction );
        if ( !selectable )
        {
            return sicnu::data::Result<void>::failure( sicnu::data::Diagnostic{
                QStringLiteral( "suitability.profile_unknown" ),
                QStringLiteral( "unknown suitability profile '%1'" ).arg( goal.profileKey ),
                sicnu::data::DiagnosticSeverity::Error } );
        }
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
    // Absurd numbers are invalid goals, not "unset": NaN would silently pass
    // every comparison downstream and a negative GSD bound is meaningless.
    const double checkedDoubles[] = { goal.minGsdM,          goal.maxGsdM,
                                      goal.maxCloudCoverPercent, goal.minCoverageFraction,
                                      goal.modelMinGsdM,     goal.modelMaxGsdM };
    for ( const double value : checkedDoubles )
    {
        if ( !std::isfinite( value ) )
            return sicnu::data::Result<void>::failure( invalidGoal(
                QStringLiteral( "goal carries a non-finite numeric value" ) ) );
    }
    if ( goal.minGsdM < 0.0 || goal.maxGsdM < 0.0 || goal.modelMinGsdM < 0.0
         || goal.modelMaxGsdM < 0.0 )
        return sicnu::data::Result<void>::failure(
            invalidGoal( QStringLiteral( "GSD bounds must not be negative" ) ) );
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
        const char *aoiKeys[] = { "min_x", "min_y", "max_x", "max_y" };
        double *aoiTargets[] = { &goal.aoi.minimumX, &goal.aoi.minimumY, &goal.aoi.maximumX,
                                 &goal.aoi.maximumY };
        for ( int i = 0; i < 4; ++i )
        {
            const auto coordinate = boundedDouble( aoiJson, QLatin1String( aoiKeys[i] ), 0.0 );
            if ( !coordinate.has_value() )
                return sicnu::data::Result<SuitabilityGoal>::failure( coordinate.diagnostics() );
            *aoiTargets[i] = coordinate.value();
        }
        goal.aoi.valid = aoiJson.value( QStringLiteral( "valid" ) ).toBool( false );
        goal.aoiCrsWkt = json.value( QStringLiteral( "aoi_crs_wkt" ) ).toString();
    }

    goal.hasTimeWindow = json.value( QStringLiteral( "has_time_window" ) ).toBool( false );
    if ( goal.hasTimeWindow )
    {
        // Zoneless stamps read as UTC (keys say _utc); shared with the scene
        // reader so goal and scenes never disagree on the absolute window.
        goal.windowStartUtc =
            parseIsoUtc( json.value( QStringLiteral( "window_start_utc" ) ).toString() );
        goal.windowEndUtc =
            parseIsoUtc( json.value( QStringLiteral( "window_end_utc" ) ).toString() );
    }

    const QJsonArray seasonsArray = json.value( QStringLiteral( "required_seasons" ) ).toArray();
    for ( const QJsonValue &season : seasonsArray )
        goal.requiredSeasons.append( season.toString() );

    const auto minGsd = boundedDouble( json, QStringLiteral( "min_gsd_m" ), 0.0 );
    if ( !minGsd.has_value() )
        return sicnu::data::Result<SuitabilityGoal>::failure( minGsd.diagnostics() );
    goal.minGsdM = minGsd.value();
    const auto maxGsd = boundedDouble( json, QStringLiteral( "max_gsd_m" ), 0.0 );
    if ( !maxGsd.has_value() )
        return sicnu::data::Result<SuitabilityGoal>::failure( maxGsd.diagnostics() );
    goal.maxGsdM = maxGsd.value();

    const QJsonArray bandRolesArray = json.value( QStringLiteral( "required_band_roles" ) ).toArray();
    for ( const QJsonValue &role : bandRolesArray )
        goal.requiredBandRoles.append( role.toString() );

    const auto maxCloud = boundedDouble( json, QStringLiteral( "max_cloud_cover_percent" ), -1.0 );
    if ( !maxCloud.has_value() )
        return sicnu::data::Result<SuitabilityGoal>::failure( maxCloud.diagnostics() );
    goal.maxCloudCoverPercent = maxCloud.value();
    const auto minSamples = boundedIntegral( json, QStringLiteral( "min_samples" ), 0 );
    if ( !minSamples.has_value() )
        return sicnu::data::Result<SuitabilityGoal>::failure( minSamples.diagnostics() );
    goal.minSamples = minSamples.value();

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
        const auto modelMinGsd =
            boundedDouble( json, QStringLiteral( "model_min_gsd_m" ), 0.0 );
        const auto modelMaxGsd =
            boundedDouble( json, QStringLiteral( "model_max_gsd_m" ), 0.0 );
        if ( !modelMinGsd.has_value() )
            return sicnu::data::Result<SuitabilityGoal>::failure( modelMinGsd.diagnostics() );
        if ( !modelMaxGsd.has_value() )
            return sicnu::data::Result<SuitabilityGoal>::failure( modelMaxGsd.diagnostics() );
        goal.modelMinGsdM = modelMinGsd.value();
        goal.modelMaxGsdM = modelMaxGsd.value();
        goal.modelModality = json.value( QStringLiteral( "model_modality" ) ).toString();
    }

    const auto minCoverage =
        boundedDouble( json, QStringLiteral( "min_coverage_fraction" ), 0.0 );
    if ( !minCoverage.has_value() )
        return sicnu::data::Result<SuitabilityGoal>::failure( minCoverage.diagnostics() );
    goal.minCoverageFraction = minCoverage.value();
    const auto minScenes = boundedIntegral( json, QStringLiteral( "min_scenes_in_window" ), 0 );
    if ( !minScenes.has_value() )
        return sicnu::data::Result<SuitabilityGoal>::failure( minScenes.diagnostics() );
    goal.minScenesInWindow = minScenes.value();

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

    // Profile selection: an empty key picks the task family's default
    // profile; a non-empty key passed validation (built-in, and phenology
    // only under TemporalPrediction).
    const QString profileKey = !goal.profileKey.isEmpty()
                                   ? goal.profileKey
                                   : defaultProfileKeyForFamily( goal.taskFamily );
    const auto profile = builtinProfile( profileKey );
    if ( !profile.has_value() )
    {
        // Defensive: validation above already made this unreachable.
        return sicnu::data::Result<ResolvedRequirements>::failure( sicnu::data::Diagnostic{
            QStringLiteral( "suitability.profile_unknown" ),
            QStringLiteral( "unknown suitability profile '%1'" ).arg( profileKey ),
            sicnu::data::DiagnosticSeverity::Error } );
    }

    // Overlay: the profile fills defaults, the explicit goal value always
    // wins. Unset goal scalars are 0 / empty / false / <0 by schema; a
    // false boolean therefore cannot suppress a profile default (the goal
    // schema has no tri-state) — declared in the profile documentation.
    ResolvedRequirements resolved;
    resolved.hasAoi = goal.hasAoi;
    resolved.aoi = goal.aoi;
    resolved.aoiCrsWkt = goal.aoiCrsWkt;
    resolved.minCoverageFractionResolved =
        goal.minCoverageFraction > 0.0
            ? goal.minCoverageFraction
            : profile->minCoverageFraction.value_or( kDefaultMinCoverageFraction );
    resolved.minGsdM = goal.minGsdM > 0.0 ? goal.minGsdM : profile->minGsdM.value_or( 0.0 );
    resolved.maxGsdM = goal.maxGsdM > 0.0 ? goal.maxGsdM : profile->maxGsdM.value_or( 0.0 );
    resolved.requiredBandRoles = goal.requiredBandRoles;
    resolved.maxCloudCoverPercent = goal.maxCloudCoverPercent;
    resolved.hasTimeWindow = goal.hasTimeWindow;
    resolved.windowStartUtc = goal.windowStartUtc;
    resolved.windowEndUtc = goal.windowEndUtc;
    resolved.requiredSeasons =
        !goal.requiredSeasons.isEmpty() ? goal.requiredSeasons : profile->requiredSeasons;
    resolved.minScenesInWindow = goal.minScenesInWindow > 0
                                     ? goal.minScenesInWindow
                                     : profile->minScenesInWindow.value_or( 0 );
    resolved.requireLabels = goal.requireLabels || profile->requireLabels.value_or( false );
    resolved.requiredClasses = goal.requiredClasses;
    resolved.minSamples = goal.minSamples > 0 ? goal.minSamples : profile->minSamples.value_or( 0 );
    resolved.pseudoLabelsAllowed = profile->pseudoLabelsAllowed.value_or( true );
    resolved.gridStrict = profile->gridStrict.value_or( false );
    resolved.hasModel = goal.hasModel;
    resolved.modelRequiredBandRoles = goal.modelRequiredBandRoles;
    resolved.modelMinGsdM = goal.modelMinGsdM;
    resolved.modelMaxGsdM = goal.modelMaxGsdM;
    resolved.modelModality = goal.modelModality;
    return sicnu::data::Result<ResolvedRequirements>::success( resolved );
}

} // namespace sicnu::suitability
