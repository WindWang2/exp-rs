// experiment_store.h — persistent store for experiments and runs
// (ADR 0137). Same data-plane playbook as the dataset store: WAL, schema
// version with read-only forward tolerance, checked transactions, paged
// queries, mutex-guarded single connection.
#pragma once

#include "benchmark_definition.h"
#include "benchmark_runner.h"
#include "evaluation.h"
#include "experiment_types.h"

#include <QDateTime>
#include <QPair>
#include <QString>
#include <QVector>

#include <optional>

namespace sicnu::experiment
{

class ExperimentStore
{
  public:
    static constexpr qint64 kMaxPageSize = 500;

    ExperimentStore() = default;
    ~ExperimentStore();
    ExperimentStore( const ExperimentStore & ) = delete;
    ExperimentStore &operator=( const ExperimentStore & ) = delete;

    bool open( const QString &dbPath, QString *errorOut = nullptr );
    void close();
    bool isOpen() const { return m_impl != nullptr; }
    bool isReadOnly() const;
    QString schemaVersion() const;
    bool checkpointForBackup();

    // --- experiments ---------------------------------------------------------
    sicnu::data::Result<void> upsertExperiment( const Experiment &experiment );
    std::optional<Experiment> experimentById( const QString &experimentId ) const;
    sicnu::data::Result<QPair<qint64, QVector<Experiment>>> listExperiments(
        qint64 offset = 0, qint64 limit = kMaxPageSize ) const;
    sicnu::data::Result<void> deleteExperiment( const QString &experimentId );

    // --- runs ------------------------------------------------------------------
    /// Creates or advances a run. Status transitions are validated
    /// (`experiment.bad_transition`); a run may never jump backwards or
    /// leave a terminal state. Content updates ride the same checked
    /// transaction as the status change.
    sicnu::data::Result<void> upsertRun( const ExperimentRun &run );
    /// Batch creates/advances runs in ONE transaction (12.0 scale path):
    /// every run passes exactly the validation of upsertRun (transition,
    /// identity immutability, corrupt-row guard); the FIRST failure rolls
    /// back the whole batch — no partial batch ever becomes visible.
    sicnu::data::Result<void> upsertRunsBatch( const QVector<ExperimentRun> &runs );
    std::optional<ExperimentRun> runById( const QString &runId ) const;
    sicnu::data::Result<QPair<qint64, QVector<ExperimentRun>>> listRuns(
        const QString &experimentId = QString(), const QString &datasetVersionId = QString(),
        const QString &status = QString(), qint64 offset = 0,
        qint64 limit = kMaxPageSize ) const;
    /// Keyset-paged runs (12.0) ordered by (created_ms, run_id): deep pages
    /// cost O(log n), so paging a 100k-run store does not degrade toward the
    /// end. @p cursor is the opaque `nextCursor` of the previous page (empty
    /// = first page); it embeds the filter triple, so replaying a cursor
    /// under a different filter fails `experiment.cursor_mismatch`.
    struct RunCursorPage
    {
        QVector<ExperimentRun> runs;
        QString nextCursor; ///< empty after the last page
        qint64 total = 0;   ///< full match count, independent of the cursor
    };
    sicnu::data::Result<RunCursorPage> listRunsByCursor(
        const QString &experimentId = QString(), const QString &datasetVersionId = QString(),
        const QString &status = QString(), const QString &cursor = QString(),
        qint64 limit = kMaxPageSize ) const;
    /// Total runs, or -1 when the store is closed / the COUNT query fails
    /// (fail-closed; previously returned 0 on errors and looked empty).
    qint64 runCount() const;
    /// Run ids whose executionRef matches @p executionRef (store order, not
    /// recency). Bounded paged scan over the run JSON: a cold-path
    /// reconciliation helper, not a per-event lookup — callers tracking
    /// executions live keep their own ref→runId map.
    QStringList runIdsByExecutionRef( const QString &executionRef,
                                      qint64 limit = 10 ) const;
    /// Run ids whose stored execution fingerprint (the identity hash column)
    /// equals @p fingerprint — an indexed lookup (12.0): the seam the
    /// repeat-execution classifier and any duplicate-ingest guard use at
    /// 100k-run scale, where scanning every run's JSON is not acceptable.
    QStringList runIdsByExecutionFingerprint( const QString &fingerprint,
                                              qint64 limit = 50 ) const;

    // --- run retention / prune (12.0) ----------------------------------------
    /// Retention policy for recorded runs. Every rule is re-derived against
    /// the CURRENT store state by both plan and execute, so a plan is a
    /// statement about one store state, not a blank cheque.
    struct RunPrunePolicy
    {
        /// Collapse identity twins: only the newest run per execution
        /// fingerprint survives; older duplicate-ingest attempts become
        /// removable.
        bool collapseIdentityTwins = false;
        /// Runs created before this UTC moment are removable (invalid = off).
        QDateTime olderThan;
        /// Runs cited as promotion evidence are never removable (default).
        /// Setting false also DISCARDS the promotion evidence rows of pruned
        /// runs — an explicit policy choice, never a side effect.
        bool keepPromoted = true;
        /// Runs that are endpoints of experiment lineage edges are never
        /// removable (their provenance links survive). Setting false deletes
        /// the run's lineage edges with it, atomically.
        bool keepWithRunLineage = true;
        /// Runs cited by an immutable benchmark_results row are never
        /// removable (#1173). Setting false is not supported as a cascade
        /// delete of benchmark rows — refuse prune of cited runs instead.
        bool keepWithBenchmarkCitation = true;

