// studio_types.h — Experiment Exploration Studio resource policy & schema
// markers (projection leaf). Owns NO second truth for study / faultlab /
// debugger / verifier / grader.
#pragma once

#include <QtGlobal>

namespace sicnu::experiment_studio
{

/// Studio projection document family.
inline constexpr char kStudioSessionSchema[] = "sicnu.experiment_studio.session/1";
inline constexpr char kStudioExportSchema[] = "sicnu.experiment_studio.export/1";
inline constexpr char kStudyDesignerVmSchema[] = "sicnu.experiment_studio.study_designer/1";
inline constexpr char kRunMatrixVmSchema[] = "sicnu.experiment_studio.run_matrix/1";
inline constexpr char kSpatialCompareVmSchema[] = "sicnu.experiment_studio.spatial_compare/1";
inline constexpr char kSensitivityVmSchema[] = "sicnu.experiment_studio.sensitivity/1";
inline constexpr char kFaultTeachingVmSchema[] = "sicnu.experiment_studio.fault_teaching/1";
inline constexpr char kFirstDivergenceVmSchema[] = "sicnu.experiment_studio.first_divergence/1";

/// Hard resource policy (UI admit + session). Core still refuses over its own
/// caps; these are the Studio-facing defaults students see.
struct StudioResourcePolicy
{
    qint64 maxPoints = 1000;          ///< mirror experiment::kMaxMatrixCells
    int maxInFlight = 8;              ///< mirror study::kStudyMaxInFlightBound
    qint64 maxDiskBytes = 512LL * 1024 * 1024; ///< session output + sandbox budget
    qint64 maxPerRunDeadlineMs = 600000;
    int spatialPreviewMaxSamples = 65536; ///< sampled preview bound (never full-pixel UI scan)
    int runTablePerfBaseline = 1000;      ///< synthetic table size for UI perf tests

    friend bool operator==( const StudioResourcePolicy &, const StudioResourcePolicy & ) = default;
};

inline StudioResourcePolicy defaultResourcePolicy()
{
    return {};
}

} // namespace sicnu::experiment_studio
