// dataset_types.h — shared vocabularies for the dataset foundation (ADR 0134–0136).
//
// Small closed enums with strict string round-trips (used both for store
// columns and manifest JSON). Every fromString returns nullopt on unknown
// text — corrupt rows/payloads are reported, never silently defaulted
// (the ADR 0130 fail-conservative rule applied to vocabulary parsing).
#pragma once

#include "../data/data_result.h"

#include <QString>

#include <optional>

namespace sicnu::dataset
{

// Reuse the shared diagnostics vocabulary from the data layer.
using sicnu::data::Diagnostic;
using sicnu::data::DiagnosticSeverity;
using sicnu::data::Result;

/// Lifecycle of a dataset version. `Draft` is the only mutable state.
/// `Committed` versions are byte-stable by contract (ADR 0134); `Deprecated`
/// versions stay readable and referenced, they are just flagged.
enum class DatasetVersionStatus
{
    Draft,
    Committed,
    Deprecated,
};

QString datasetVersionStatusToString( DatasetVersionStatus status );
std::optional<DatasetVersionStatus> datasetVersionStatusFromString( const QString &text );

/// Quality gate of a dataset version (goal §20). Independent from the
/// draft/committed lifecycle: quality is an assertion about content, status
/// is an assertion about mutability.
enum class DatasetQualityLevel
{
    Unassessed,
    Draft,
    Valid,
    Certified,
};

QString datasetQualityLevelToString( DatasetQualityLevel level );
std::optional<DatasetQualityLevel> datasetQualityLevelFromString( const QString &text );

/// Sample payload family (ADR 0135). One enum, one typed payload per kind in
/// sample.h — no task-specific free-form samples.
enum class SampleKind
{
    Point,
    Pixel,
    Window,
    Patch,
    Polygon,
    Object,
    Pair,
    Temporal,
    MultiModal,
};

QString sampleKindToString( SampleKind kind );
std::optional<SampleKind> sampleKindFromString( const QString &text );

/// Where a label came from (goal §16). The distinction is provenance, not
/// quality: a model-assisted label can be reviewed; a human label can be wrong.
enum class AnnotationSourceType
{
    Human,
    FieldSurvey,
    ExistingMap,
    ManualInterpretation,
    ModelAssisted,
    Weak,
    Pseudo,
    ExternalDataset,
};

QString annotationSourceTypeToString( AnnotationSourceType type );
std::optional<AnnotationSourceType> annotationSourceTypeFromString( const QString &text );

/// Review state of one annotation revision.
enum class AnnotationReviewStatus
{
    Pending,
    Approved,
    Rejected,
};

QString annotationReviewStatusToString( AnnotationReviewStatus status );
std::optional<AnnotationReviewStatus> annotationReviewStatusFromString( const QString &text );

/// How a patch generator handles windows that leave the source raster
/// (goal §17). Recorded on every generated patch.
enum class BorderPolicy
{
    Drop,      ///< windows crossing the raster edge are discarded
    Clip,      ///< shrunken window is kept, footprint records the real extent
    Pad,       ///< window kept at full size, outside filled
    Reflect,   ///< window kept at full size, outside mirror-filled
    Constant,  ///< window kept at full size, outside constant-filled
};

QString borderPolicyToString( BorderPolicy policy );
std::optional<BorderPolicy> borderPolicyFromString( const QString &text );

/// How a patch generator treats NoData / invalid pixels (goal §17).
enum class NoDataMode
{
    Drop,               ///< patch discarded when any rule fires
    KeepWithFlag,       ///< patch kept, validity flag recorded on the sample
    MinValidFraction,   ///< drop unless validFraction >= threshold
    MaxNoDataFraction,  ///< drop when noDataFraction > threshold
};

QString noDataModeToString( NoDataMode mode );
std::optional<NoDataMode> noDataModeFromString( const QString &text );

/// Split strategy family (goal §18 / ADR 0136).
enum class SplitMethod
{
    Random,
    Stratified,
    Grouped,
    SpatialBlock,
    SpatialBuffer,
    Temporal,
    LeaveOneRegionOut,
    LeaveOneSceneOut,
    LeaveOneYearOut,
    KFold,
    SpatialKFold,
    GroupKFold,
    /// Joint space×time isolation: grid cell × time window is the atomic
    /// unit (9.0). Prevents the cross-year same-place autocorrelation that
    /// SpatialBlock alone leaves inside blocks and Temporal alone leaves
    /// inside periods.
    SpatioTemporalBlock,
};

QString splitMethodToString( SplitMethod method );
std::optional<SplitMethod> splitMethodFromString( const QString &text );

/// Assignment of one sample within a split manifest.
enum class SplitRole
{
    Train,
    Validation,
    Test,
    Unassigned,
};

QString splitRoleToString( SplitRole role );
std::optional<SplitRole> splitRoleFromString( const QString &text );

/// Honest determinism grading (goal §30), aligned with the operator
/// determinism grades (ADR 0124): a `NonDeterministic` step must say so and
/// why; nothing claims Strict without a seed policy behind it.
enum class DeterminismGrade
{
    Strict,
    BestEffort,
    NonDeterministic,
};

QString determinismGradeToString( DeterminismGrade grade );
std::optional<DeterminismGrade> determinismGradeFromString( const QString &text );

/// Leakage finding family of the audit (goal §19 / ADR 0136).
enum class LeakageKind
{
    ExactDuplicate,
    OverlappingPatch,
    SameParentPolygon,
    SameSourceObject,
    SameSourceScene,
    DistanceBelowThreshold,
    BufferOverlap,
    AugmentationParentLeakage,
    PseudoLabelParentLeakage,
    TemporalFutureLeakage,
    SameEventCrossing,
    SameTemporalGroupCrossing,
    PrePostPairLeakage,
};

QString leakageKindToString( LeakageKind kind );
std::optional<LeakageKind> leakageKindFromString( const QString &text );

/// Run status lifecycle (goal §22). Terminal states are Failed/Completed/
/// Cancelled; transitions are validated by the experiment store. States are
/// truthful: a run that died with the process stays Interrupted (never
/// silently rewritten to Completed).
enum class RunStatus
{
    Created,
    Running,
    Interrupted,
    Cancelling,
    Cancelled,
    Failed,
    Completed,
};

QString runStatusToString( RunStatus status );
std::optional<RunStatus> runStatusFromString( const QString &text );

/// Reproduction verdict of a validated bundle (goal §31).
enum class ReproductionLevel
{
    Exact,        ///< everything pinned and available; replay must be byte-identical
    Compatible,   ///< pins match, some environment dimension differs within policy
    BestEffort,   ///< replayable in principle, gaps recorded in the report
    Impossible,   ///< a required pin is missing or mismatched
};

QString reproductionLevelToString( ReproductionLevel level );
std::optional<ReproductionLevel> reproductionLevelFromString( const QString &text );

} // namespace sicnu::dataset
