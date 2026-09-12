// experiment_matrix.h — Experiment Matrix (goal M5).
//
// A matrix is a bounded sweep descriptor: axes × values → cells. Cells are
// SUBMITTED BY THE CALLER through the existing execution chain (workflow
// coordinator / CLI runner); this module never schedules, spawns or queues
// anything — it describes, links and aggregates.
//
// Honesty contracts:
//   - every cell carries a deterministic cellId (canonical hash of its axis
//     assignments), so "same cell" is identity, not ordering;
//   - the cell↔run link is an explicit store lineage edge written by the
//     submitter — a cell with no edge reports MISSING, never silently
//     dropped from aggregates;
//   - aggregate statistics never mix cells with different identity pins
//     (the pins are part of the cell definition).
#pragma once

#include "experiment_store.h"
#include "run_bridge.h"

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace sicnu::experiment
{

/// Hard cap of matrix cells: sweeps are bounded by contract (goal §2.3 —
/// no unbounded fan-out), and the refusal is typed, not a silent truncation.
inline constexpr qint64 kMaxMatrixCells = 1000;

/// Where an axis value lands in the submitted run's identity.
enum class AxisRole
{
    DatasetVersion,  ///< value = dataset version id (pin, store-verified)
    SplitManifest,   ///< value = split manifest id (pin)
    Model,           ///< value = model catalog id (pin)
    Seed,            ///< value = decimal seed (pin)
    Tag,             ///< value = descriptive tag (recorded on the cell, NOT a pin)
};

QString axisRoleToString( AxisRole role );
std::optional<AxisRole> axisRoleFromString( const QString &text );

struct MatrixAxis
{
    QString name;             ///< "region", "year", "sensor", "model", "seed", …
    AxisRole role = AxisRole::Tag;
    QVector<QString> values;  ///< non-empty, distinct

    friend bool operator==( const MatrixAxis &, const MatrixAxis & ) = default;
};

struct MatrixCell
{
    QString cellId;                    ///< canonical content hash of the assignments
    QHash<QString, QString> assignments; ///< axis name → value
    RunPins pins;                      ///< derived from the role-mapped assignments

    QJsonObject toJson() const;
    static Result<MatrixCell> fromJson( const QJsonObject &json );

    friend bool operator==( const MatrixCell &, const MatrixCell & ) = default;
};

struct MatrixDescriptor
{
    QString matrixId;
    QString experimentId;  ///< the experiment every cell records into
    QString workflowId;    ///< the workflow each cell submits (execution side)
    QString name;
    QString objective;
    QVector<MatrixAxis> axes;

    sicnu::data::Result<void> validate() const;
    /// Deterministic cell enumeration (axes in declared order; values in
    /// declared order). Refuses when the product exceeds kMaxMatrixCells.
    Result<QVector<MatrixCell>> enumerateCells() const;

    QJsonObject toJson() const;
    static Result<MatrixDescriptor> fromJson( const QJsonObject &json );
};

/// The explicit cell↔run ledger (store lineage edges).
class MatrixLedger
{
  public:
    explicit MatrixLedger( ExperimentStore &store );

    /// Links one recorded run to its cell (idempotent by edge identity).
    sicnu::data::Result<void> link( const QString &cellId, const QString &runId );
    /// Run ids linked to @p cellId (store order, bounded).
    QStringList runsForCell( const QString &cellId, qint64 limit = 100 ) const;
    /// Cells that have at least one linked run, as cellId → run ids.
    QHash<QString, QStringList> ledgerForMatrix( const QVector<MatrixCell> &cells ) const;

  private:
    ExperimentStore &m_store;
};

/// Aggregate statistics over the recorded runs of one metric — computed
/// only over cells whose runs recorded that metric (no imputation).
struct MetricAggregate
{
    qint64 runCount = 0;   ///< runs that REPORTED this metric
    double mean = 0.0;
    double populationStdDev = 0.0;
    double min = 0.0;
    double max = 0.0;

    QJsonObject toJson() const;
};

struct CellAggregate
{
    QString cellId;
    QHash<QString, QString> assignments;
    /// "recorded" (≥1 linked Completed run), "missing" (no linked run),
    /// "failed" (linked runs exist, all terminal-failed/cancelled),
    /// "partial" (some linked runs failed, at least one recorded),
    /// "in_progress" (linked runs exist, none terminal yet).
    /// Note: recorded+missing+failed != total when in_progress cells exist.
    QString status;
    QStringList runIds;
    QHash<QString, MetricAggregate> metrics; ///< metric name → aggregate
};

struct MatrixAggregate
{
    QString matrixId;
    qint64 totalCells = 0;
    qint64 recordedCells = 0;
    qint64 missingCells = 0;
    qint64 failedCells = 0;
    QVector<CellAggregate> cells;

    QJsonObject toJson() const;
};

/// Aggregates the recorded truth of a matrix. Reads stores only.
class MatrixAggregator
{
  public:
    MatrixAggregator( ExperimentStore &store, MatrixLedger &ledger );

    /// @p metricNames select the metrics to aggregate; a metric a run did
    /// not record is absent from that cell's metrics (count reflects it).
    Result<MatrixAggregate> aggregate( const MatrixDescriptor &descriptor,
                                       const QVector<QString> &metricNames ) const;

    /// Non-dominated cells under @p maximizeMetrics (all treated as
    /// higher-is-better; only cells with EVERY named metric recorded are
    /// candidates — incomparable cells never win by absence).
    static QStringList paretoCellIds( const MatrixAggregate &aggregate,
                                      const QVector<QString> &maximizeMetrics );

  private:
    ExperimentStore *m_store = nullptr;
    MatrixLedger *m_ledger = nullptr;
};

} // namespace sicnu::experiment
