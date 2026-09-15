// rs_edit_index.h — F11 Package E: maintained spatial index over an
// editable layer (large-editing support).
//
// Contract:
//   * attach() bulk-builds a QgsSpatialIndex once, then keeps it consistent
//     INCREMENTALLY from the layer's featureAdded/featureDeleted/
//     geometryChanged signals — queries never trigger a full scan or a UI
//     rebuild (Oracle O3).
//   * attach refuses layers above maxFeatures (default kDefaultMaxFeatures,
//     overridable via RS_EDIT_INDEX_MAX_FEATURES env for opt-in scale runs)
//     — fail-closed instead of silently index-pooring a huge layer.
//   * rectangles per feature are cached so undo/redo-driven geometryChanged
//     events can remove the OLD footprint from the index.
//   * selectionBounds() is the union of cached footprints of the layer's
//     current selection (empty rectangle when nothing is selected).
#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>

#include <qgsfeatureid.h>
#include <qgsrectangle.h>
#include <qgsspatialindex.h>
#include <qgsvectorlayer.h>

class RsEditIndex : public QObject
{
    Q_OBJECT

  public:
    /// Logical default scale gate (D10): the unit tests build 100k features;
    /// higher scales are an explicit env opt-in.
    static constexpr qlonglong kDefaultMaxFeatures = 100'000;

    explicit RsEditIndex( QObject *parent = nullptr );
    ~RsEditIndex() override;

    /// Attach and bulk-build. \a error is set on refusal (already attached,
    /// invalid layer, feature count above the cap, or canceled build).
    bool attach( QgsVectorLayer *layer, QString *error = nullptr,
                 const std::function<bool()> &isCanceled = {},
                 qlonglong maxFeatures = readMaxFeaturesEnv() );
    void detach();

    bool isAttached() const { return !mLayer.isNull(); }
    QgsVectorLayer *layer() const { return mLayer.data(); }
    qlonglong size() const { return mIndexSize; }

    /// Feature ids whose cached footprint intersects \a rect.
    QList<QgsFeatureId> intersects( const QgsRectangle &rect ) const;
    /// Up to \a k nearest ids to \a point (distance between rectangles and
    /// the point; independent brute-force oracles verify test cases).
    QList<QgsFeatureId> nearest( const QgsPointXY &point, int k ) const;

    /// Union of footprints of the layer's current selection.
    QgsRectangle selectionBounds() const;
    /// Union footprint of everything indexed.
    QgsRectangle bounds() const;

    /// Env override for the attach cap; invalid/absent → kDefaultMaxFeatures.
    static qlonglong readMaxFeaturesEnv();

  signals:
    void changed();

  private:
    void onFeatureAdded( QgsFeatureId fid );
    void onFeatureDeleted( QgsFeatureId fid );
    void onGeometryChanged( QgsFeatureId fid, const QgsGeometry &geometry );
    void rebuildBoundsCache();

    QPointer<QgsVectorLayer> mLayer;
    QgsSpatialIndex mIndex;
    QHash<QgsFeatureId, QgsRectangle> mBoxes;
    qlonglong mIndexSize = 0;
    QList<QMetaObject::Connection> mConnections;
};
