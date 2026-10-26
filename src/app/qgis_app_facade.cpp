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

void QgisApp::setEditLeaseCoordinator( SicnuEditLeaseCoordinator *coordinator )
{
  mEditLeaseCoordinator = coordinator;
}

SicnuEditLeaseCoordinator *QgisApp::editLeaseCoordinator() const
{
  return mEditLeaseCoordinator;
}

namespace
{
/// Re-resolve a layer whose Display Layer a coordinated commit/rollback may
/// have relocated (destroyed) synchronously: by stable QGIS layer id first,
/// then by the shell's sicnu/displayLayerId identity, exactly like
/// QgisDesktopWindow::saveEdits() (main_window_vector.cpp). Returns a null
/// pointer when the layer left the project across the commit window.
QgsVectorLayer *resolveAfterLeaseStep( const QString &layerId,
                                       const QString &displayLayerId )
{
  QgsVectorLayer *resolved = qobject_cast<QgsVectorLayer *>(
    QgsProject::instance()->mapLayer( layerId ) );
  if ( resolved || displayLayerId.isEmpty() )
    return resolved;
  for ( QgsMapLayer *candidate : QgsProject::instance()->mapLayers() )
  {
    if ( candidate->customProperty( QStringLiteral( "sicnu/displayLayerId" ) ).toString()
         == displayLayerId )
    {
      resolved = qobject_cast<QgsVectorLayer *>( candidate );
      if ( resolved )
        return resolved;
    }
  }
  return nullptr;
}
} // namespace

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
  // W1b: identity snapshot up front — a coordinated commit may relocate
  // (destroy) this layer's Display Layer before the epilogue re-enters
  // edit mode (the same hazard QgisDesktopWindow::saveEdits() guards).
  const QString layerId = layer->id();
  const QString displayLayerId =
    layer->customProperty( QStringLiteral( "sicnu/displayLayerId" ) ).toString();
  bool ok = layer->commitChanges();
  // True only when a coordinator released the session's lease above: the
  // re-acquire below is exclusively for a session that continues after a
  // PUBLISHED commit. A failed commit keeps both the buffer and the lease,
  // so re-acquiring there would trip the one-editor-per-asset rule with a
  // bogus conflict dialog against this very session.
  bool leaseReleased = false;
  if ( !ok )
  {
    // Route commit errors to the message bar like the neighboring ported
    // code (qgsattributetabledialog et al.): a failed commit used to be
    // indistinguishable from success — the layer silently stayed/re-entered
    // edit mode with the edits still buffered.
    const QString details = layer->commitErrors().isEmpty()
      ? tr( "The data source rejected the commit; the layer keeps the edits buffered." )
      : layer->commitErrors().join( QLatin1Char( '\n' ) );
    if ( mMessageBar )
      mMessageBar->pushMessage( tr( "Save Edits" ),
        tr( "Failed to save edits on layer '%1':\n%2" ).arg( layer->name(), details ),
        Qgis::MessageLevel::Critical );
    // A failed commit keeps the session (and its lease) exactly as the
    // shell path does: the edits are still buffered, nothing to publish.
  }
  else if ( SicnuEditLeaseCoordinator *coordinator = editLeaseCoordinator() )
  {
    // Publish the commit to the data plane: advance the asset revision,
    // release the Edit Lease, refresh sibling Display Layers. The shell's
    // lease bookkeeping owns the same bracket for its own menu path; the
    // attribute-table path must not strand the shell's RAII lease handle
    // (W1b: the asset would stay locked against every other view).
    coordinator->commitEditLease( layer );
    leaseReleased = true;
    layer = resolveAfterLeaseStep( layerId, displayLayerId );
    if ( !layer )
      // The display layer left the project across the commit window: the
      // session ended with the swap; there is nothing to re-enter.
      return ok;
  }
  if ( leaveEditable )
  {
    if ( leaseReleased )
    {
      // Saving stays in edit mode, and an edit session without the lease
      // would edit the asset unlocked — re-acquire first (the shell's
      // saveEdits() enforces the same order). A cross-commit-window
      // acquisition failure must leave the layer NOT editable. The
      // conflict dialog is suppressed (R2-P3-4): this is a save, not an
      // explicit "start editing" request — the shell reports the same
      // window through its status bar.
      if ( SicnuEditLeaseCoordinator *coordinator = editLeaseCoordinator() )
        if ( !coordinator->acquireEditLease( layer, /*showConflictWarning=*/false ) )
          return ok;
    }
    layer->startEditing();
  }
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
  SicnuEditLeaseCoordinator *coordinator = editLeaseCoordinator();
  if ( layer->isEditable() )
  {
    if ( layer->isModified() )
    {
      if ( !layer->commitChanges() )
      {
        // R2-P3-5: the shell's toggleEditing() surfaces a refused commit
        // (modal + status bar); the facade used to return false in
        // silence, so an attribute-table user saw the toggle stay checked
        // with no reason. Route the same details through the message bar
        // the dialog shows. The session (and its lease) stays exactly as
        // the shell leaves it: still editing, edits still buffered.
        const QString details = layer->commitErrors().isEmpty()
          ? tr( "The data source rejected the commit; the layer keeps the edits buffered." )
          : layer->commitErrors().join( QLatin1Char( '\n' ) );
        if ( mMessageBar )
          mMessageBar->pushMessage( tr( "Save Edits" ),
            tr( "Failed to save edits on layer '%1':\n%2" ).arg( layer->name(), details ),
            Qgis::MessageLevel::Critical );
        return false;
      }
      // The session's buffered edits are now on the provider: publish the
      // revision and release the lease (W1b — same bracket as the shell
      // path). The coordinator may relocate this layer; nothing below
      // touches it again.
      if ( coordinator )
        coordinator->commitEditLease( layer );
      return true;
    }
    else
    {
      layer->rollBack();
      if ( coordinator )
        coordinator->rollbackEditLease( layer );
    }
    return true;
  }
  else
  {
    // Entering an edit session requires the exclusive lease first: the
    // shell's toggleEditing() treats a failed acquisition as a hard
    // refusal (another view is editing the asset) — the attribute-table
    // path must not open an unguarded session (W1b).
    if ( coordinator && !coordinator->acquireEditLease( layer ) )
      return false;
    if ( !layer->startEditing() )
    {
      // A refused start must not keep the lease (APP-10/R18-D contract):
      // the asset would stay locked against every other view, including
      // this one.
      if ( coordinator )
        coordinator->rollbackEditLease( layer );
      return false;
    }
    return true;
  }
}

