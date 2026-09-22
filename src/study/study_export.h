// study_export.h — the versioned study report (`sicnu.studyreport.v1`, RS14-07).
//
// One document, two audiences:
//   - TEACHING: the run table + curves + mechanical trend triples give the
//     parameter→result→interpretation chain (labels are factual observations,
//     never explanations or recommendations);
//   - AGENT: the same document is machine-readable evidence — versioned,
//     standalone-parseable, with explicit status accounting. No "best"
//     verdict exists unless the spec declared an objective metric.
//
// Reading contract (asymmetric with the spec, deliberately): a report READER
// refuses foreign schema versions but tolerates unknown fields — reports are
// observational evidence, and consumers must survive additive evolution.
// A SPEC reader refuses unknown fields because there a typo changes meaning.
//
// Persistence: atomic (Qt QSaveFile — staged temp + rename on commit). For a
// fixed input the bytes are stable (tests pin generatedAtUtc).
#pragma once

#include "study/study_analysis.h"
#include "study/study_runner.h"
#include "study/study_spatial.h"

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace sicnu::study
{

using sicnu::data::Result;

/// Bump on breaking change; the document also carries
/// "document_type": "sicnu.studyreport.v1".
inline constexpr int kStudyReportSchemaVersion = 1;

/// One run-table row — also the DTO a future QAbstractTableModel panel binds
/// (thin-client pattern, cf. dataset_experiment_panel). UI renders this;
/// it never derives business facts.
struct StudyRunRow
{
    QString pointId;
    int replicateIndex = 0;
    QString runId;      ///< empty = the study stopped before this point was submitted
    QString status;     ///< matrix vocabulary (recorded/failed/cancelled/partial/in_progress/missing)
    QHash<QString, QString> parameterAssignments; ///< dimension → value text, seed excluded
    quint64 seed = 0;
    QJsonObject metrics;   ///< metrics the run recorded (may be empty)
    QString errorSummary;  ///< "code: message" when failed/cancelled, else empty
    QString outputAssetPath;

    QJsonObject toJson() const;
    static Result<StudyRunRow> fromJson( const QJsonObject &json );

    friend bool operator==( const StudyRunRow &, const StudyRunRow & ) = default;
};

/// The mechanical teaching triple: parameter → observation (fact) with the
/// trend label. No causal language.
struct TrendTriple
{
    QString parameterPath;
    QString metricName;
    QString trend;      ///< curve trend label
    QString observation; ///< factual sentence with the measured endpoints

    QJsonObject toJson() const;
    friend bool operator==( const TrendTriple &, const TrendTriple & ) = default;
};

struct StudyReport
{
    QString studyId;
    QString experimentId;
    QString algorithmId;
    QString strategy;
    QJsonObject specJson; ///< full spec echo (replayability)
    QString stoppedReason;
    qint64 elapsedMs = 0;
    QDateTime generatedAtUtc;

    QVector<StudyRunRow> runTable;
    QVector<SensitivityCurve> curves;
    QVector<UncertaintyEnvelope> envelopes;
    QStringList paretoPointIds;
    QJsonObject declaredBest; ///< {point_id, value, basis} or empty
    QVector<SpatialDifferenceSummary> spatialSummaries;
    QVector<TrendTriple> narrative;

    // Explicit status accounting — sums to runTable.size().
    int recordedCount = 0;
    int failedCount = 0;
    int cancelledCount = 0;
    int missingCount = 0; ///< sampled but never executed (e.g. early cancel)

    QJsonObject toJson() const;
    static Result<StudyReport> fromJson( const QJsonObject &json );
};

/// Builds the report from recorded truth (store + ledger) and the sampled
/// points. @p runnerSummary (optional) carries the runner's live accounting;
/// @p spatialSummaries carries precomputed run-vs-baseline summaries.
/// @p generatedAtUtc defaults to now; tests pin it for byte-stable output.
StudyReport buildStudyReport( experiment::ExperimentStore &store,
                              experiment::MatrixLedger &ledger, const ParameterStudySpec &spec,
                              const QVector<StudyPoint> &points,
                              const QVector<SpatialDifferenceSummary> &spatialSummaries,
                              const StudyRunSummary *runnerSummary,
                              QDateTime generatedAtUtc = QDateTime::currentDateTimeUtc() );

/// Atomic write (temp + rename). Typed failures: study.report_write_failed.
Result<void> writeStudyReport( const StudyReport &report, const QString &path );

/// Composes run-vs-baseline spatial summaries for a study. Honours
/// `spec.spatialComparison` (the DECLARATION; this explicit call is the
/// composition, so the report stays a pure projection of what it is handed).
/// Baseline: the reference point's output (dimensions at reference ladder
/// values; LHS: first recorded output). Points without committed outputs are
/// skipped, never fabricated. Typed failures: study.spatial_no_baseline and
/// the summarizer's own codes (study.spatial_mismatch / _unreadable).
Result<QVector<SpatialDifferenceSummary>> summarizeStudyOutputs(
    experiment::ExperimentStore &store, experiment::MatrixLedger &ledger,
    const ParameterStudySpec &spec, const QVector<StudyPoint> &points,
    const QString &studyOutputDir, const ISpatialDifferenceSummarizer &summarizer );

} // namespace sicnu::study
