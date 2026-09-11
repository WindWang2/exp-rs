// experiment_matrix.cpp — Experiment Matrix (goal M5).
//
// Descriptor/ledger/aggregator over the existing ExperimentStore. The
// execution chain stays untouched: nothing here submits a run.
#include "experiment_matrix.h"

#include "../data/data_result.h"
#include "../data/execution_fingerprint.h" // canonicalizeJsonRfc8785
#include "metric_path.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace sicnu::experiment
{

using sicnu::data::Diagnostic;
using sicnu::data::DiagnosticSeverity;
using sicnu::data::Result;

namespace
{

Diagnostic matrixError( const QString &code, const QString &message )
{
    return Diagnostic{ code, message, DiagnosticSeverity::Error };
}

QString roleKeyFor( AxisRole role )
{
    switch ( role )
    {
        case AxisRole::DatasetVersion:
            return QStringLiteral( "dataset_version" );
        case AxisRole::SplitManifest:
            return QStringLiteral( "split_manifest" );
        case AxisRole::Model:
            return QStringLiteral( "model" );
        case AxisRole::Seed:
            return QStringLiteral( "seed" );
        case AxisRole::Tag:
            return QStringLiteral( "tag" );
    }
    return QStringLiteral( "tag" );
}

/// Canonical serialization of one cell's assignments — the cellId basis.
QJsonObject cellIdentityJson( const QHash<QString, QString> &assignments )
{
    QJsonObject json;
    for ( auto it = assignments.constBegin(); it != assignments.constEnd(); ++it )
        json.insert( it.key(), it.value() );
    return json;
}

} // namespace

QString axisRoleToString( AxisRole role )
{
    return roleKeyFor( role );
}

std::optional<AxisRole> axisRoleFromString( const QString &text )
{
    if ( text == QStringLiteral( "dataset_version" ) )
        return AxisRole::DatasetVersion;
    if ( text == QStringLiteral( "split_manifest" ) )
        return AxisRole::SplitManifest;
    if ( text == QStringLiteral( "model" ) )
        return AxisRole::Model;
    if ( text == QStringLiteral( "seed" ) )
        return AxisRole::Seed;
    if ( text == QStringLiteral( "tag" ) )
        return AxisRole::Tag;
    return std::nullopt;
}

QJsonObject MatrixCell::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "cell_id" ), cellId );
    json.insert( QStringLiteral( "assignments" ), cellIdentityJson( assignments ) );
    json.insert( QStringLiteral( "pins" ), pins.toJson() );
    return json;
}

Result<MatrixCell> MatrixCell::fromJson( const QJsonObject &json )
{
    MatrixCell cell;
    cell.cellId = json.value( QStringLiteral( "cell_id" ) ).toString();
    const QJsonObject assignments = json.value( QStringLiteral( "assignments" ) ).toObject();
    for ( auto it = assignments.constBegin(); it != assignments.constEnd(); ++it )
        cell.assignments.insert( it.key(), it.value().toString() );
    const auto pins = RunPins::fromJson( json.value( QStringLiteral( "pins" ) ).toObject() );
    if ( !pins )
        return Result<MatrixCell>::failure( pins.diagnostics() );
    cell.pins = pins.value();
    if ( cell.cellId.isEmpty() )
        return Result<MatrixCell>::failure( matrixError(
            QStringLiteral( "experiment.matrix_invalid" ),
            QStringLiteral( "cell requires a cell id" ) ) );
    return Result<MatrixCell>::success( cell );
}

