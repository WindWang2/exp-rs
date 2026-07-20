#include "qgis_app_facade.h"

#include <qgsmapcanvas.h>
#include <qgsadvanceddigitizingdockwidget.h>
#include <qgsvectorlayertools.h>
#include <qgsmessagebar.h>
#include <qgsstatusbar.h>
#include <qgsattributeeditorcontext.h>
#include <qgsvectorlayer.h>
#include <qgsproject.h>
#include <qgscoordinatereferencesystem.h>
#include "qgsclipboard.h"
#include <QAction>
#include <QToolBar>
#include <QMainWindow>
#include <QStatusBar>

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
  QgsClipboard *clipboard,
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
  if ( clipboard )
    app->mClipboard = clipboard;

  // Own a real QgsStatusBar so vertex tool / map tools can show messages.
  if ( mainWindow && !app->mStatusBar )
  {
    app->mStatusBar = new QgsStatusBar( mainWindow );
    if ( QStatusBar *sb = mainWindow->statusBar() )
    {
      sb->addPermanentWidget( app->mStatusBar, 1 );
      app->mStatusBar->setParentStatusBar( sb );
    }
  }

  // Base context is the "home" binding — clear any pending restore snapshot.
  app->mHasSavedContext = false;
}

void QgisApp::rebind(
  QgsMapCanvas *canvas,
  QgsAdvancedDigitizingDockWidget *cadDock,
  QgsVectorLayerTools *vectorLayerTools,
  QgsMessageBar *messageBar,
  QMainWindow *mainWindow )
{
  if ( !mHasSavedContext )
  {
    mSavedCanvas = mCanvas;
    mSavedCadDock = mCadDock;
    mSavedVectorLayerTools = mVectorLayerTools;
    mSavedMessageBar = mMessageBar;
    mSavedMainWindow = mMainWindow;
    mHasSavedContext = true;
  }
  mCanvas = canvas;
  mCadDock = cadDock;
  mVectorLayerTools = vectorLayerTools;
  mMessageBar = messageBar;
  mMainWindow = mainWindow;
}

void QgisApp::restoreContext()
{
  if ( !mHasSavedContext )
    return;
  mCanvas = mSavedCanvas;
  mCadDock = mSavedCadDock;
  mVectorLayerTools = mSavedVectorLayerTools;
  mMessageBar = mSavedMessageBar;
  mMainWindow = mSavedMainWindow;
  mHasSavedContext = false;
}

// ── Accessors ───────────────────────────────────────────────────────────────

QgsMapCanvas *QgisApp::mapCanvas() const          { return mCanvas; }
QgsAdvancedDigitizingDockWidget *QgisApp::cadDockWidget() const { return mCadDock; }
QgsVectorLayerTools *QgisApp::vectorLayerTools() const  { return mVectorLayerTools; }
QgsMessageBar *QgisApp::messageBar() const        { return mMessageBar; }

QMainWindow *QgisApp::mainWindow() const { return mMainWindow; }

QgsVertexEditor *QgisApp::vertexEditor() const { return mVertexEditor; }

void QgisApp::addUserInputWidget( QWidget *widget )
{
  if ( !widget )
    return;

  if ( mCanvas )
  {
    widget->setParent( mCanvas );
    const QSize hint = widget->sizeHint().isValid() ? widget->sizeHint() : widget->size();
    const int x = 10;
    const int y = qMax( 0, mCanvas->height() - hint.height() - 10 );
    widget->move( x, y );
    widget->show();
    widget->raise();
    return;
  }

  if ( mMainWindow )
  {
    widget->setParent( mMainWindow );
    widget->setWindowFlags( Qt::Tool | Qt::FramelessWindowHint );
    widget->show();
  }
}

QgsStatusBar *QgisApp::statusBarIface() const
{
  if ( !mStatusBar )
  {
    // Lazy create if initialize ran without a main window (or was never called).
    QWidget *parent = mMainWindow ? static_cast<QWidget *>( mMainWindow ) : nullptr;
    mStatusBar = new QgsStatusBar( parent );
    if ( mMainWindow )
    {
      if ( QStatusBar *sb = mMainWindow->statusBar() )
      {
        sb->addPermanentWidget( mStatusBar, 1 );
        mStatusBar->setParentStatusBar( sb );
      }
    }
  }
  return mStatusBar;
}

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

void QgisApp::cutSelectionToClipboard( QgsVectorLayer *layer )
{
  if ( !layer || !mClipboard )
    return;
  mClipboard->replaceWithCopyOf( layer );
  if ( layer->isEditable() )
    layer->deleteSelectedFeatures();
}

void QgisApp::copySelectionToClipboard( QgsVectorLayer *layer )
{
  if ( !layer || !mClipboard )
    return;
  mClipboard->replaceWithCopyOf( layer );
}

void QgisApp::pasteFromClipboard( QgsVectorLayer *layer )
{
  if ( !layer || !mClipboard || !layer->isEditable() )
    return;
  QgsFeatureList features = mClipboard->transformedCopyOf( layer->crs(), layer->fields() );
  if ( features.isEmpty() )
    return;
  layer->addFeatures( features );
  if ( mCanvas )
    mCanvas->refresh();
}

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
