#include "criteria_model.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>
#include <limits>

namespace sicnu::suitability
{

namespace
{

SuitabilityCriterion makeCriterion( const QString &id )
{
    SuitabilityCriterion criterion;
    criterion.id = id;
    return criterion;
}

SuitabilityGap makeGap( const QString &criterionId, const QString &id, const QString &description )
{
    SuitabilityGap gap;
    gap.id = id;
    gap.criterionId = criterionId;
    gap.description = description;
    return gap;
}

/// band_role.h/modality vocabulary is lowercase with underscores; normalize
/// for matching and for stable gap ids (same rule as the spectral criterion).
QString normalizedToken( const QString &text )
{
    return text.trimmed().toLower().replace( QLatin1Char( ' ' ), QLatin1Char( '_' ) );
}

QStringList normalizedUnique( const QStringList &values )
{
    QStringList result;
    for ( const QString &value : values )
    {
        const QString normalized = normalizedToken( value );
        if ( !normalized.isEmpty() && !result.contains( normalized ) )
            result.append( normalized );
    }
    return result;
}

} // namespace

SuitabilityCriterion assessModelCompatibility( const ResolvedRequirements &req,
                                               const QVector<SceneCandidate> &scenes,
                                               const std::optional<DatasetFacts> &facts )
{
    SuitabilityCriterion criterion = makeCriterion( QStringLiteral( "model.compatibility" ) );

    if ( !req.hasModel )
    {
        criterion.applicable = false;
        criterion.level = SuitabilityLevel::Unknown;
        criterion.summary = QStringLiteral( "No model pinned; model fit is not assessed." );
        criterion.evidence.insert( QStringLiteral( "status" ), QStringLiteral( "not_applicable" ) );
        return criterion;
    }

    // Fused data-side evidence pool: usable scenes first, facts second.
    QStringList bandPool;
    QStringList modalityPool;
    QVector<double> gsds;
    int invalidGsdCount = 0;
    for ( const SceneCandidate &scene : scenes )
    {
        if ( !scene.usable() )
            continue;
        for ( const QString &role : normalizedUnique( scene.bandRoles ) )
        {
            if ( !bandPool.contains( role ) )
                bandPool.append( role );
        }
        const QString modality = normalizedToken( scene.modality );
        if ( !modality.isEmpty() && !modalityPool.contains( modality ) )
            modalityPool.append( modality );
        if ( scene.gsdM.has_value() )
        {
            // Broken GSD metadata (non-finite/non-positive) never becomes
            // model-fit evidence: NaN would silently satisfy every range
            // comparison below.
            if ( std::isfinite( *scene.gsdM ) && *scene.gsdM > 0.0 )
                gsds.append( *scene.gsdM );
            else
                ++invalidGsdCount;
        }
    }
    if ( facts.has_value() )
    {
        for ( const QString &role : normalizedUnique( facts->bandRoles ) )
        {
            if ( !bandPool.contains( role ) )
                bandPool.append( role );
        }
        const QString modality = normalizedToken( facts->modality );
        if ( !modality.isEmpty() && !modalityPool.contains( modality ) )
            modalityPool.append( modality );
    }

    const QStringList requiredRoles = normalizedUnique( req.modelRequiredBandRoles );
    criterion.evidence.insert( QStringLiteral( "model_required_band_roles" ),
                               QJsonArray::fromStringList( requiredRoles ) );
    if ( req.modelMinGsdM > 0.0 )
        criterion.evidence.insert( QStringLiteral( "model_min_gsd_m" ), req.modelMinGsdM );
    if ( req.modelMaxGsdM > 0.0 )
        criterion.evidence.insert( QStringLiteral( "model_max_gsd_m" ), req.modelMaxGsdM );
    criterion.evidence.insert( QStringLiteral( "model_modality" ),
                               normalizedToken( req.modelModality ) );
    criterion.evidence.insert( QStringLiteral( "band_pool" ),
                               QJsonArray::fromStringList( bandPool ) );
    criterion.evidence.insert( QStringLiteral( "modality_pool" ),
                               QJsonArray::fromStringList( modalityPool ) );
    criterion.evidence.insert( QStringLiteral( "gsd_sample_count" ), gsds.size() );
    criterion.evidence.insert( QStringLiteral( "gsd_invalid_count" ), invalidGsdCount );

    if ( bandPool.isEmpty() && modalityPool.isEmpty() && gsds.isEmpty() )
    {
        criterion.level = SuitabilityLevel::Unknown;
        criterion.summary = QStringLiteral(
            "Neither scenes nor dataset facts carry band, GSD or modality evidence; model fit is unmeasured." );
        criterion.notes.append( QStringLiteral( "no model-fit evidence available" ) );
        criterion.evidence.insert( QStringLiteral( "status" ), QStringLiteral( "unknown" ) );
        criterion.evidence.insert( QStringLiteral( "reason" ),
                                   QStringLiteral( "model_evidence_absent" ) );
        return criterion;
    }

    bool unsuitable = false;

    for ( const QString &role : requiredRoles )
    {
        if ( bandPool.contains( role ) )
            continue;
        unsuitable = true;
        SuitabilityGap gap = makeGap(
            criterion.id, QStringLiteral( "model.band_missing.%1" ).arg( role ),
            QStringLiteral( "The model requires band role '%1', which no usable scene or dataset fact carries." )
                .arg( role ) );
        gap.evidence.insert( QStringLiteral( "required_role" ), role );
        criterion.gaps.append( gap );
    }

    bool partialGsd = false;
    bool gsdUnverifiable = false;
    if ( req.modelMinGsdM > 0.0 || req.modelMaxGsdM > 0.0 )
    {
        if ( gsds.isEmpty() )
        {
            // The model pins a resolution range but no valid GSD evidence
            // exists (none carried, or every value was broken): unknown,
            // never a silent pass on missing metadata.
            gsdUnverifiable = true;
        }
        else
        {
            const double sceneMin = *std::min_element( gsds.cbegin(), gsds.cend() );
            const double sceneMax = *std::max_element( gsds.cbegin(), gsds.cend() );
            const bool entirelyTooFine =
                req.modelMaxGsdM > 0.0 && sceneMin > req.modelMaxGsdM;
            const bool entirelyTooCoarse =
                req.modelMinGsdM > 0.0 && sceneMax < req.modelMinGsdM;
            if ( entirelyTooFine || entirelyTooCoarse )
            {
                unsuitable = true;
                SuitabilityGap gap = makeGap(
                    criterion.id, QStringLiteral( "model.resolution_out_of_range" ),
                    QStringLiteral( "The model's GSD range [%1, %2] m does not intersect the scenes' measured GSD range [%3, %4] m." )
                        .arg( req.modelMinGsdM, 0, 'g', 4 )
                        .arg( req.modelMaxGsdM, 0, 'g', 4 )
                        .arg( sceneMin, 0, 'g', 4 )
                        .arg( sceneMax, 0, 'g', 4 ) );
                gap.evidence.insert( QStringLiteral( "model_min_gsd_m" ), req.modelMinGsdM );
                gap.evidence.insert( QStringLiteral( "model_max_gsd_m" ), req.modelMaxGsdM );
                gap.evidence.insert( QStringLiteral( "scene_min_gsd_m" ), sceneMin );
                gap.evidence.insert( QStringLiteral( "scene_max_gsd_m" ), sceneMax );
                criterion.gaps.append( gap );
            }
            else
            {
                const auto withinModel = [ &req ]( double gsd )
                {
                    if ( req.modelMinGsdM > 0.0 && gsd < req.modelMinGsdM )
                        return false;
                    if ( req.modelMaxGsdM > 0.0 && gsd > req.modelMaxGsdM )
                        return false;
                    return true;
                };
                int inRange = 0;
                int outOfRange = 0;
                for ( const double gsd : gsds )
                {
                    if ( withinModel( gsd ) )
                        ++inRange;
                    else
                        ++outOfRange;
                }
                criterion.evidence.insert( QStringLiteral( "gsd_in_model_range_count" ), inRange );
                criterion.evidence.insert( QStringLiteral( "gsd_out_of_model_range_count" ),
                                           outOfRange );
                partialGsd = inRange > 0 && outOfRange > 0;
            }
        }
    }
    const QString modelModality = normalizedToken( req.modelModality );
    bool modalityUnverifiable = false;
    if ( !modelModality.isEmpty() )
    {
        if ( modalityPool.isEmpty() )
        {
            // The model pins a modality but no source reports one: unknown,
            // never a silent pass on missing metadata (mirrors the GSD
            // handling above).
            modalityUnverifiable = true;
        }
        else if ( !modalityPool.contains( modelModality ) )
        {
            unsuitable = true;
            SuitabilityGap gap = makeGap(
                criterion.id, QStringLiteral( "model.modality_mismatch" ),
                QStringLiteral( "The model expects modality '%1', but every data source reports a different modality." )
                    .arg( modelModality ) );
            gap.evidence.insert( QStringLiteral( "model_modality" ), modelModality );
            gap.evidence.insert( QStringLiteral( "modality_pool" ),
                                 QJsonArray::fromStringList( modalityPool ) );
            criterion.gaps.append( gap );
        }
    }

    if ( unsuitable )
    {
        criterion.level = SuitabilityLevel::Unsuitable;
        criterion.summary = QStringLiteral(
            "The data does not satisfy the pinned model's input requirements." );
        criterion.evidence.insert( QStringLiteral( "status" ), QStringLiteral( "unsuitable" ) );
        return criterion;
    }

    if ( partialGsd )
    {
        criterion.level = SuitabilityLevel::Marginal;
        criterion.summary = QStringLiteral(
            "The model's GSD range covers only part of the usable scenes." );
        criterion.notes.append( QStringLiteral(
            "some scenes fall outside the model's GSD range" ) );
        criterion.evidence.insert( QStringLiteral( "status" ), QStringLiteral( "marginal" ) );
        return criterion;
    }

    if ( gsdUnverifiable )
    {
        criterion.level = SuitabilityLevel::Unknown;
        criterion.summary = QStringLiteral(
            "The model pins a GSD range but no valid GSD evidence exists; resolution fit is unmeasured." );
        criterion.notes.append( invalidGsdCount > 0
                                     ? QStringLiteral( "GSD metadata present but invalid (non-finite or non-positive)" )
                                     : QStringLiteral( "no scene carries a meter GSD" ) );
        criterion.evidence.insert( QStringLiteral( "status" ), QStringLiteral( "unknown" ) );
        criterion.evidence.insert(
            QStringLiteral( "reason" ),
            invalidGsdCount > 0 ? QStringLiteral( "gsd_evidence_invalid" )
                                : QStringLiteral( "gsd_evidence_absent" ) );
        return criterion;
    }

    if ( modalityUnverifiable )
    {
        criterion.level = SuitabilityLevel::Unknown;
        criterion.summary = QStringLiteral(
            "The model pins a modality but no source reports any modality; modality fit is unmeasured." );
        criterion.notes.append( QStringLiteral( "no modality evidence available" ) );
        criterion.evidence.insert( QStringLiteral( "status" ), QStringLiteral( "unknown" ) );
        criterion.evidence.insert( QStringLiteral( "reason" ),
                                   QStringLiteral( "modality_evidence_absent" ) );
        return criterion;
    }

    criterion.level = SuitabilityLevel::Suitable;
    criterion.summary = QStringLiteral( "The data satisfies the pinned model's input requirements." );
    return criterion;
}

} // namespace sicnu::suitability