Result<void> MatrixDescriptor::validate() const
{
    if ( matrixId.isEmpty() )
        return Result<void>::failure( matrixError( QStringLiteral( "experiment.matrix_invalid" ),
                                                   QStringLiteral( "matrix requires an id" ) ) );
    if ( experimentId.isEmpty() )
        return Result<void>::failure( matrixError(
            QStringLiteral( "experiment.matrix_invalid" ),
            QStringLiteral( "matrix requires the target experiment id" ) ) );
    if ( workflowId.isEmpty() )
        return Result<void>::failure( matrixError(
            QStringLiteral( "experiment.matrix_invalid" ),
            QStringLiteral( "matrix requires the submitted workflow id" ) ) );
    if ( axes.isEmpty() )
        return Result<void>::failure( matrixError( QStringLiteral( "experiment.matrix_invalid" ),
                                                   QStringLiteral( "matrix requires at least one axis" ) ) );
    QSet<QString> axisNames;
    qint64 product = 1;
    for ( const MatrixAxis &axis : axes )
    {
        if ( axis.name.isEmpty() )
            return Result<void>::failure( matrixError(
                QStringLiteral( "experiment.matrix_invalid" ),
                QStringLiteral( "every axis requires a name" ) ) );
        if ( axisNames.contains( axis.name ) )
            return Result<void>::failure( matrixError(
                QStringLiteral( "experiment.matrix_invalid" ),
                QStringLiteral( "duplicate axis name %1" ).arg( axis.name ) ) );
        axisNames.insert( axis.name );
        if ( axis.values.isEmpty() )
            return Result<void>::failure( matrixError(
                QStringLiteral( "experiment.matrix_invalid" ),
                QStringLiteral( "axis %1 requires at least one value" ).arg( axis.name ) ) );
        QSet<QString> distinct;
        for ( const QString &value : axis.values )
        {
            if ( value.isEmpty() )
                return Result<void>::failure( matrixError(
                    QStringLiteral( "experiment.matrix_invalid" ),
                    QStringLiteral( "axis %1 has an empty value" ).arg( axis.name ) ) );
            if ( distinct.contains( value ) )
                return Result<void>::failure( matrixError(
                    QStringLiteral( "experiment.matrix_invalid" ),
                    QStringLiteral( "axis %1 has duplicate value %2" )
                        .arg( axis.name, value ) ) );
            distinct.insert( value );
        }
        product *= axis.values.size();
        if ( product > kMaxMatrixCells )
            return Result<void>::failure( matrixError(
                QStringLiteral( "experiment.matrix_too_large" ),
                QStringLiteral( "matrix product %1 exceeds the %2-cell bound" )
                    .arg( product )
                    .arg( kMaxMatrixCells ) ) );
    }
    return Result<void>::success();
}

Result<QVector<MatrixCell>> MatrixDescriptor::enumerateCells() const
{
    const auto validated = validate();
    if ( !validated )
        return Result<QVector<MatrixCell>>::failure( validated.diagnostics() );

    QVector<MatrixCell> cells;
    cells.append( MatrixCell{} ); // seed cell grows axis by axis
    for ( const MatrixAxis &axis : axes )
    {
        QVector<MatrixCell> expanded;
        expanded.reserve( cells.size() * axis.values.size() );
        for ( const MatrixCell &partial : cells )
        {
            for ( const QString &value : axis.values )
            {
                MatrixCell cell = partial;
                cell.assignments.insert( axis.name, value );
                switch ( axis.role )
                {
                    case AxisRole::DatasetVersion:
                        cell.pins.datasetVersionId = value;
                        break;
                    case AxisRole::SplitManifest:
                        cell.pins.splitManifestId = value;
                        break;
                    case AxisRole::Model:
                        cell.pins.modelId = value;
                        break;
                    case AxisRole::Seed:
                    {
                        bool ok = false;
                        const quint64 seed = value.toULongLong( &ok, 10 );
                        if ( !ok )
                            return Result<QVector<MatrixCell>>::failure( matrixError(
                                QStringLiteral( "experiment.matrix_invalid" ),
                                QStringLiteral( "seed axis %1 has non-integer value %2" )
                                    .arg( axis.name, value ) ) );
                        cell.pins.seed = seed;
                        cell.pins.hasSeed = true;
                        break;
                    }
                    case AxisRole::Tag:
                        break;
                }
                expanded.append( cell );
            }
        }
        cells = expanded;
    }

    // Cell identity last, once every assignment is in place: SHA-256 over
    // the canonical RFC-8785 form — "same cell" is content identity.
    for ( MatrixCell &cell : cells )
    {
        cell.cellId = QString::fromUtf8(
            QCryptographicHash::hash( sicnu::data::canonicalizeJsonRfc8785(
                                          cellIdentityJson( cell.assignments ) ),
                                      QCryptographicHash::Sha256 )
                .toHex() );
    }
    return Result<QVector<MatrixCell>>::success( cells );
}

