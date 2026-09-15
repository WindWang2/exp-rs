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

    // ── Linked-visual 11.0: optional per-point raster geometry ────────
    // Parallel to xs/ys; empty (or hasGeometry false) when the producer
    // does not carry geometry. Consumers (brush filters!) MUST keep these
    // arrays consistent with xs/ys. With them a picked point resolves to a
    // map location through geotransform arithmetic — no re-scan, no I/O.
    QVector<qint64> cols;      ///< full-resolution pixel column per point
    QVector<qint64> rows;      ///< full-resolution pixel row per point
    bool hasGeometry = false;
    QVector<double> geotransform; ///< GDAL order, size 6 when hasGeometry
    QString crsWkt;               ///< raster CRS when hasGeometry
    QString sourcePath;           ///< provenance: sampled raster path
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

/// Pure brushing helper (11.0): compacts a Scatter-kind payload to the points
/// with @p x0 <= x <= @p x1, keeping the parallel geometry arrays (cols/rows)
/// consistent with xs/ys. Non-scatter payloads are returned unchanged. No
/// rescan, no I/O — callers apply this to their bounded snapshot.
inline VaData filterScatterByXRange( const VaData &scatterData, double x0, double x1 )
{
    if ( scatterData.kind != VaChartKind::Scatter )
        return scatterData;
    VaData filtered = scatterData;
    filtered.scatter.xs.clear();
    filtered.scatter.ys.clear();
    filtered.scatter.groups.clear();
    filtered.scatter.cols.clear();
    filtered.scatter.rows.clear();
    const VaScatter &source = scatterData.scatter;
    for ( int i = 0; i < source.xs.size(); ++i )
    {
        const double v = source.xs.at( i );
        if ( v < x0 || v > x1 )
            continue;
        filtered.scatter.xs.append( v );
        filtered.scatter.ys.append( source.ys.at( i ) );
        if ( i < source.groups.size() )
            filtered.scatter.groups.append( source.groups.at( i ) );
        if ( i < source.cols.size() )
            filtered.scatter.cols.append( source.cols.at( i ) );
        if ( i < source.rows.size() )
            filtered.scatter.rows.append( source.rows.at( i ) );
    }
    return filtered;
}

/// Pure pick helper (11.0): resolves a chart pick index against the payload
/// the widget is CURRENTLY SHOWING (post-filter) to a raster-CRS map point
/// via the payload's own geotransform. False when the index or geometry is
/// missing — never guesses. Pointers may be null.
inline bool scatterPickToMapPoint( const VaScatter &displayed, int index,
                                   double *rx, double *ry )
{
    if ( index < 0 || !displayed.hasGeometry || displayed.geotransform.size() != 6 )
        return false;
    if ( index >= displayed.cols.size() || index >= displayed.rows.size() )
        return false;
    const QVector<double> &gt = displayed.geotransform;
    const qint64 col = displayed.cols.at( index );
    const qint64 row = displayed.rows.at( index );
    if ( rx )
        *rx = gt.at( 0 ) + col * gt.at( 1 ) + row * gt.at( 2 );
    if ( ry )
        *ry = gt.at( 3 ) + col * gt.at( 4 ) + row * gt.at( 5 );
    return true;
}

} // namespace sicnu::app::va
