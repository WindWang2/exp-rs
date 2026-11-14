// run_matrix_metric.cpp — see run_matrix_metric.h for the contract.
#include "experiment_studio/run_matrix_metric.h"

#include <QJsonArray>
#include <QJsonValue>

#include <algorithm>
#include <cmath>

namespace sicnu::experiment_studio
{

const char *runMetricCellKindName( RunMetricCellKind kind )
{
    switch ( kind )
    {
        case RunMetricCellKind::Value: return "value";
        case RunMetricCellKind::MissingKey: return "missing_metric";
        case RunMetricCellKind::Null: return "null";
        case RunMetricCellKind::Object: return "object";
        case RunMetricCellKind::Array: return "array";
        case RunMetricCellKind::String: return "string";
        case RunMetricCellKind::Bool: return "bool";
        case RunMetricCellKind::Undefined: return "undefined";
        case RunMetricCellKind::NonFinite: return "non_finite";
    }
    return "unknown";
}

QJsonObject RunMetricCell::toJson() const
{
    QJsonObject o;
    o.insert( QStringLiteral( "metric" ), metricName );
    o.insert( QStringLiteral( "run_id" ), runId );
    if ( !unit.isEmpty() ) o.insert( QStringLiteral( "unit" ), unit );
    if ( !protocol.isEmpty() ) o.insert( QStringLiteral( "protocol" ), protocol );
    o.insert( QStringLiteral( "kind" ), QString::fromUtf8( runMetricCellKindName( kind ) ) );
    o.insert( QStringLiteral( "display" ), display );
    o.insert( QStringLiteral( "reason" ), reason );
    if ( kind == RunMetricCellKind::Value )
        o.insert( QStringLiteral( "value" ), value );
    return o;
}

RunMetricCell projectRunMetricCell( const QJsonObject &rowMetrics,
                                    const QString &metricName,
                                    const QString &runId,
                                    const QString &unit,
                                    const QString &protocol )
{
    RunMetricCell cell;
    cell.metricName = metricName;
    cell.runId = runId;
    cell.unit = unit;
    cell.protocol = protocol;

    const QString withUnit = unit.isEmpty() ? QString() : QStringLiteral( " %1" ).arg( unit );
    const QJsonValue value = rowMetrics.value( metricName );

    if ( value.isUndefined() )
    {
        cell.kind = RunMetricCellKind::MissingKey;
        cell.reason = QStringLiteral( "experiment_studio.metric_missing:%1" ).arg( metricName );
        cell.display = QStringLiteral( "— (metric not recorded)" );
        return cell;
    }
    if ( value.isNull() )
    {
        cell.kind = RunMetricCellKind::Null;
        cell.reason = QStringLiteral( "experiment_studio.metric_null:%1" ).arg( metricName );
        cell.display = QStringLiteral( "— (null)" );
        return cell;
    }
    if ( value.isObject() )
    {
        cell.kind = RunMetricCellKind::Object;
        cell.reason = QStringLiteral( "experiment_studio.metric_structured:%1" ).arg( metricName );
        cell.display = QStringLiteral( "— (per-class document)" );
        return cell;
    }
    if ( value.isArray() )
    {
        cell.kind = RunMetricCellKind::Array;
        cell.reason = QStringLiteral( "experiment_studio.metric_array:%1" ).arg( metricName );
        cell.display = QStringLiteral( "— (array)" );
        return cell;
    }
    if ( value.isBool() )
    {
        cell.kind = RunMetricCellKind::Bool;
        cell.reason = QStringLiteral( "experiment_studio.metric_bool:%1" ).arg( metricName );
        cell.display = QStringLiteral( "— (boolean)" );
        return cell;
    }
    if ( value.isString() )
    {
        cell.kind = RunMetricCellKind::String;
        cell.reason = QStringLiteral( "experiment_studio.metric_string:%1" ).arg( metricName );
        cell.display = QStringLiteral( "— (non-numeric)" );
        return cell;
    }
    if ( !value.isDouble() )
    {
        cell.kind = RunMetricCellKind::Undefined;
        cell.reason = QStringLiteral( "experiment_studio.metric_undefined:%1" ).arg( metricName );
        cell.display = QStringLiteral( "— (undefined)" );
        return cell;
    }

    const double d = value.toDouble();
    if ( !std::isfinite( d ) )
    {
        cell.kind = RunMetricCellKind::NonFinite;
        cell.reason = QStringLiteral( "experiment_studio.metric_non_finite:%1" ).arg( metricName );
        cell.display = QStringLiteral( "— (non-finite)" );
        return cell;
    }
    cell.kind = RunMetricCellKind::Value;
    cell.value = d;
    cell.reason.clear();
    cell.display = QString::number( d, 'g', 12 ) + withUnit;
    return cell;
}

QJsonObject RunMatrixPage::toJson() const
{
    QJsonObject o;
    o.insert( QStringLiteral( "page" ), page );
    o.insert( QStringLiteral( "page_size" ), pageSize );
    o.insert( QStringLiteral( "total_rows" ), totalRows );
    o.insert( QStringLiteral( "page_count" ), pageCount );
    o.insert( QStringLiteral( "first_row" ), firstRow );
    o.insert( QStringLiteral( "visible_rows" ), static_cast<int>( rows.size() ) );
    QJsonArray ids;
    for ( const auto &row : rows )
        ids.append( pointIdentityKey( row ) );
    o.insert( QStringLiteral( "visible_keys" ), ids );
    QJsonArray sel;
    for ( const QString &key : selectedKeys )
        sel.append( key );
    o.insert( QStringLiteral( "selected_keys" ), sel );
    QJsonArray iss;
    for ( const QString &issue : issues )
        iss.append( issue );
    o.insert( QStringLiteral( "issues" ), iss );
    return o;
}

RunMatrixPage projectRunMatrixPage( const RunMatrixViewModel &vm,
                                    int requestedPage,
                                    int pageSize,
                                    const QStringList &stableSelection )
{
    RunMatrixPage result;
    result.pageSize = std::clamp( pageSize, 1, 500 );
    result.totalRows = static_cast<int>( vm.rows.size() );
    result.pageCount = std::max( 1, ( result.totalRows + result.pageSize - 1 ) / result.pageSize );
    result.page = std::clamp( requestedPage, 0, result.pageCount - 1 );
    result.firstRow = result.page * result.pageSize;
    const int last = std::min( result.totalRows, result.firstRow + result.pageSize );
    for ( int i = result.firstRow; i < last; ++i )
        result.rows.append( vm.rows.at( i ) );

    // Selection memory is NOT trimmed to the visible page: an off-page pin
    // stays selected and re-resolves when its page comes back.
    result.selectedKeys = stableSelection;
    result.selectedKeys.removeDuplicates();
    for ( const QString &issue : vm.issues )
        result.issues.append( issue );
    if ( result.totalRows == 0 )
        result.issues.append( QStringLiteral( "experiment_studio.matrix_no_rows" ) );
    return result;
}

} // namespace sicnu::experiment_studio
