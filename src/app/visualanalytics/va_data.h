/***************************************************************************
 * va_data.h — Workbench 10.0 Visual Analytics typed payloads
 *
 * Pure value types flowing between VA sources (bounded sampling jobs) and
 * chart hosts. They carry NO layer/GDAL handles: a payload is a finished,
 * small projection (bounded bins/points/boxes/cells), safe to marshal
 * across threads and to export as JSON/CSV. Boundedness is part of the
 * contract — every producer documents (and enforces) its caps, and every
 * payload reports `truncated` truthfully when a cap bit.
 ***************************************************************************/
#pragma once

#include <QString>
#include <QVector>

namespace sicnu::app::va
{

/// Chart families the platform hosts. One value type per family; a host
/// switches on kind.
enum class VaChartKind
{
    Histogram,
    Series,  ///< spectral curves, temporal curves, profiles
    Scatter,
    BoxPlot,
    Matrix,  ///< confusion / transition matrices (row-major cells)
    Areas,   ///< class-area bars
};

struct VaHistogram
{
    QVector<double> binEdges;  ///< size = bins + 1 (monotone ascending)
    QVector<qint64> counts;    ///< size = bins
    qint64 validCount = 0;
    qint64 noDataCount = 0;
    double min = 0;
    double max = 0;
    double mean = 0;
    double stddev = 0;
    QString xLabel = QStringLiteral( "value" );
    QString yLabel = QStringLiteral( "count" );
    bool truncated = false;    ///< true when sampling skipped pixels
};

struct VaSeries
{
    QVector<double> xs;
    QVector<double> ys;
    QString name;
    QString xLabel = QStringLiteral( "x" );
    QString yLabel = QStringLiteral( "y" );
};

struct VaScatter
{
    QVector<double> xs;
    QVector<double> ys;
    QVector<int> groups;       ///< index into groupNames (-1 = ungrouped)
    QStringList groupNames;
    QString xLabel = QStringLiteral( "x" );
    QString yLabel = QStringLiteral( "y" );
    bool truncated = false;
};

struct VaBox
{
    QString label;
    double q0 = 0, q1 = 0, q2 = 0, q3 = 0, q4 = 0; ///< min, quartiles, max
    QVector<double> outliers;
};

struct VaBoxPlot
{
    QVector<VaBox> boxes;
    QString yLabel = QStringLiteral( "value" );
};

struct VaMatrix
{
    QStringList rowLabels;
    QStringList colLabels;
    QVector<qint64> cells;     ///< row-major, rows*cols
    QString caption;
    qint64 total() const
    {
        qint64 sum = 0;
        for ( qint64 v : cells )
            sum += v;
        return sum;
    }
};

struct VaAreas
{
    QStringList labels;
    QVector<qint64> values;
    QString caption;
};

/// The tagged payload one source produces. Exactly one member is meaningful,
/// named by `kind` (the rest are default-empty).
struct VaData
{
    VaChartKind kind = VaChartKind::Histogram;
    VaHistogram histogram;
    VaSeries series;
    VaScatter scatter;
    VaBoxPlot boxPlot;
    VaMatrix matrix;
    VaAreas areas;
};

} // namespace sicnu::app::va