QJsonObject MatrixDescriptor::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "matrix_id" ), matrixId );
    json.insert( QStringLiteral( "experiment_id" ), experimentId );
    json.insert( QStringLiteral( "workflow_id" ), workflowId );
    json.insert( QStringLiteral( "name" ), name );
    json.insert( QStringLiteral( "objective" ), objective );
    QJsonArray axisArray;
    for ( const MatrixAxis &axis : axes )
    {
        QJsonObject item;
        item.insert( QStringLiteral( "name" ), axis.name );
        item.insert( QStringLiteral( "role" ), axisRoleToString( axis.role ) );
        item.insert( QStringLiteral( "values" ),
                     QJsonArray::fromStringList(
                         QStringList( axis.values.cbegin(), axis.values.cend() ) ) );
        axisArray.append( item );
    }
    json.insert( QStringLiteral( "axes" ), axisArray );
    return json;
}

Result<MatrixDescriptor> MatrixDescriptor::fromJson( const QJsonObject &json )
{
    MatrixDescriptor descriptor;
    descriptor.matrixId = json.value( QStringLiteral( "matrix_id" ) ).toString();
    descriptor.experimentId = json.value( QStringLiteral( "experiment_id" ) ).toString();
    descriptor.workflowId = json.value( QStringLiteral( "workflow_id" ) ).toString();
    descriptor.name = json.value( QStringLiteral( "name" ) ).toString();
    descriptor.objective = json.value( QStringLiteral( "objective" ) ).toString();
    for ( const QJsonValue &value : json.value( QStringLiteral( "axes" ) ).toArray() )
    {
        const QJsonObject item = value.toObject();
        MatrixAxis axis;
        axis.name = item.value( QStringLiteral( "name" ) ).toString();
        const auto role = axisRoleFromString( item.value( QStringLiteral( "role" ) ).toString() );
        if ( !role )
            return Result<MatrixDescriptor>::failure( matrixError(
                QStringLiteral( "experiment.matrix_invalid" ),
                QStringLiteral( "axis %1 has an unknown role" ).arg( axis.name ) ) );
        axis.role = *role;
        for ( const QJsonValue &entry : item.value( QStringLiteral( "values" ) ).toArray() )
            axis.values.append( entry.toString() );
        descriptor.axes.append( axis );
    }
    return Result<MatrixDescriptor>::success( descriptor );
}

MatrixLedger::MatrixLedger( ExperimentStore &store )
    : m_store( store )
{
}

sicnu::data::Result<void> MatrixLedger::link( const QString &cellId, const QString &runId )
{
    if ( cellId.isEmpty() || runId.isEmpty() )
        return sicnu::data::Result<void>::failure( matrixError(
            QStringLiteral( "experiment.matrix_invalid" ),
            QStringLiteral( "ledger link requires cell id and run id" ) ) );
    return m_store.addLineageEdge( QStringLiteral( "matrix" ), cellId,
                                   QStringLiteral( "recorded" ),
                                   QStringLiteral( "run" ), runId );
}

QStringList MatrixLedger::runsForCell( const QString &cellId, qint64 limit ) const
{
    QStringList runIds;
    const auto edges = m_store.outgoingEdges( QStringLiteral( "matrix" ), cellId, limit );
    for ( const auto &edge : edges )
    {
        if ( edge.edgeKind == QStringLiteral( "recorded" ) &&
             edge.toKind == QStringLiteral( "run" ) )
            runIds.append( edge.toId );
    }
    return runIds;
}

QHash<QString, QStringList> MatrixLedger::ledgerForMatrix(
    const QVector<MatrixCell> &cells ) const
{
    QHash<QString, QStringList> ledger;
    for ( const MatrixCell &cell : cells )
        ledger.insert( cell.cellId, runsForCell( cell.cellId ) );
    return ledger;
}

QJsonObject MetricAggregate::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "run_count" ), runCount );
    json.insert( QStringLiteral( "mean" ), mean );
    json.insert( QStringLiteral( "population_std_dev" ), populationStdDev );
    json.insert( QStringLiteral( "min" ), min );
    json.insert( QStringLiteral( "max" ), max );
    return json;
}

MatrixAggregator::MatrixAggregator( ExperimentStore &store, MatrixLedger &ledger )
    : m_store( &store )
    , m_ledger( &ledger )
{
}