        QJsonObject toJson() const;
    };
    struct RunPrunePlan
    {
        RunPrunePolicy policy; ///< the rules the plan was built with; execute
                               ///< re-derives them against current state
        QStringList runIds;    ///< ascending; exactly what execute will remove
        qint64 scannedRuns = 0;
        QJsonObject toJson() const;
    };
    /// Dry-run: the exact run set the policy removes against the CURRENT
    /// store state (Oracle O4: planRunPrune == executed deletions whenever
    /// the store does not change between the two calls).
    sicnu::data::Result<RunPrunePlan> planRunPrune( const RunPrunePolicy &policy ) const;
    /// Executes a plan inside ONE transaction: every id is re-checked for
    /// eligibility at execution time, so a stale plan shrinks — it can never
    /// over-delete. Run rows, their metric records, promotion rows (when
    /// !keepPromoted), lineage edges (when !keepWithRunLineage), and the
    /// affected experiments' run_id lists are updated atomically: no phantom
    /// references survive a prune. Runs cited by benchmark_results are
    /// refused when keepWithBenchmarkCitation (default).
    sicnu::data::Result<qint64> executeRunPrune( const RunPrunePlan &plan );

    // --- metric records -----------------------------------------------------------
    /// One metric record per run (the protocol+metrics of the run's primary
    /// evaluation). Re-saving with different content is a conflict.
    sicnu::data::Result<void> saveMetricRecord( const MetricRecord &record );
    /// Batch persists metric records in ONE transaction (12.0 scale path);
    /// same conflict rule as saveMetricRecord, all-or-nothing on the first
    /// failure.
    sicnu::data::Result<void> saveMetricRecordsBatch(
        const QVector<MetricRecord> &records );
    std::optional<MetricRecord> metricRecordForRun( const QString &runId ) const;
    sicnu::data::Result<QPair<qint64, QVector<MetricRecord>>> listMetricRecords(
        const QString &datasetVersionId = QString(), qint64 offset = 0,
        qint64 limit = kMaxPageSize ) const;

    // --- lineage (experiment-side edges; ADR 0138) ----------------------------------
    sicnu::data::Result<void> addLineageEdge( const QString &fromKind, const QString &fromId,
                                              const QString &edgeKind, const QString &toKind,
                                              const QString &toId );
    struct LineageEdge
    {
        QString fromKind;
        QString fromId;
        QString edgeKind;
        QString toKind;
        QString toId;
    };
    QVector<LineageEdge> outgoingEdges( const QString &kind, const QString &id,
                                        qint64 limit = 1000 ) const;
    QVector<LineageEdge> incomingEdges( const QString &kind, const QString &id,
                                        qint64 limit = 1000 ) const;
    /// Whole-table edge scan (graph assembly input; bounded by @p limit).
    QVector<LineageEdge> allLineageEdges( qint64 limit = 100000 ) const;

    // --- model promotion evidence (M8; evidence only, no registry) -----------
    /// Persists one promotion evidence record. Re-saving the same promotion
    /// id with different content is a conflict (`experiment.promotion_conflict`).
    sicnu::data::Result<void> savePromotionRecord( const PromotionRecord &record );
    std::optional<PromotionRecord> promotionById( const QString &promotionId ) const;
    /// Promotion evidence for one model catalog id (ascending creation order).
    QVector<PromotionRecord> promotionsForModel( const QString &modelId,
                                                 qint64 limit = 100 ) const;

    // --- benchmark definitions / results (D19; additive tables, schema stays v1) ---
    /// Persist a published BenchmarkDefinition. Same id@version with different
    /// contentDigest is a conflict; identical content is idempotent.
    sicnu::data::Result<void> saveBenchmarkDefinition( const BenchmarkDefinition &definition );
    std::optional<BenchmarkDefinition> benchmarkDefinition( const QString &benchmarkId,
                                                            quint64 version ) const;
    sicnu::data::Result<QPair<qint64, QVector<BenchmarkDefinition>>> listBenchmarkDefinitions(
        qint64 offset = 0, qint64 limit = kMaxPageSize ) const;

    /// Persist a BenchmarkResult by result_id (conflict on different content).
    sicnu::data::Result<void> saveBenchmarkResult( const BenchmarkResult &result );
    std::optional<BenchmarkResult> benchmarkResultById( const QString &resultId ) const;
    QVector<BenchmarkResult> benchmarkResultsFor( const QString &benchmarkId,
                                                  qint64 limit = 100 ) const;

  private:
    /// The real upsert path; `upsertRun()` wraps it with the unified-trace
    /// record (Verification Platform 8.0). No behavior change.
    sicnu::data::Result<void> upsertRunImpl( const ExperimentRun &run );

    struct Impl;
    Impl *m_impl = nullptr;
    QString m_storePath;
};

inline constexpr const char *kExperimentStoreSchemaVersion = "1";

} // namespace sicnu::experiment
