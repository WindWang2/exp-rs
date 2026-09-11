// experiment_store.h — persistent store for experiments and runs
// (ADR 0137). Same data-plane playbook as the dataset store: WAL, schema
// version with read-only forward tolerance, checked transactions, paged
// queries, mutex-guarded single connection.
#pragma once

#include "evaluation.h"
#include "experiment_types.h"

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
    std::optional<ExperimentRun> runById( const QString &runId ) const;
    sicnu::data::Result<QPair<qint64, QVector<ExperimentRun>>> listRuns(
        const QString &experimentId = QString(), const QString &datasetVersionId = QString(),
        const QString &status = QString(), qint64 offset = 0,
        qint64 limit = kMaxPageSize ) const;
    qint64 runCount() const;
    /// Run ids whose executionRef matches @p executionRef (store order, not
    /// recency). Bounded paged scan over the run JSON: a cold-path
    /// reconciliation helper, not a per-event lookup — callers tracking
    /// executions live keep their own ref→runId map.
    QStringList runIdsByExecutionRef( const QString &executionRef,
                                      qint64 limit = 10 ) const;

    // --- metric records -----------------------------------------------------------
    /// One metric record per run (the protocol+metrics of the run's primary
    /// evaluation). Re-saving with different content is a conflict.
    sicnu::data::Result<void> saveMetricRecord( const MetricRecord &record );
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
