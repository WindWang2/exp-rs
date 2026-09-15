// rs_edit_index.cpp — see rs_edit_index.h.
#include "rs_edit_index.h"

#include <qgsfeature.h>
#include <qgsvectorlayer.h>

#include <QtGlobal>

namespace
{
QgsRectangle geometryBounds( const QgsGeometry &geom )
{
    return geom.isNull() ? QgsRectangle() : geom.boundingBox();
}
} // namespace

RsEditIndex::RsEditIndex( QObject *parent )
  : QObject( parent )
{
}

RsEditIndex::~RsEditIndex()
{
    detach();
}

qlonglong RsEditIndex::readMaxFeaturesEnv()
{
    const QByteArray raw = qgetenv( "RS_EDIT_INDEX_MAX_FEATURES" );
    bool ok = false;
    const qlonglong v = raw.toLongLong( &ok );
    if ( ok && v > 0 )
        return v;
    return kDefaultMaxFeatures;
}

bool RsEditIndex::attach( QgsVectorLayer *layer, QString *error,
                          const std::function<bool()> &isCanceled,
                          qlonglong maxFeatures )
{
    auto fail = [error]( const QString & msg )
    {
        if ( error )
            *error = msg;
        return false;
    };
    if ( mLayer )
        return fail( QStringLiteral( "index already attached to a layer" ) );
    if ( !layer || !layer->isValid() )
        return fail( QStringLiteral( "invalid layer" ) );
    if ( maxFeatures <= 0 )
        maxFeatures = kDefaultMaxFeatures;

    const qlonglong count = layer->featureCount();
    if ( count > maxFeatures )
    {
        return fail( QStringLiteral( "layer has %1 features, above the index cap %2 "
                                     "(raise RS_EDIT_INDEX_MAX_FEATURES to opt in)" )
                     .arg( count ).arg( maxFeatures ) );
    }

    mIndex = QgsSpatialIndex();
    mBoxes.clear();
    mBoxes.reserve( static_cast<int>( std::min<qlonglong>( count, 1 << 26 ) ) );
    mIndexSize = 0;

    QgsFeature f;
    QgsFeatureIterator it = layer->getFeatures();
    while ( it.nextFeature( f ) )
    {
        if ( isCanceled && isCanceled() )
        {
            mIndex = QgsSpatialIndex();
            mBoxes.clear();
            mIndexSize = 0;
            return fail( QStringLiteral( "canceled" ) );
        }
        if ( !f.hasGeometry() )
            continue;
        const QgsRectangle box = geometryBounds( f.geometry() );
        mIndex.addFeature( f.id(), box );
        mBoxes.insert( f.id(), box );
        ++mIndexSize;
    }

    mLayer = layer;
    mConnections << connect( layer, &QgsVectorLayer::featureAdded, this,
                             &RsEditIndex::onFeatureAdded );
    mConnections << connect( layer, &QgsVectorLayer::featureDeleted, this,
                             &RsEditIndex::onFeatureDeleted );
    mConnections << connect( layer, &QgsVectorLayer::geometryChanged, this,
                             &RsEditIndex::onGeometryChanged );
    emit changed();
    return true;
}

void RsEditIndex::detach()
{
    for ( const QMetaObject::Connection &c : mConnections )
        disconnect( c );
    mConnections.clear();
    mIndex = QgsSpatialIndex();
    mBoxes.clear();
    mIndexSize = 0;
    mLayer.clear();
}

void RsEditIndex::onFeatureAdded( QgsFeatureId fid )
{
    if ( !mLayer )
        return;
    if ( mBoxes.contains( fid ) )
        return; // undo/redo re-notify of a tracked id: nothing to do
    QgsFeature f = mLayer->getFeature( fid );
    if ( !f.hasGeometry() )
        return;
    const QgsRectangle box = geometryBounds( f.geometry() );
    mIndex.addFeature( fid, box );
    mBoxes.insert( fid, box );
    ++mIndexSize;
    emit changed();
}

void RsEditIndex::onFeatureDeleted( QgsFeatureId fid )
{
    const auto it = mBoxes.constFind( fid );
    if ( it == mBoxes.constEnd() )
        return;
    mIndex.deleteFeature( fid, *it );
    mBoxes.erase( it );
    --mIndexSize;
    emit changed();
}

void RsEditIndex::onGeometryChanged( QgsFeatureId fid, const QgsGeometry &geometry )
{
    auto it = mBoxes.find( fid );
    if ( it == mBoxes.constEnd() )
    {
        onFeatureAdded( fid );
        return;
    }
    mIndex.deleteFeature( fid, *it );
    const QgsRectangle newBox = geometryBounds( geometry );
    mIndex.addFeature( fid, newBox );
    *it = newBox;
    emit changed();
}

QList<QgsFeatureId> RsEditIndex::intersects( const QgsRectangle &rect ) const
{
    return mIndex.intersects( rect );
}

QList<QgsFeatureId> RsEditIndex::nearest( const QgsPointXY &point, int k ) const
{
    if ( k <= 0 || mIndexSize == 0 )
        return {};
    return mIndex.nearestNeighbor( point, k );
}

QgsRectangle RsEditIndex::bounds() const
{
    QgsRectangle unionRect;
    for ( auto it = mBoxes.constBegin(); it != mBoxes.constEnd(); ++it )
    {
        if ( unionRect.isNull() )
            unionRect = *it;
        else
            unionRect.combineExtentWith( *it );
    }
    return unionRect;
}

QgsRectangle RsEditIndex::selectionBounds() const
{
    QgsVectorLayer *layer = mLayer.data();
    if ( !layer )
        return QgsRectangle();
    QgsRectangle unionRect;
    const QgsFeatureIds selection = layer->selectedFeatureIds();
    for ( QgsFeatureId fid : selection )
    {
        const auto it = mBoxes.constFind( fid );
        if ( it == mBoxes.constEnd() )
            continue;
        if ( unionRect.isNull() )
            unionRect = *it;
        else
            unionRect.combineExtentWith( *it );
    }
    return unionRect;
}