void QgisApp::saveEdits()
{
  // no-arg overload: save edits for all editable vector layers. Iterate by
  // id and re-resolve each layer before its turn: a coordinated commit
  // relocates (destroys) Display Layers, so sibling snapshot pointers may
  // be freed mid-loop (the same hazard checkUnsavedChanges() guards).
  QStringList editableIds;
  for ( QgsMapLayer *layer : QgsProject::instance()->layers<QgsVectorLayer *>() )
    if ( layer && layer->isEditable() && layer->isModified() )
      editableIds.append( layer->id() );

  for ( const QString &layerId : editableIds )
  {
    QgsVectorLayer *layer = qobject_cast<QgsVectorLayer *>(
      QgsProject::instance()->mapLayer( layerId ) );
    if ( !layer || !layer->isEditable() || !layer->isModified() )
      continue; // relocated across an earlier commit in this batch
    const QString displayLayerId =
      layer->customProperty( QStringLiteral( "sicnu/displayLayerId" ) ).toString();
    if ( !layer->commitChanges() )
    {
      // Same honesty as the per-layer overload: every failed commit is
      // surfaced, never silently swallowed by the batch loop.
      const QString details = layer->commitErrors().isEmpty()
        ? tr( "The data source rejected the commit; the layer keeps the edits buffered." )
        : layer->commitErrors().join( QLatin1Char( '\n' ) );
      if ( mMessageBar )
        mMessageBar->pushMessage( tr( "Save Edits" ),
          tr( "Failed to save edits on layer '%1':\n%2" ).arg( layer->name(), details ),
          Qgis::MessageLevel::Critical );
      layer->startEditing();
      continue;
    }
    if ( SicnuEditLeaseCoordinator *coordinator = editLeaseCoordinator() )
    {
      coordinator->commitEditLease( layer );
      layer = resolveAfterLeaseStep( layerId, displayLayerId );
      if ( !layer )
        continue; // the swap ended this layer's session
      // R2-P3-4: re-acquire without the modal — the batch save reports a
      // lost lease through the message bar path below, not a dialog.
      if ( !coordinator->acquireEditLease( layer, /*showConflictWarning=*/false ) )
        // Another view took the lease across the commit window: leave the
        // layer out of edit mode rather than edit unlocked.
        continue;
    }
    layer->startEditing();
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
