#include "qgis_app_facade.h"

#include <qgsmapcanvas.h>
#include <qgsadvanceddigitizingdockwidget.h>
#include <qgsvectorlayertools.h>
#include <qgsmessagebar.h>
#include <qgsattributeeditorcontext.h>
#include <qgsvectorlayer.h>
#include <qgsproject.h>
#include <qgscoordinatereferencesystem.h>
#include <QAction>
#include <QToolBar>
#include <QMainWindow>

// ── Singleton ──────────────────────────────────────────────────────────────

QgisApp *QgisApp::sInstance = nullptr;

QgisApp::QgisApp( QObject *parent )
  : QObject( parent )
{
}

QgisApp::~QgisApp()
{
  sInstance = nullptr;
}

QgisApp *QgisApp::instance()
{
  if ( !sInstance )
  {
    sInstance = new QgisApp();
  }
  return sInstance;
}

// ── Initialization ──────────────────────────────────────────────────────────

void QgisApp::initialize(
  QgsMapCanvas *canvas,
  QgsAdvancedDigitizingDockWidget *cadDock,
  QgsVectorLayerTools *vectorLayerTools,
  QgsMessageBar *messageBar,
  QMainWindow *mainWindow,
  QObject *parent )
{
  QgisApp *app = instance();

  if ( parent )
    app->setParent( parent );

  app->mCanvas = canvas;
  app->mCadDock = cadDock;
  app->mVectorLayerTools = vectorLayerTools;
  app->mMessageBar = messageBar;
  app->mMainWindow = mainWindow;
}

// ── Accessors ───────────────────────────────────────────────────────────────

QgsMapCanvas *QgisApp::mapCanvas() const          { return mCanvas; }
QgsAdvancedDigitizingDockWidget *QgisApp::cadDockWidget() const { return mCadDock; }
QgsVectorLayerTools *QgisApp::vectorLayerTools() const  { return mVectorLayerTools; }
QgsMessageBar *QgisApp::messageBar() const        { return mMessageBar; }

QMainWindow *QgisApp::mainWindow() const { return mMainWindow; }

QgsVertexEditor *QgisApp::vertexEditor() const { return mVertexEditor; }

void QgisApp::addUserInputWidget( QWidget *widget ) { Q_UNUSED( widget ) }

class QgsStatusBar *QgisApp::statusBarIface() const { return nullptr; }

QString QgisApp::styleSheet() const { return QString(); }

// ── Stub methods ────────────────────────────────────────────────────────────

QgsAttributeEditorContext QgisApp::createAttributeEditorContext() const
{
  return QgsAttributeEditorContext();
}

QgsClipboard *QgisApp::clipboard() const { return mClipboard; }

bool QgisApp::saveEdits( QgsVectorLayer *layer, bool leaveEditable, bool triggerRepaint )
{
  if ( !layer || !layer->isEditable() ) return false;
  bool ok = layer->commitChanges();
  if ( leaveEditable ) layer->startEditing();
  if ( triggerRepaint && mCanvas ) mCanvas->refresh();
  return ok;
}

void QgisApp::deleteSelected( QgsVectorLayer *layer, QWidget *parent )
{
  if ( !layer || !layer->isEditable() ) return;
  layer->deleteFeatures( layer->selectedFeatureIds() );
}

void QgisApp::cutSelectionToClipboard( QgsVectorLayer *layer ) { Q_UNUSED( layer ) }
void QgisApp::copySelectionToClipboard( QgsVectorLayer *layer ) { Q_UNUSED( layer ) }
void QgisApp::pasteFromClipboard( QgsVectorLayer *layer ) { Q_UNUSED( layer ) }

bool QgisApp::toggleEditing( QgsVectorLayer *layer, bool allowCancel )
{
  Q_UNUSED( allowCancel )
  if ( !layer ) return false;
  if ( layer->isEditable() )
  {
    if ( layer->isModified() )
    {
      if ( !layer->commitChanges() )
      {
        return false;
      }
    }
    else
    {
      layer->rollBack();
    }
    return true;
  }
  else
  {
    return layer->startEditing();
  }
}

void QgisApp::saveEdits()
{
  // no-arg overload: save edits for all editable vector layers
  const auto layers = QgsProject::instance()->layers<QgsVectorLayer *>();
  for ( QgsVectorLayer *layer : layers )
  {
    if ( layer && layer->isEditable() && layer->isModified() )
    {
      layer->commitChanges();
      layer->startEditing();
    }
  }
  if ( mCanvas ) mCanvas->refresh();
}

void QgisApp::freezeCanvases( bool frozen )
{
  Q_UNUSED( frozen )
}

QString QgisApp::askUserForDatumTransform( const QgsCoordinateReferenceSystem &sourceCrs, const QgsCoordinateReferenceSystem &destinationCrs )
{
  Q_UNUSED( sourceCrs )
  Q_UNUSED( destinationCrs )
  return QString();
}

void QgisApp::pasteFeatures( QgsVectorLayer *layer, int propTypes, qsizetype count, QgsFeatureList &features )
{
  Q_UNUSED( layer )
  Q_UNUSED( propTypes )
  Q_UNUSED( count )
  Q_UNUSED( features )
}

QAction *QgisApp::snappingOptions()
{
  return nullptr;
}
