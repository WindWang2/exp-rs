// rs_annotation_controller.cpp — see rs_annotation_controller.h.
#include "rs_annotation_controller.h"

#include <qgsannotationlayer.h>
#include <qgsannotationmarkeritem.h>
#include <qgsannotationpointtextitem.h>
#include <qgsmapcanvas.h>
#include <qgsproject.h>
#include <qgstextformat.h>

#include "annotations/qgscreateannotationitemmaptool.h"
#include "annotations/qgscreateannotationitemmaptool_impl.h"
#include "annotations/qgsmaptoolmodifyannotation.h"
#include "annotations/qgsmaptoolselectannotation.h"

RsAnnotationController::RsAnnotationController( QgsProject *project, QObject *parent )
  : QObject( parent )
  , mProject( project )
{
}

QgsAnnotationLayer *RsAnnotationController::annotationLayer()
{
    if ( mLayer )
        return mLayer.data();
    if ( !mProject )
        return nullptr;

    // Reuse an annotation layer already present in the project (project
    // reopen path) instead of stacking a second one.
    const QMap<QString, QgsMapLayer *> layers = mProject->mapLayers( true );
    for ( QgsMapLayer *layer : layers )
    {
        if ( auto *ann = qobject_cast<QgsAnnotationLayer *>( layer ) )
        {
            mLayer = ann;
            return ann;
        }
    }

    auto *layer = new QgsAnnotationLayer(
      tr( "Annotations" ),
      QgsAnnotationLayer::LayerOptions( mProject->transformContext() ) );
    layer->setCrs( mProject->crs() );
    if ( !mProject->addMapLayer( layer ) )
    {
        delete layer;
        return nullptr;
    }
    mLayer = layer;
    return layer;
}

QString RsAnnotationController::addPointText( const QString &text, const QgsPointXY &point, double sizePt )
{
    QgsAnnotationLayer *layer = annotationLayer();
    if ( !layer )
        return QString();
    auto *item = new QgsAnnotationPointTextItem( text, point );
    QgsTextFormat format;
    format.setSize( sizePt );
    item->setFormat( format );
    return layer->addItem( item );
}

QString RsAnnotationController::addMarker( const QgsPointXY &point )
{
    QgsAnnotationLayer *layer = annotationLayer();
    if ( !layer )
        return QString();
    auto *item = new QgsAnnotationMarkerItem( QgsPoint( point ) );
    return layer->addItem( item );
}

bool RsAnnotationController::removeItem( const QString &itemId )
{
    QgsAnnotationLayer *layer = mLayer.data();
    if ( !layer )
        return false;
    return layer->removeItem( itemId );
}

int RsAnnotationController::itemCount() const
{
    QgsAnnotationLayer *layer = mLayer.data();
    return layer ? layer->items().size() : 0;
}

QgsMapTool *RsAnnotationController::createPointTextTool( QgsMapCanvas *canvas, QgsAdvancedDigitizingDockWidget *cadDock )
{
    if ( !canvas || !cadDock )
        return nullptr;
    auto *tool = new QgsCreatePointTextItemMapTool( canvas, cadDock );
    // Created items surface through the handler's itemCreated signal; route
    // them into our project-owned annotation layer (single destination).
    connect( tool->handler(), &QgsCreateAnnotationItemMapToolHandler::itemCreated,
             this, [this, tool]()
    {
        QgsAnnotationLayer *layer = annotationLayer();
        QgsAnnotationItem *item = tool->handler()->takeCreatedItem();
        if ( layer && item )
            layer->addItem( item );
        else
            delete item;
    } );
    return tool;
}

QgsMapTool *RsAnnotationController::modifyTool( QgsMapCanvas *canvas, QgsAdvancedDigitizingDockWidget *cadDock )
{
    if ( !canvas )
        return nullptr;
    return new QgsMapToolModifyAnnotation( canvas, cadDock );
}

QgsMapTool *RsAnnotationController::selectTool( QgsMapCanvas *canvas, QgsAdvancedDigitizingDockWidget *cadDock )
{
    if ( !canvas )
        return nullptr;
    return new QgsMapToolSelectAnnotation( canvas, cadDock );
}
