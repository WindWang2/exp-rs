#include "criteria_labels.h"

#include <QJsonArray>

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

} // namespace

SuitabilityCriterion assessLabelAvailability( const ResolvedRequirements &req,
                                              const std::optional<DatasetFacts> &facts )
{
    SuitabilityCriterion criterion = makeCriterion( QStringLiteral( "labels.availability" ) );

    if ( !req.requireLabels )
    {
        criterion.applicable = false;
        criterion.level = SuitabilityLevel::Unknown;
        criterion.summary = QStringLiteral( "No label requirement; label availability is not assessed." );
        criterion.evidence.insert( QStringLiteral( "status" ), QStringLiteral( "not_applicable" ) );
        // Truncation stays visible even where it cannot downgrade a verdict:
        // every other facts consumer of this report is reading partial data.
        if ( facts.has_value() && facts->factsTruncated )
        {
            criterion.notes.append( QStringLiteral(
                "dataset facts were truncated by the provider; unassessed dimensions may hide gaps" ) );
            criterion.evidence.insert( QStringLiteral( "facts_truncated" ), true );
        }
        return criterion;
    }

    if ( !facts.has_value() )
    {
        criterion.level = SuitabilityLevel::Unknown;
        criterion.summary = QStringLiteral( "No dataset facts were provided; label evidence is unmeasured." );
        criterion.notes.append( QStringLiteral( "no dataset facts provided" ) );
        criterion.evidence.insert( QStringLiteral( "status" ), QStringLiteral( "unknown" ) );
        criterion.evidence.insert( QStringLiteral( "reason" ), QStringLiteral( "facts_absent" ) );
        return criterion;
    }

    criterion.evidence.insert( QStringLiteral( "has_label_schema" ), facts->hasLabelSchema );
    criterion.evidence.insert( QStringLiteral( "observed_class_count" ), facts->labelClasses.size() );
    criterion.evidence.insert( QStringLiteral( "sample_count" ),
                               static_cast< qint64 >( facts->sampleCount ) );
    criterion.evidence.insert( QStringLiteral( "pseudo_label_count" ),
                               static_cast< qint64 >( facts->pseudoLabelCount ) );
    criterion.evidence.insert( QStringLiteral( "facts_truncated" ), facts->factsTruncated );
    if ( facts->factsTruncated )
    {
        criterion.notes.append( QStringLiteral(
            "dataset facts were truncated by the provider; verdicts rest on partial evidence" ) );
    }
    QJsonArray requiredClassesArray;
    for ( const QString &code : req.requiredClasses )
        requiredClassesArray.append( code );
    criterion.evidence.insert( QStringLiteral( "required_classes" ), requiredClassesArray );

    // Duplicate or blank class codes are goal noise: deduplicated/ignored for
    // gap generation (same posture as the spectral criterion's roles) while
    // the evidence above keeps the raw list.
    QStringList checkedClasses;
    for ( const QString &code : req.requiredClasses )
    {
        if ( code.trimmed().isEmpty() || checkedClasses.contains( code ) )
            continue;
        checkedClasses.append( code );
    }

    bool unsuitable = false;

    const bool schemaEvidence = facts->hasLabelSchema;
    const bool classEvidence = !facts->labelClasses.isEmpty();
    if ( !schemaEvidence && !classEvidence )
    {
        unsuitable = true;
        criterion.gaps.append( makeGap(
            criterion.id, QStringLiteral( "labels.schema_missing" ),
            QStringLiteral( "A label requirement is declared but the dataset carries no label schema and no class evidence." ) ) );
    }

    // Class coverage against the observed vocabulary; class codes match
    // exactly (they are store vocabulary, not free-form band text).
    qint64 missingClassCount = 0;
    for ( const QString &code : checkedClasses )
    {
        if ( facts->labelClasses.contains( code ) )
            continue;
        ++missingClassCount;
        SuitabilityGap gap = makeGap(
            criterion.id, QStringLiteral( "label.class_missing.%1" ).arg( code ),
            QStringLiteral( "Required class '%1' is absent from the dataset's observed label vocabulary." )
                .arg( code ) );
        gap.evidence.insert( QStringLiteral( "required_class" ), code );
        criterion.gaps.append( gap );
    }
    if ( missingClassCount > 0 )
        unsuitable = true;

    const bool volumeRequired = req.minSamples > 0;
    const bool volumeKnown = facts->sampleCount >= 0;
    const bool volumeBelow = volumeKnown && volumeRequired && facts->sampleCount < req.minSamples;
    if ( volumeBelow )
    {
        unsuitable = true;
        SuitabilityGap gap = makeGap(
            criterion.id, QStringLiteral( "samples.below_minimum" ),
            QStringLiteral( "The dataset holds %1 sample(s), below the required minimum of %2." )
                .arg( facts->sampleCount )
                .arg( req.minSamples ) );
        gap.evidence.insert( QStringLiteral( "required_min_samples" ), req.minSamples );
        gap.evidence.insert( QStringLiteral( "measured_sample_count" ),
                             static_cast< qint64 >( facts->sampleCount ) );
        criterion.gaps.append( gap );
    }

    const bool pseudoConflict = facts->pseudoLabelCount > 0 && !req.pseudoLabelsAllowed;
    if ( pseudoConflict )
    {
        SuitabilityGap gap = makeGap(
            criterion.id, QStringLiteral( "labels.pseudo_present" ),
            QStringLiteral( "The dataset carries %1 pseudo-label sample(s) while the requirement forbids them." )
                .arg( facts->pseudoLabelCount ) );
        gap.evidence.insert( QStringLiteral( "pseudo_label_count" ),
                             static_cast< qint64 >( facts->pseudoLabelCount ) );
        criterion.gaps.append( gap );
    }

    if ( unsuitable )
    {
        criterion.level = SuitabilityLevel::Unsuitable;
        criterion.summary = QStringLiteral( "The dataset's label evidence does not satisfy the requirement." );
        criterion.evidence.insert( QStringLiteral( "status" ), QStringLiteral( "unsuitable" ) );
        return criterion;
    }

    if ( pseudoConflict )
    {
        criterion.level = SuitabilityLevel::Marginal;
        criterion.summary = QStringLiteral(
            "Labels fit except for present pseudo-labels, which the requirement forbids." );
        criterion.evidence.insert( QStringLiteral( "status" ), QStringLiteral( "marginal" ) );
        return criterion;
    }

    if ( volumeRequired && !volumeKnown )
    {
        // The requirement exists but the volume was never measured: honest
        // unknown, never a silent pass.
        criterion.level = SuitabilityLevel::Unknown;
        criterion.summary = QStringLiteral( "The sample volume is unknown; the minimum cannot be checked." );
        criterion.evidence.insert( QStringLiteral( "status" ), QStringLiteral( "unknown" ) );
        criterion.evidence.insert( QStringLiteral( "reason" ), QStringLiteral( "sample_count_unknown" ) );
        return criterion;
    }

    if ( facts->factsTruncated )
    {
        // Truncated facts mean every passing verdict derived from them is
        // provisional: the fold bucket can hide vocabulary and the caps can
        // hide volumes we never saw. A pass therefore degrades to Marginal
        // (failures already fail; unknown stays unknown).
        criterion.level = SuitabilityLevel::Marginal;
        criterion.summary = QStringLiteral(
            "Label evidence satisfies the requirement, but the dataset facts were truncated; the pass is provisional." );
        criterion.evidence.insert( QStringLiteral( "status" ), QStringLiteral( "marginal" ) );
        criterion.evidence.insert( QStringLiteral( "truncation_downgraded" ), true );
        return criterion;
    }

    criterion.level = SuitabilityLevel::Suitable;
    criterion.summary = QStringLiteral( "The dataset's label evidence satisfies the requirement." );
    return criterion;
}

} // namespace sicnu::suitability
