// vector_optimizer.h — Vector processing optimization utilities
#pragma once

#include <qgsspatialindex.h>
#include <qgsfeatureiterator.h>
#include <qgsfeature.h>
#include <qgsgeometry.h>
#include <qgsrectangle.h>
#include <qgsvectorlayer.h>
#include <QMutex>
#include <QCache>

/**
 * Vector processing optimizer with spatial index caching.
 * Provides optimized spatial queries and batch operations.
 */
class VectorOptimizer
{
public:
    /**
     * Get or build spatial index for a vector layer.
     * Index is cached and reused until the layer is modified.
     */
    static QgsSpatialIndex *getSpatialIndex(QgsVectorLayer *layer);

    /**
     * Invalidate cached spatial index for a layer.
     * Call when layer features are modified.
     */
    static void invalidateIndex(QgsVectorLayer *layer);

    /**
     * Optimized intersection query using spatial index.
     * Returns feature IDs that intersect with the given geometry.
     */
    static QgsFeatureIds intersects(QgsVectorLayer *layer, const QgsGeometry &geometry);

    /**
     * Optimized contains query using spatial index.
     * Returns feature IDs that contain the given geometry.
     */
    static QgsFeatureIds contains(QgsVectorLayer *layer, const QgsGeometry &geometry);

    /**
     * Optimized nearest neighbor query using spatial index.
     * Returns the N nearest feature IDs to the given point.
     */
    static QgsFeatureIds nearestNeighbors(QgsVectorLayer *layer, const QgsPointXY &point, int count = 1);

    /**
     * Batch feature retrieval with spatial filter.
     * Returns features that match the spatial filter.
     */
    static QList<QgsFeature> getFeaturesInRegion(QgsVectorLayer *layer, const QgsRectangle &region);

private:
    static QMutex s_mutex;
    static QCache<QString, QgsSpatialIndex> s_indexCache;
};
