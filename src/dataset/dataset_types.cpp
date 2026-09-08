// dataset_types.cpp — vocabulary string round-trips.
#include "dataset_types.h"

#include <QHash>

namespace sicnu::dataset
{

namespace
{

/// Shared bidirectional table: one lookup structure for toString and
/// fromString, so the two can never drift apart (a vocabulary pair added on
/// one side only is a classic silent-corruption bug).
template <typename Enum>
class Vocabulary
{
  public:
    struct Pair
    {
        Enum value;
        const char *text;
    };

    explicit Vocabulary( std::initializer_list<Pair> pairs )
    {
        for ( const Pair &pair : pairs )
        {
            // Scoped enums never implicitly convert to int — be explicit at
            // the table boundary so both directions share one key type.
            m_toString.insert( static_cast<int>( pair.value ), QString::fromLatin1( pair.text ) );
            m_fromString.insert( QString::fromLatin1( pair.text ), pair.value );
        }
    }

    QString toString( Enum value ) const
    {
        return m_toString.value( static_cast<int>( value ) );
    }

    std::optional<Enum> fromString( const QString &text ) const
    {
        const auto it = m_fromString.constFind( text );
        if ( it == m_fromString.constEnd() )
            return std::nullopt;
        return *it;
    }

  private:
    QHash<int, QString> m_toString;
    QHash<QString, Enum> m_fromString;
};

const Vocabulary<DatasetVersionStatus> &versionStatusVocabulary()
{
    static const Vocabulary<DatasetVersionStatus> vocabulary( {
        { DatasetVersionStatus::Draft, "draft" },
        { DatasetVersionStatus::Committed, "committed" },
        { DatasetVersionStatus::Deprecated, "deprecated" },
    } );
    return vocabulary;
}

const Vocabulary<DatasetQualityLevel> &qualityLevelVocabulary()
{
    static const Vocabulary<DatasetQualityLevel> vocabulary( {
        { DatasetQualityLevel::Unassessed, "unassessed" },
        { DatasetQualityLevel::Draft, "draft_quality" },
        { DatasetQualityLevel::Valid, "valid" },
        { DatasetQualityLevel::Certified, "certified" },
    } );
    return vocabulary;
}

const Vocabulary<SampleKind> &sampleKindVocabulary()
{
    static const Vocabulary<SampleKind> vocabulary( {
        { SampleKind::Point, "point" },
        { SampleKind::Pixel, "pixel" },
        { SampleKind::Window, "window" },
        { SampleKind::Patch, "patch" },
        { SampleKind::Polygon, "polygon" },
        { SampleKind::Object, "object" },
        { SampleKind::Pair, "pair" },
        { SampleKind::Temporal, "temporal" },
        { SampleKind::MultiModal, "multimodal" },
    } );
    return vocabulary;
}

const Vocabulary<AnnotationSourceType> &annotationSourceVocabulary()
{
    static const Vocabulary<AnnotationSourceType> vocabulary( {
        { AnnotationSourceType::Human, "human" },
        { AnnotationSourceType::FieldSurvey, "field_survey" },
        { AnnotationSourceType::ExistingMap, "existing_map" },
        { AnnotationSourceType::ManualInterpretation, "manual_interpretation" },
        { AnnotationSourceType::ModelAssisted, "model_assisted" },
        { AnnotationSourceType::Weak, "weak" },
        { AnnotationSourceType::Pseudo, "pseudo" },
        { AnnotationSourceType::ExternalDataset, "external_dataset" },
    } );
    return vocabulary;
}

const Vocabulary<AnnotationReviewStatus> &reviewStatusVocabulary()
{
    static const Vocabulary<AnnotationReviewStatus> vocabulary( {
        { AnnotationReviewStatus::Pending, "pending" },
        { AnnotationReviewStatus::Approved, "approved" },
        { AnnotationReviewStatus::Rejected, "rejected" },
    } );
    return vocabulary;
}

const Vocabulary<BorderPolicy> &borderPolicyVocabulary()
{
    static const Vocabulary<BorderPolicy> vocabulary( {
        { BorderPolicy::Drop, "drop" },
        { BorderPolicy::Clip, "clip" },
        { BorderPolicy::Pad, "pad" },
        { BorderPolicy::Reflect, "reflect" },
        { BorderPolicy::Constant, "constant" },
    } );
    return vocabulary;
}

const Vocabulary<NoDataMode> &noDataModeVocabulary()
{
    static const Vocabulary<NoDataMode> vocabulary( {
        { NoDataMode::Drop, "drop" },
        { NoDataMode::KeepWithFlag, "keep_with_flag" },
        { NoDataMode::MinValidFraction, "min_valid_fraction" },
        { NoDataMode::MaxNoDataFraction, "max_nodata_fraction" },
    } );
    return vocabulary;
}

const Vocabulary<SplitMethod> &splitMethodVocabulary()
{
    static const Vocabulary<SplitMethod> vocabulary( {
        { SplitMethod::Random, "random" },
        { SplitMethod::Stratified, "stratified" },
        { SplitMethod::Grouped, "grouped" },
        { SplitMethod::SpatialBlock, "spatial_block" },
        { SplitMethod::SpatialBuffer, "spatial_buffer" },
        { SplitMethod::Temporal, "temporal" },
        { SplitMethod::LeaveOneRegionOut, "leave_one_region_out" },
        { SplitMethod::LeaveOneSceneOut, "leave_one_scene_out" },
        { SplitMethod::LeaveOneYearOut, "leave_one_year_out" },
        { SplitMethod::KFold, "k_fold" },
        { SplitMethod::SpatialKFold, "spatial_k_fold" },
        { SplitMethod::GroupKFold, "group_k_fold" },
    } );
    return vocabulary;
}

const Vocabulary<SplitRole> &splitRoleVocabulary()
{
    static const Vocabulary<SplitRole> vocabulary( {
        { SplitRole::Train, "train" },
        { SplitRole::Validation, "validation" },
        { SplitRole::Test, "test" },
        { SplitRole::Unassigned, "unassigned" },
    } );
    return vocabulary;
}

const Vocabulary<DeterminismGrade> &determinismVocabulary()
{
    static const Vocabulary<DeterminismGrade> vocabulary( {
        { DeterminismGrade::Strict, "strict" },
        { DeterminismGrade::BestEffort, "best_effort" },
        { DeterminismGrade::NonDeterministic, "non_deterministic" },
    } );
    return vocabulary;
}

const Vocabulary<LeakageKind> &leakageKindVocabulary()
{
    static const Vocabulary<LeakageKind> vocabulary( {
        { LeakageKind::ExactDuplicate, "exact_duplicate" },
        { LeakageKind::OverlappingPatch, "overlapping_patch" },
        { LeakageKind::SameParentPolygon, "same_parent_polygon" },
        { LeakageKind::SameSourceObject, "same_source_object" },
        { LeakageKind::SameSourceScene, "same_source_scene" },
        { LeakageKind::DistanceBelowThreshold, "distance_below_threshold" },
        { LeakageKind::BufferOverlap, "buffer_overlap" },
        { LeakageKind::AugmentationParentLeakage, "augmentation_parent_leakage" },
        { LeakageKind::PseudoLabelParentLeakage, "pseudo_label_parent_leakage" },
        { LeakageKind::TemporalFutureLeakage, "temporal_future_leakage" },
        { LeakageKind::SameEventCrossing, "same_event_crossing" },
        { LeakageKind::SameTemporalGroupCrossing, "same_temporal_group_crossing" },
        { LeakageKind::PrePostPairLeakage, "pre_post_pair_leakage" },
    } );
    return vocabulary;
}

const Vocabulary<RunStatus> &runStatusVocabulary()
{
    static const Vocabulary<RunStatus> vocabulary( {
        { RunStatus::Created, "created" },
        { RunStatus::Running, "running" },
        { RunStatus::Interrupted, "interrupted" },
        { RunStatus::Cancelling, "cancelling" },
        { RunStatus::Cancelled, "cancelled" },
        { RunStatus::Failed, "failed" },
        { RunStatus::Completed, "completed" },
    } );
    return vocabulary;
}

const Vocabulary<ReproductionLevel> &reproductionLevelVocabulary()
{
    static const Vocabulary<ReproductionLevel> vocabulary( {
        { ReproductionLevel::Exact, "exact" },
        { ReproductionLevel::Compatible, "compatible" },
        { ReproductionLevel::BestEffort, "best_effort" },
        { ReproductionLevel::Impossible, "impossible" },
    } );
    return vocabulary;
}

} // namespace

QString datasetVersionStatusToString( DatasetVersionStatus status )
{
    return versionStatusVocabulary().toString( status );
}

std::optional<DatasetVersionStatus> datasetVersionStatusFromString( const QString &text )
{
    return versionStatusVocabulary().fromString( text );
}

QString datasetQualityLevelToString( DatasetQualityLevel level )
{
    return qualityLevelVocabulary().toString( level );
}

std::optional<DatasetQualityLevel> datasetQualityLevelFromString( const QString &text )
{
    return qualityLevelVocabulary().fromString( text );
}

QString sampleKindToString( SampleKind kind )
{
    return sampleKindVocabulary().toString( kind );
}

std::optional<SampleKind> sampleKindFromString( const QString &text )
{
    return sampleKindVocabulary().fromString( text );
}

QString annotationSourceTypeToString( AnnotationSourceType type )
{
    return annotationSourceVocabulary().toString( type );
}

std::optional<AnnotationSourceType> annotationSourceTypeFromString( const QString &text )
{
    return annotationSourceVocabulary().fromString( text );
}

QString annotationReviewStatusToString( AnnotationReviewStatus status )
{
    return reviewStatusVocabulary().toString( status );
}

std::optional<AnnotationReviewStatus> annotationReviewStatusFromString( const QString &text )
{
    return reviewStatusVocabulary().fromString( text );
}

QString borderPolicyToString( BorderPolicy policy )
{
    return borderPolicyVocabulary().toString( policy );
}

std::optional<BorderPolicy> borderPolicyFromString( const QString &text )
{
    return borderPolicyVocabulary().fromString( text );
}

QString noDataModeToString( NoDataMode mode )
{
    return noDataModeVocabulary().toString( mode );
}

std::optional<NoDataMode> noDataModeFromString( const QString &text )
{
    return noDataModeVocabulary().fromString( text );
}

QString splitMethodToString( SplitMethod method )
{
    return splitMethodVocabulary().toString( method );
}

std::optional<SplitMethod> splitMethodFromString( const QString &text )
{
    return splitMethodVocabulary().fromString( text );
}

QString splitRoleToString( SplitRole role )
{
    return splitRoleVocabulary().toString( role );
}

std::optional<SplitRole> splitRoleFromString( const QString &text )
{
    return splitRoleVocabulary().fromString( text );
}

QString determinismGradeToString( DeterminismGrade grade )
{
    return determinismVocabulary().toString( grade );
}

std::optional<DeterminismGrade> determinismGradeFromString( const QString &text )
{
    return determinismVocabulary().fromString( text );
}

QString leakageKindToString( LeakageKind kind )
{
    return leakageKindVocabulary().toString( kind );
}

std::optional<LeakageKind> leakageKindFromString( const QString &text )
{
    return leakageKindVocabulary().fromString( text );
}

QString runStatusToString( RunStatus status )
{
    return runStatusVocabulary().toString( status );
}

std::optional<RunStatus> runStatusFromString( const QString &text )
{
    return runStatusVocabulary().fromString( text );
}

QString reproductionLevelToString( ReproductionLevel level )
{
    return reproductionLevelVocabulary().toString( level );
}

std::optional<ReproductionLevel> reproductionLevelFromString( const QString &text )
{
    return reproductionLevelVocabulary().fromString( text );
}

} // namespace sicnu::dataset
