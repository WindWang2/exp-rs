// vector_optimizer.cpp — Vector processing optimization implementation
#include "vector_optimizer.h"
#include "core/sicnu_logging.h"

#include <qgsfeaturerequest.h>

QMutex VectorOptimizer::s_mutex;
QCache<QString, QgsSpatialIndex> VectorOptimizer::s_indexCache;

QgsSpatialIndex *VectorOptimizer::getSpatialIndex(QgsVectorLayer *layer)
{
    if (!layer || !layer->isValid())
        return nullptr;

    QString key = layer->id();

    // Check cache first (with lock)
    {
        QMutexLocker locker(&s_mutex);
        QgsSpatialIndex *index = s_indexCache.object(key);
        if (index)
            return index;
    }

    // Build index WITHOUT holding the lock (expensive operation)
    SICNU_LOG_INFO(SicnuLogTags::Algorithms,
                   QString("Building spatial index for layer: %1").arg(layer->name()));

    QgsSpatialIndex *index = new QgsSpatialIndex();
    QgsFeatureIterator iter = layer->getFeatures(QgsFeatureRequest().setNoAttributes());
    QgsFeature feat;
    int count = 0;
    while (iter.nextFeature(feat)) {
        if (feat.hasGeometry()) {
            index->addFeature(feat);
            count++;
        }
    }

    SICNU_LOG_INFO(SicnuLogTags::Algorithms,
                   QString("Spatial index built: %1 features indexed").arg(count));

    // Insert into cache (with lock), check for race condition
    QMutexLocker locker(&s_mutex);
    QgsSpatialIndex *existing = s_indexCache.object(key);
    if (existing) {
        // Another thread built it first
        delete index;
        return existing;
    }
    s_indexCache.insert(key, index);
    return index;
}

void VectorOptimizer::invalidateIndex(QgsVectorLayer *layer)
{
    if (!layer) return;

    QMutexLocker locker(&s_mutex);
    s_indexCache.remove(layer->id());
}

QgsFeatureIds VectorOptimizer::intersects(QgsVectorLayer *layer, const QgsGeometry &geometry)
{
    QgsFeatureIds result;

    QgsSpatialIndex *index = getSpatialIndex(layer);
    if (!index)
        return result;

    // Use spatial index for candidate selection
    QList<QgsFeatureId> candidatesList = index->intersects(geometry.boundingBox());
    QgsFeatureIds candidates(candidatesList.begin(), candidatesList.end());

    // Verify with exact geometry intersection
    QgsFeatureIterator iter = layer->getFeatures(QgsFeatureRequest()
        .setFilterFids(candidates)
        .setNoAttributes());

    QgsFeature feat;
    while (iter.nextFeature(feat)) {
        if (feat.hasGeometry() && feat.geometry().intersects(geometry)) {
            result.insert(feat.id());
        }
    }

    return result;
}

QgsFeatureIds VectorOptimizer::contains(QgsVectorLayer *layer, const QgsGeometry &geometry)
{
    QgsFeatureIds result;

    QgsSpatialIndex *index = getSpatialIndex(layer);
    if (!index)
        return result;

    // Use spatial index for candidate selection
    QList<QgsFeatureId> candidatesList = index->intersects(geometry.boundingBox());
    QgsFeatureIds candidates(candidatesList.begin(), candidatesList.end());

    // Verify with exact geometry containment
    QgsFeatureIterator iter = layer->getFeatures(QgsFeatureRequest()
        .setFilterFids(candidates)
        .setNoAttributes());

    QgsFeature feat;
    while (iter.nextFeature(feat)) {
        if (feat.hasGeometry() && feat.geometry().contains(geometry)) {
            result.insert(feat.id());
        }
    }

    return result;
}

QgsFeatureIds VectorOptimizer::nearestNeighbors(QgsVectorLayer *layer, const QgsPointXY &point, int count)
{
    QgsFeatureIds result;

    QgsSpatialIndex *index = getSpatialIndex(layer);
    if (!index)
        return result;

    // Use spatial index for nearest neighbor
    QList<QgsFeatureId> nearest = index->nearestNeighbor(point, count);
    return QgsFeatureIds(nearest.begin(), nearest.end());
}

QList<QgsFeature> VectorOptimizer::getFeaturesInRegion(QgsVectorLayer *layer, const QgsRectangle &region)
{
    QList<QgsFeature> result;

    QgsSpatialIndex *index = getSpatialIndex(layer);
    if (!index)
        return result;

    // Use spatial index for candidate selection
    QList<QgsFeatureId> candidatesList = index->intersects(region);
    QgsFeatureIds candidates(candidatesList.begin(), candidatesList.end());

    // Retrieve full features
    QgsFeatureIterator iter = layer->getFeatures(QgsFeatureRequest()
        .setFilterFids(candidates));

    QgsFeature feat;
    while (iter.nextFeature(feat)) {
        result.append(feat);
    }

    return result;
}