Result<MatrixAggregate> MatrixAggregator::aggregate(
    const MatrixDescriptor &descriptor, const QVector<QString> &metricNames ) const
{
    const auto cells = descriptor.enumerateCells();
    if ( !cells )
        return Result<MatrixAggregate>::failure( cells.diagnostics() );

    MatrixAggregate aggregate;
    aggregate.matrixId = descriptor.matrixId;
    aggregate.totalCells = cells.value().size();

    for ( const MatrixCell &cell : cells.value() )
    {
        CellAggregate cellAggregate;
        cellAggregate.cellId = cell.cellId;
        cellAggregate.assignments = cell.assignments;
        cellAggregate.runIds = m_ledger->runsForCell( cell.cellId );

        bool anyRecorded = false;
        bool anyFailed = false;
        // metric name → collected values
        QHash<QString, QVector<double>> samples;

        for ( const QString &runId : cellAggregate.runIds )
        {
            const auto run = m_store->runById( runId );
            if ( !run )
                continue; // dangling ledger edge: counted as neither recorded nor failed
            if ( run->status() == RunStatus::Completed )
            {
                anyRecorded = true;
                const auto record = m_store->metricRecordForRun( runId );
                if ( record.has_value() )
                {
                    for ( const QString &metric : metricNames )
                    {
                        if ( const auto value = metricValueAtPath( record->metrics, metric ) )
                            samples[metric].append( *value );
                    }
                }
            }
            else if ( run->status() == RunStatus::Failed ||
                      run->status() == RunStatus::Cancelled )
            {
                anyFailed = true;
            }
        }

        for ( auto it = samples.constBegin(); it != samples.constEnd(); ++it )
        {
            MetricAggregate stats;
            stats.runCount = it.value().size();
            double sum = 0.0;
            stats.min = it.value().first();
            stats.max = it.value().first();
            for ( const double value : it.value() )
            {
                sum += value;
                stats.min = std::min( stats.min, value );
                stats.max = std::max( stats.max, value );
            }
            stats.mean = sum / stats.runCount;
            double squared = 0.0;
            for ( const double value : it.value() )
                squared += ( value - stats.mean ) * ( value - stats.mean );
            stats.populationStdDev = std::sqrt( squared / stats.runCount );
            cellAggregate.metrics.insert( it.key(), stats );
        }

        cellAggregate.status = anyRecorded
                                   ? ( anyFailed ? QStringLiteral( "partial" )
                                                 : QStringLiteral( "recorded" ) )
                                   : ( anyFailed ? QStringLiteral( "failed" )
                                                 : QStringLiteral( "missing" ) );
        if ( cellAggregate.status == QStringLiteral( "recorded" ) ||
             cellAggregate.status == QStringLiteral( "partial" ) )
            ++aggregate.recordedCells;
        if ( cellAggregate.status == QStringLiteral( "missing" ) )
            ++aggregate.missingCells;
        if ( cellAggregate.status == QStringLiteral( "failed" ) )
            ++aggregate.failedCells;
        aggregate.cells.append( cellAggregate );
    }
    return Result<MatrixAggregate>::success( aggregate );
}

QStringList MatrixAggregator::paretoCellIds( const MatrixAggregate &aggregate,
                                             const QVector<QString> &maximizeMetrics )
{
    // Candidates: cells that recorded EVERY objective metric. A cell missing
    // one metric cannot be compared — and must never win by absence.
    struct Candidate
    {
        QString cellId;
        QVector<double> values;
    };
    QVector<Candidate> candidates;
    for ( const CellAggregate &cell : aggregate.cells )
    {
        Candidate candidate;
        candidate.cellId = cell.cellId;
        bool complete = true;
        for ( const QString &metric : maximizeMetrics )
        {
            const auto it = cell.metrics.constFind( metric );
            if ( it == cell.metrics.constEnd() || it->runCount == 0 )
            {
                complete = false;
                break;
            }
            candidate.values.append( it->mean );
        }
        if ( complete && !maximizeMetrics.isEmpty() )
            candidates.append( candidate );
    }

    QStringList pareto;
    for ( const Candidate &a : candidates )
    {
        bool dominated = false;
        for ( const Candidate &b : candidates )
        {
            if ( a.cellId == b.cellId )
                continue;
            bool bAtLeastAsGood = true;
            bool bStrictlyBetter = false;
            for ( int i = 0; i < a.values.size(); ++i )
            {
                if ( b.values.at( i ) < a.values.at( i ) )
                    bAtLeastAsGood = false;
                if ( b.values.at( i ) > a.values.at( i ) )
                    bStrictlyBetter = true;
            }
            if ( bAtLeastAsGood && bStrictlyBetter )
            {
                dominated = true;
                break;
            }
        }
        if ( !dominated )
            pareto.append( a.cellId );
    }
    std::sort( pareto.begin(), pareto.end() );
    return pareto;
}

} // namespace sicnu::experiment
