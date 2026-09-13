// lab_report.h — `sicnu.labreport.v1`: the teacher-facing lab report schema
// and its builder (D5: Lab Report & Lineage).
//
// The report is a PROJECTION of recorded truth, never a computation over
// guesses — the same contract as EvidenceProjector and the reproduction
// bundle:
//   - runs[]        come from the ExperimentStore (identity pins, timing,
//                   artifacts, workflow evidence) — never re-derived;
//   - steps[]       are the RSOperationLogger operation records, joined to
//                   runs by an EXPLICIT attribution policy (the operator
//                   trail carries no step identity — see ADR 0146);
//   - lineage{}     is a LineageGraph slice around the primary run (or, with
//                   no dataset store wired, the experiment-store edges around
//                   it — existence reported as unchecked, never asserted);
//   - replay{}      is ReplayReadiness::assess output verbatim (levels never
//                   overstate; unwired hooks are "unknown", blockers listed);
//   - environment{} is the run's RunEnvironment.redacted() (issue #789);
//   - grade{}       is a typed seam: "recorded" with inline copy + grading
//                   reference, or "unavailable" with a reason — never a
//                   fabricated score (D4 LabGradeResult is not merged yet).
//
// JSON-first: Markdown and HTML (lab_report_writers.h) are renderings of the
// ONE document produced here. Determinism contract: given identical inputs
// and an identical injected `generatedAtUtc`, two documents serialize to
// identical bytes — all arrays are explicitly ordered, and anything that
// passes through a hash container is re-sorted before insertion.
#pragma once

#include "../data/data_result.h"
#include "experiment/experiment_store.h"
#include "experiment/replay_readiness.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace sicnu::dataset
{
class DatasetStore;
}

namespace sicnu::experiment
{

inline constexpr const char *kLabReportSchemaId = "sicnu.labreport.v1";

/// Depth-complete secret pass for export (#789): RunEnvironment::
/// redactSecretKeys recurses objects and ONE array level, but stops at
/// arrays nested inside arrays — exactly where a trail parameter can hide a
/// credential under an innocuous key. This walks the whole structure and
/// applies the platform's key-matching redaction to EVERY object reached,
/// at any depth, through any number of array layers.
QJsonObject deepRedactSecretKeys( const QJsonObject &json );

/// How steps[] were joined to runs[] (declared in the document, never
/// implied — the join is an attribution, not a recorded fact).
inline constexpr const char *kLabStepAttributionPolicy = "time-window+operator-name";

/// The typed grade seam. When D4's LabGradeResult merges, it constructs the
/// "recorded" variant; until then every report carries the honest default.
inline constexpr const char *kLabGradeUnavailableReason =
    "grading module not available in this build";

struct LabGradeEmbedding
{
    /// "recorded" | "unavailable". Anything else is a builder error.
    QString status = QStringLiteral( "unavailable" );
    /// Required when recorded: where the authoritative grade lives (e.g. the
    /// grading store key), so the inline copy is verifiable, not orphaned.
    QString gradingRef;
    /// Structured reference details (grader, rubric version, graded-at, …).
    QJsonObject gradingRefDetails;
    /// The full grade document (LabGradeResult JSON once D4 exists).
    QJsonObject inlineResult;
    /// Human-readable unavailability reason (unavailable variant only).
    QString reason;

    static LabGradeEmbedding unavailable(
        const QString &reason = QString::fromUtf8( kLabGradeUnavailableReason ) )
    {
        LabGradeEmbedding g;
        g.reason = reason;
        return g;
    }

    QJsonObject toJson() const;
};

/// One output thumbnail as the caller supplies it (rendered through the
/// existing bounded preview path — sicnu::app::renderRasterPreview — by the
/// GUI; the experiment side stays GUI-free). The builder VALIDATES before
/// embedding: PNG magic + IHDR dimensions ≤ 512 px on the long edge, and
/// re-computes the digest. Oversized or non-PNG payloads are refused with a
/// warning — never embedded, never silently shrunk.
struct LabReportThumbnail
{
    QString sourcePath;       ///< artifact the pixels came from
    qint64 sourceSizeBytes = -1; ///< on-disk size of the source artifact
    QByteArray pngBytes;      /// complete PNG payload (already ≤ 512 px)
    QString error;            ///< non-empty = render failed; reported, never fabricated

    QJsonObject toJson() const;
};

struct LabReportRequest
{
    QString labId;            ///< experiment id in the store (required)
    QString labName;          ///< header metadata; falls back to the store experiment
    QString objective;        ///< same fallback rule
    QString student;          ///< header metadata (may be empty — never invented)
    QString session;          ///< header metadata (may be empty)
    QString gitSha;           ///< empty → "unknown" (no shelling out at export)
    QString softwareRevision; ///< empty → the run's stored revision, else "unknown"
    /// Primary run. Empty → the most recently finished (then started, then
    /// created) run of the experiment, deterministically.
    QString runId;
    /// Injected generation timestamp (ISO 8601). Empty → now. THIS is the
    /// one field allowed to differ between two builds of the same lab.
    QString generatedAtUtc;
    /// Operation-trail snapshot (RSOperationLogger records, Qt JSON array).
    /// Redacted + ordered by the builder; attributed to runs by policy.
    /// When EMPTY, the builder falls back to the trail recorded in each
    /// run's stored workflow evidence — so a report exported after an app
    /// restart is as complete as the live-session one.
    QJsonArray operationTrail;
    QVector<LabReportThumbnail> thumbnails;
    LabGradeEmbedding grade = LabGradeEmbedding::unavailable();
    /// Availability hooks for the replay checks; unwired hooks report
    /// "unknown" (BestEffort), never a fake exact.
    ReproductionHooks hooks;
    int lineageMaxDepth = 8;
    qint64 lineageMaxNodes = 500;
};

class LabReportBuilder
{
  public:
    /// @p datasetStore may be null — dataset/split replay checks then report
    /// "unknown" and the lineage slice degrades to the experiment-store
    /// edges (both honestly labelled in the document).
    explicit LabReportBuilder( const ExperimentStore &experimentStore,
                               const sicnu::dataset::DatasetStore *datasetStore = nullptr );

    /// Builds the complete `sicnu.labreport.v1` document. Typed failures:
    ///   lab.report_no_experiment — labId unknown to the store
    ///   lab.report_empty         — no runs AND no operation trail
    Result<QJsonObject> build( const LabReportRequest &request ) const;

    /// Structural validation of a report document (writers refuse to emit
    /// documents that fail this). Returns the first failure, if any.
    static Result<void> validate( const QJsonObject &document );

  private:
    Result<ExperimentRun> resolvePrimaryRun( const LabReportRequest &request,
                                             const QVector<ExperimentRun> &runs ) const;
    QJsonArray collectTrail( const LabReportRequest &request,
                             const QVector<ExperimentRun> &runs ) const;
    QJsonArray buildSteps( const QJsonArray &operationTrail,
                           const QVector<ExperimentRun> &runs ) const;
    QJsonObject buildLineage( const LabReportRequest &request,
                              const ExperimentRun &primaryRun ) const;
    QJsonArray buildThumbnails( const LabReportRequest &request, QJsonArray &warnings ) const;

    const ExperimentStore *m_store = nullptr;
    const sicnu::dataset::DatasetStore *m_datasetStore = nullptr;
};

} // namespace sicnu::experiment
