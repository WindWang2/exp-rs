// wkt.h — minimal WKT geometry reader for audit predicates (ADR 0136).
//
// The dataset layer needs just enough geometry to answer spatial questions:
// points, and (multi)polygon rings without interior holes, in 2D. Full GEOS
// power stays in the app/analysis layers; anything this parser cannot read
// is a typed validation failure, never a guessed shape.
#pragma once

#include "../data/data_result.h"

#include <QPointF>
#include <QString>
#include <QVector>

namespace sicnu::dataset
{

/// A polygon as one exterior ring (no holes). Audits only need areas and
/// containment against simple exteriors; holes are a validation warning at
/// the label-QA layer, not a silent ignore here.
struct SimplePolygon
{
    QVector<QPointF> ring; ///< closed or open ring; closed on read

    bool isValid() const { return ring.size() >= 4; }
    double minX() const;
    double minY() const;
    double maxX() const;
    double maxY() const;
    /// Point-in-polygon (ray casting) on the exterior ring.
    bool contains( const QPointF &point ) const;
    bool intersects( const SimplePolygon &other ) const;
};

struct WktPoint
{
    double x = 0.0;
    double y = 0.0;
};

/// Parses "POINT(x y)". Fails with `dataset.wkt_invalid` otherwise.
sicnu::data::Result<WktPoint> parseWktPoint( const QString &wkt );

/// Parses "POLYGON((x y, x y, ...))" (one exterior ring) or the first
/// member of "MULTIPOLYGON(((...)))" — multi-rings are reported via
/// @p ringCountOut so callers can flag holes/multiparts upstream.
sicnu::data::Result<SimplePolygon> parseWktPolygon( const QString &wkt,
                                                    int *ringCountOut = nullptr );

/// Axis-aligned bounding box of any (multi)polygon WKT.
sicnu::data::Result<QVector<double>> parseWktBounds( const QString &wkt ); // [minX,minY,maxX,maxY]

} // namespace sicnu::dataset
