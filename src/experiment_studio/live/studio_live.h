// studio_live.h — the live-execution bridge of the Experiment Exploration
// Studio (completion/experiment-studio-live-execution).
//
// Replaces the Studio UI's synthetic/demo flows with REAL recorded truth.
// Every function is a thin composition over an EXISTING authority — this
// module is a bridge leaf in the spirit of src/study/bridge, and owns NO
// second store, NO sweep, NO thread pool, NO queue, NO silent resample:
//
//   - runStudy() executes through StudyRunner with an injected
//     IStudyExecutionBackend (production: ExecutionPlaneStudyBackend →
//     ExecutionPlane → TaskCenter → JobEngine). TaskCenter stays the ONLY
//     scheduling authority; tests inject the fake backend from
//     test_study_runner's contract. Run truth stays in the ExperimentStore.
//   - compareRecordedPoints() compares two COMMITTED study outputs through
//     the REAL GdalRasterDifferenceSummarizer. Grid mismatch stays a typed
//     study.spatial_mismatch refusal — never an implicit resample. A missing
//     committed output is a typed experiment_studio.output_missing refusal,
//     never a fabricated path.
//   - firstDivergenceReport() reads REAL recorded evidence through the
//     debugger's DirectoryEvidenceSource (checkpoint > provenance >
//     workflow metrics > typed evidence_absent). The analyzer's own
//     evidence-derived confidence is passed through untouched — the Studio
//     never claims more certainty than the evidence holds.
//   - runFaultScenarioTeaching() runs the REAL faultlab sandbox pipeline
//     (runFaultScenario: digest → copy → inject into the copy → … → re-digest)
//     and projects its ACTUAL digest facts; a mutated source surfaces as a
//     contract-violation issue in the VM, never as a success.
//   - exportRunCapsule() / capsuleReloadReadiness() go through the REAL
//     CapsuleBuilder / CapsuleIO / CapsuleReadiness. Callers (session,
//     export bundle) keep only REFS — run ids and capsule paths — never a
//     capsule body.
//
// The pure projection leaf (sicnu_experiment_studio) is reused for every
// view model; no VM is rebuilt or forked here.
#pragma once

#include "experiment_studio/fault_teaching_projection.h"
#include "experiment_studio/spatial_compare_projection.h"
#include "experiment_studio/studio_errors.h"
#include "experiment_studio/studio_types.h"
#include "study/study_export.h"
#include "study/study_runner.h"

#include <QJsonObject>
#include <QString>

#include <atomic>

namespace sicnu::dataset
{
class DatasetStore;
}

namespace sicnu::experiment
{
class ExperimentStore;
class MatrixLedger;
}

namespace sicnu::study
{
class IStudyExecutionBackend;
}

namespace sicnu::experiment_studio::live
{

using sicnu::data::Diagnostic;
using sicnu::data::Result;

/// Runs one parameter study through StudyRunner on the injected backend and
/// returns the runner's truthful accounting. The caller owns store, ledger,
/// spec and output directory; every submitted point ends recorded/failed/
/// cancelled per the runner's contract. Typed refusals pass through
/// (study.spec_*, study.budget_exceeded, study.store_unavailable, …).
Result<study::StudyRunSummary> runStudy( experiment::ExperimentStore &store,
                                         experiment::MatrixLedger &ledger,
                                         const study::ParameterStudySpec &spec,
                                         study::IStudyExecutionBackend &backend,
                                         const QString &studyOutputDir,
                                         const std::atomic<bool> &cancelFlag );

/// Projects the study's recorded truth (store + ledger) into the versioned
/// `sicnu.studyreport.v1` document — the SAME production projection
/// test_study_e2e exercises. Pure read; the store stays the only fact source.
study::StudyReport reportFromStore( experiment::ExperimentStore &store,
                                    experiment::MatrixLedger &ledger,
                                    const study::ParameterStudySpec &spec,
                                    const QVector<study::StudyPoint> &points,
                                    const QVector<study::SpatialDifferenceSummary> &spatialSummaries,
                                    const study::StudyRunSummary *runnerSummary );

/// Committed output path of one sampled point ("<dir>/<pointId>/output.tif",
/// the runner's documented layout). Typed refusal
/// experiment_studio.output_missing when the file is absent — callers never
/// guess or fabricate a path.
Result<QString> committedOutputPath( const QString &studyOutputDir, const QString &pointId );

/// Real GDAL comparison of two recorded points' committed outputs. Uses
/// GdalRasterDifferenceSummarizer; typed refusals pass through unchanged
/// (study.spatial_mismatch / study.spatial_too_large / _unreadable,
/// experiment_studio.output_missing).
Result<SpatialCompareViewModel> compareRecordedPoints(
    const QString &studyOutputDir, const QString &leftPointId, const QString &rightPointId,
    double epsilon, SpatialCompareMode mode = SpatialCompareMode::DiffSummary,
    const StudioResourcePolicy &policy = defaultResourcePolicy() );

/// First-divergence analysis of two recorded runs over REAL evidence
/// (DirectoryEvidenceSource over @p store + @p runDirectory). Returns the
/// analyzer's own `exp.debugger.divergence.v1` document; typed refusals
/// (unknown run) pass through. Absent step evidence is the analyzer's
/// honest incomplete verdict — never upgraded here.
Result<QJsonObject> firstDivergenceReport( experiment::ExperimentStore *store,
                                           const QString &runDirectory,
                                           const QString &referenceRunId,
                                           const QString &studentRunId );

/// Runs one fault scenario document (`sicnu.lab.faults/1`) through the REAL
/// faultlab pipeline and projects the ACTUAL run facts into the teaching VM:
/// systemDiagnosis is the diagnosed signature, sandboxUnchangedOriginal is
/// the runner's re-digest verdict, evidence is the canonical report. A
/// foreign/invalid scenario is a typed refusal (faultlab.* via
/// experiment_studio.fault_scenario_invalid), never a synthetic success.
/// @p sandboxRoot may be empty (the pipeline's own temp default).
Result<FaultTeachingViewModel> runFaultScenarioTeaching( const QJsonObject &scenarioJson,
                                                         const QString &studentPrediction,
                                                         const QString &sandboxRoot = {} );

/// One exported capsule REF — the only capsule shape a Studio session or
/// export bundle stores.
struct StudioCapsuleRef
{
    QString runId;
    QString path;
    qint64 bytes = 0;
};

/// Builds a capsule from ONE recorded run (real CapsuleBuilder), exports its
/// canonical bytes (real CapsuleIO), then reloads the file (real load gates)
/// to prove the artifact on disk is intact. @p workspaceRoot enables the
/// portability rewrite of recorded artifact paths (workspace:/external:).
/// @p createdUtc empty ⇒ wall clock (pass a fixed stamp for determinism).
Result<StudioCapsuleRef> exportRunCapsule( const experiment::ExperimentStore &store,
                                           const dataset::DatasetStore &datasets,
                                           const QString &runId, const QString &outputPath,
                                           const QString &workspaceRoot,
                                           const QString &createdUtc = {} );

/// Reloads a capsule from disk through the real load gates and assesses its
/// replay readiness (CapsuleReadiness; unwired hooks answer Unknown — never
/// a fabricated Exact). Returns { reload: <document echo>, readiness:
/// <report> } for Studio display; the document itself stays OUT of sessions.
Result<QJsonObject> capsuleReloadReadiness( const QString &capsulePath,
                                            const dataset::DatasetStore *datasets );

} // namespace sicnu::experiment_studio::live
