/***************************************************************************
  qgslayertreemapcanvasbridge.cpp
  --------------------------------------
  Date                 : May 2014
  Copyright            : (C) 2014 by Martin Dobias
  Email                : wonder dot sk at gmail dot com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgslayertreemapcanvasbridge.h"

#include "qgsgui.h"
#include "qgslayertree.h"
#include "qgslayertreegroup.h"
#include "qgslayertreeutils.h"
#include "qgsmapcanvas.h"
#include "qgsmaplayer.h"
#include "qgsmapoverviewcanvas.h"
#include "qgsproject.h"
#include "qgssettings.h"
#include "qgssettingsentryimpl.h"
#include "qgssettingsregistrycore.h"
#include "qgsvectorlayer.h"

#include <QPointer>
#include <QSet>
#include <QString>

#include "moc_qgslayertreemapcanvasbridge.cpp"

using namespace Qt::StringLiterals;

QgsLayerTreeMapCanvasBridge::QgsLayerTreeMapCanvasBridge( QgsLayerTree *root, QgsMapCanvas *canvas, QObject *parent )
  : QObject( parent )
  , mRoot( root )
  , mCanvas( canvas )
  , mHasLayersLoaded( !root->findLayers().isEmpty() )
{
  connect( root, &QgsLayerTreeGroup::customPropertyChanged, this, &QgsLayerTreeMapCanvasBridge::nodeCustomPropertyChanged );
  connect( root, &QgsLayerTreeNode::visibilityChanged, this, &QgsLayerTreeMapCanvasBridge::nodeVisibilityChanged );
  connect( root, &QgsLayerTree::layerOrderChanged, this, &QgsLayerTreeMapCanvasBridge::deferredSetCanvasLayers );

  connect( QgsProject::instance(), &QgsProject::layersAdded, this, &QgsLayerTreeMapCanvasBridge::layersAdded );

  // The project (its layer store) destroys removed layers but never touches
  // the layer tree: a node left behind keeps serving the dead layer's id and
  // the canvas keeps the ghost layer. Drop the nodes and refresh the canvas
  // here, before the layer objects are destroyed (id-based overload only —
  // the layer pointers are about to die).
  connect( QgsProject::instance(), qOverload<const QStringList &>( &QgsProject::layersWillBeRemoved ),
            this, &QgsLayerTreeMapCanvasBridge::layersWillBeRemoved );

  setCanvasLayers();
}

void QgsLayerTreeMapCanvasBridge::setCanvasLayers()
{
  if ( !mRoot || !mCanvas )
    return;

  QList<QgsMapLayer *> canvasLayers, overviewLayers, allLayerOrder;

  if ( mRoot->hasCustomLayerOrder() )
  {
    const QList<QgsMapLayer *> customOrderLayers = mRoot->customLayerOrder();
    for ( const QgsMapLayer *layer : customOrderLayers )
    {
      QgsLayerTreeLayer *nodeLayer = mRoot->findLayer( layer->id() );
      if ( nodeLayer && nodeLayer->layer() )
      {
        if ( !nodeLayer->layer()->isSpatial() )
          continue;

        allLayerOrder << nodeLayer->layer();
        if ( nodeLayer->isVisible() )
          canvasLayers << nodeLayer->layer();
        if ( nodeLayer->customProperty( u"overview"_s, 0 ).toInt() )
          overviewLayers << nodeLayer->layer();
      }
    }
  }
  else
  {
    setCanvasLayers( mRoot, canvasLayers, overviewLayers, allLayerOrder );
  }

  const QList<QgsLayerTreeLayer *> layerNodes = mRoot->findLayers();
  int currentSpatialLayerCount = 0;
  int currentValidSpatialLayerCount = 0;
  for ( QgsLayerTreeLayer *layerNode : layerNodes )
  {
    if ( layerNode->layer() && layerNode->layer()->isSpatial() )
    {
      currentSpatialLayerCount++;
      if ( layerNode->layer()->isValid() )
        currentValidSpatialLayerCount++;
    }
  }

  const bool firstLayers = mAutoSetupOnFirstLayer && !mHasLayersLoaded && currentSpatialLayerCount != 0;
  const bool firstValidLayers = mAutoSetupOnFirstLayer && !mHasValidLayersLoaded && currentValidSpatialLayerCount != 0;

  mCanvas->setLayers( canvasLayers );
  if ( mOverviewCanvas )
    mOverviewCanvas->setLayers( overviewLayers );

  if ( firstValidLayers )
  {
    // if we are moving from zero to non-zero layers, let's zoom to those data (only consider valid layers here!)
    mCanvas->zoomToProjectExtent();
  }

  if ( !mFirstCRS.isValid() )
  {
    // find out what is the first used CRS in case we may need to turn on OTF projections later
    for ( const QgsLayerTreeLayer *layerNode : layerNodes )
    {
      if ( layerNode->layer() && layerNode->layer()->crs().isValid() )
      {
        mFirstCRS = layerNode->layer()->crs();
        break;
      }
    }
  }

  if ( mFirstCRS.isValid() && firstLayers )
  {
    const QgsGui::ProjectCrsBehavior projectCrsBehavior = QgsSettings().enumValue( u"/projections/newProjectCrsBehavior"_s, QgsGui::UseCrsOfFirstLayerAdded, QgsSettings::App );
    switch ( projectCrsBehavior )
    {
      case QgsGui::UseCrsOfFirstLayerAdded:
      {
        const bool planimetric = QgsSettingsRegistryCore::settingsMeasurePlanimetric->value();
        // Only adjust ellipsoid to CRS if it's not set to planimetric
        QgsProject::instance()->setCrs( mFirstCRS.horizontalCrs(), !planimetric );
        const QgsCoordinateReferenceSystem vertCrs = mFirstCRS.verticalCrs();
        QgsProject::instance()->setVerticalCrs( vertCrs );
        break;
      }

      case QgsGui::UsePresetCrs:
        break;
    }
  }

  mHasLayersLoaded = currentSpatialLayerCount;
  mHasValidLayersLoaded = currentValidSpatialLayerCount;
  if ( currentSpatialLayerCount == 0 )
    mFirstCRS = QgsCoordinateReferenceSystem();

  mPendingCanvasUpdate = false;

  emit canvasLayersChanged( canvasLayers );
}

void QgsLayerTreeMapCanvasBridge::setCanvasLayers( QgsLayerTreeNode *node, QList<QgsMapLayer *> &canvasLayers, QList<QgsMapLayer *> &overviewLayers, QList<QgsMapLayer *> &allLayers )
{
  if ( QgsLayerTree::isLayer( node ) )
  {
    QgsLayerTreeLayer *nodeLayer = QgsLayerTree::toLayer( node );
    if ( nodeLayer->layer() && nodeLayer->layer()->isSpatial() )
    {
      allLayers << nodeLayer->layer();
      if ( nodeLayer->isVisible() )
        canvasLayers << nodeLayer->layer();
      if ( nodeLayer->customProperty( u"overview"_s, 0 ).toInt() )
        overviewLayers << nodeLayer->layer();
    }
  }

  const QList<QgsLayerTreeNode *> children = node->children();
  for ( QgsLayerTreeNode *child : children )
  {
    if ( QgsLayerTree::isGroup( child ) )
    {
      if ( QgsGroupLayer *groupLayer = QgsLayerTree::toGroup( child )->groupLayer() )
      {
        if ( child->isVisible() )
          canvasLayers << groupLayer;
        continue;
      }
    }
    setCanvasLayers( child, canvasLayers, overviewLayers, allLayers );
  }
}

void QgsLayerTreeMapCanvasBridge::deferredSetCanvasLayers()
{
  if ( mPendingCanvasUpdate )
    return;

  mPendingCanvasUpdate = true;
  QMetaObject::invokeMethod( this, "setCanvasLayers", Qt::QueuedConnection );
}

void QgsLayerTreeMapCanvasBridge::nodeVisibilityChanged()
{
  deferredSetCanvasLayers();
}

void QgsLayerTreeMapCanvasBridge::nodeCustomPropertyChanged( QgsLayerTreeNode *node, const QString &key )
{
  Q_UNUSED( node )
  if ( key == "overview"_L1 )
    deferredSetCanvasLayers();
}

void QgsLayerTreeMapCanvasBridge::layersAdded( const QList<QgsMapLayer *> &layers )
{
  for ( QgsMapLayer *l : layers )
  {
    if ( l )
    {
      // Capture the layer through a QPointer and read the canvas through the
      // QPointer member: the connection is torn down when either sender or
      // receiver dies, but never keep dereferencing raw pointers captured
      // from a signal that may outlive its objects.
      const QPointer<QgsMapLayer> layer = l;
      connect( l, &QgsMapLayer::dataSourceChanged, this, [this, layer] {
        if ( layer && layer->isValid() && layer->isSpatial() && mAutoSetupOnFirstLayer && !mHasValidLayersLoaded )
        {
          mHasValidLayersLoaded = true;
          // if we are moving from zero valid layers to non-zero VALID layers, let's zoom to those data
          if ( mCanvas )
            mCanvas->zoomToProjectExtent();
        }
        deferredSetCanvasLayers();
      } );
    }
  }
}

void QgsLayerTreeMapCanvasBridge::layersWillBeRemoved( const QStringList &layerIds )
{
  dropStaleLayerNodes( layerIds );

  // Refresh synchronously: the doomed layers are destroyed right after this
  // signal, so a deferred refresh could let the canvas (and observers) see a
  // removed layer in between.
  setCanvasLayers();
}

void QgsLayerTreeMapCanvasBridge::dropStaleLayerNodes( const QStringList &layerIds )
{
  if ( !mRoot || layerIds.isEmpty() )
    return;

  const QSet<QString> removedIds( layerIds.constBegin(), layerIds.constEnd() );

  // Snapshot first: removeChildNode() deletes the node it takes, and each
  // node object appears exactly once in findLayers(), so deleting one cannot
  // invalidate the walk. Duplicates (a layer registered twice, which the
  // tree tolerates temporarily) are all dropped.
  const QList<QgsLayerTreeLayer *> layerNodes = mRoot->findLayers();
  for ( QgsLayerTreeLayer *layerNode : layerNodes )
  {
    if ( !removedIds.contains( layerNode->layerId() ) )
      continue;

    // A layer node's parent is always a group (the root itself is one), so
    // the removal goes through the owning group.
    if ( QgsLayerTreeGroup *parentGroup = qobject_cast<QgsLayerTreeGroup *>( layerNode->parent() ) )
      parentGroup->removeChildNode( layerNode );
  }
}
