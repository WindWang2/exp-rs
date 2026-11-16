// run_matrix_metric.h — typed metric-cell projection + bounded matrix paging
// (R30-T07 WP05). Qt Core only; owns no evaluation math.
//
// The dock used to render `r.metrics.contains(key) ? value(key).toDouble()
// : begin().value().toDouble()`. A missing selected metric silently showed a
// DIFFERENT metric's number, and any object/null/undefined value became 0 —
// indistinguishable from a real measured zero. This projection keeps the
// metric IDENTITY and distinguishes every non-value shape.
#pragma once

#include "experiment_studio/run_matrix_projection.h"
#include "study/study_export.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace sicnu::experiment_studio
{

enum class RunMetricCellKind
{
    Value,        ///< a finite numeric measurement (including a real 0)
    MissingKey,   ///< the requested metric was not recorded for this run
    Null,         ///< recorded but JSON null (no measurement)
    Object,       ///< structured per-class document, not a scalar
    Array,        ///< array-valued metric, not a scalar
    String,       ///< non-numeric string
    Bool,         ///< boolean
    Undefined,    ///< QJsonValue::Undefined
    NonFinite,    ///< NaN / +/-Inf (should never come from a JSON reader, but
                  ///< can arrive from an in-memory synthesized report)
};

const char *runMetricCellKindName( RunMetricCellKind kind );

struct RunMetricCell
{
    QString metricName;
    QString runId;
    QString unit;       ///< optional display unit from the declared spec
    QString protocol;   ///< optional evaluation protocol label
    RunMetricCellKind kind = RunMetricCellKind::MissingKey;
    double value = 0.0; ///< meaningful ONLY for kind == Value
    QString display;    ///< never "0" for a non-value
    QString reason;     ///< stable dotted reason code for logs/export

    QJsonObject toJson() const;
    bool hasValue() const { return kind == RunMetricCellKind::Value; }
};

/// Pure projection. Never falls back to another metric, never calls toDouble
/// on a non-numeric value. @p unit / @p protocol are display metadata only.
RunMetricCell projectRunMetricCell( const QJsonObject &rowMetrics,
                                    const QString &metricName,
                                    const QString &runId = {},
                                    const QString &unit = {},
                                    const QString &protocol = {} );

struct RunMatrixPage
{
    int page = 0;             ///< zero-based, clamped to [0, pageCount-1]
    int pageSize = 100;
    int totalRows = 0;
    int pageCount = 1;
    int firstRow = 0;         ///< absolute index into the filtered rows
    QVector<sicnu::study::StudyRunRow> rows;
    /// Stable selection MEMORY (all pages), keyed by pointIdentityKey(). The
    /// visible page only resolves the entries that land on it; paging never
    /// drops a selection on another page.
    QStringList selectedKeys;
    QStringList issues;

    QJsonObject toJson() const;
};

/// Bounded, deterministic paging over a filtered matrix view model.
/// @p requestedPage is clamped (never wraps silently); @p pageSize is clamped
/// to [1, 500]. @p stableSelection carries keys across rebuilds.
RunMatrixPage projectRunMatrixPage( const RunMatrixViewModel &vm,
                                    int requestedPage,
                                    int pageSize,
                                    const QStringList &stableSelection = {} );

} // namespace sicnu::experiment_studio
