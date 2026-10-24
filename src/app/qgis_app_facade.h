#pragma once

#include "qgis_app.h"  // for APP_EXPORT

#include <QObject>
#include <QList>
#include <QAction>
#include <QToolBar>
#include <qgsfeature.h>

class QgsMapCanvas;
class QgsAdvancedDigitizingDockWidget;
class QgsVectorLayerTools;
class QgsMessageBar;
class QgsStatusBar;
class QgsVertexEditor;
class QgsClipboard;
class QgsMapLayer;
class QgsVectorLayer;
class QgsMapTool;
class QWidget;
class QMainWindow;
class QgsAttributeEditorContext;
class QgsFeatureStore;
class QgsCoordinateReferenceSystem;

/**
 * \brief Data-plane edit-lease coordination for the facade editing paths.
 *
 * The attribute table (qgsattributetabledialog.cpp) and the ported map
 * tools end edit sessions through QgisApp::saveEdits()/toggleEditing() and
 * QgsGuiVectorLayerTools, which speak directly to the provider. For
 * Display Layers backed by a Data Asset those raw calls must be bracketed
 * by the DataManager Edit Lease bookkeeping — the same contract the
 * shell's QgisDesktopWindow::toggleEditing()/saveEdits() implement: the
 * lease is acquired before startEditing(), released with a revision bump
 * after a commit, and released without an event after a rollback. Without
 * the bracket, an attribute-table session leaves the asset locked against
 * every other view (the RAII lease handle in the shell stays alive) and
 * the asset revision stale (siblings never refresh).
 *
 * The shell registers itself at startup and releases the seam in its
 * destructor; while no coordinator is installed (headless facade tests,
 * pre-window startup) the facade keeps native QGIS behavior.
 */
class APP_EXPORT SicnuEditLeaseCoordinator
{
  public:
    virtual ~SicnuEditLeaseCoordinator() = default;

    /// Acquire the exclusive Edit Lease for the layer's asset before
    /// entering edit mode. Return false to refuse the session (another
    /// view holds the lease). @p showConflictWarning controls the modal
    /// conflict dialog: the shell's own menu path shows it (the user asked
    /// to edit), while the re-acquire after a commit passes false and
    /// reports through the status bar only (main_window_vector.cpp's
    /// saveEdits() contract) — a modal popping up during an attribute-table
    /// save is noise, not information.
    virtual bool acquireEditLease( QgsVectorLayer *layer, bool showConflictWarning = true ) = 0;

    /// Publish a committed edit buffer to the data plane and release the
    /// lease. May relocate (destroy) @p layer synchronously — callers must
    /// snapshot identity and re-resolve afterwards.
    virtual void commitEditLease( QgsVectorLayer *layer ) = 0;

    /// Release the lease without advancing the revision. May relocate
    /// (destroy) @p layer synchronously.
    virtual void rollbackEditLease( QgsVectorLayer *layer ) = 0;
};

/**
 * \brief Lightweight singleton facade that wraps QgisDesktopWindow.
 *
 * Ported QGIS map tools and app-level code call QgisApp::instance() to
 * obtain global resources (map canvas, CAD dock, vector layer tools, etc.).
 * This facade stores raw pointers set during startup via initialize() and
 * delegates to the actual QgisDesktopWindow members.
 */
class APP_EXPORT QgisApp : public QObject
{
    Q_OBJECT

    // Expose as public for ported code that accesses QgisApp action members
    // directly. These are stubs — the actual actions live in QgisDesktopWindow.
  public:
    QAction *mActionDigitizeWithSegment = nullptr;
    QAction *mActionDigitizeWithCurve = nullptr;
    QAction *mActionStreamDigitize = nullptr;
    QAction *mActionDigitizeShape = nullptr;
    QAction *mActionDigitizeWithBezier = nullptr;
    QAction *mActionDigitizeWithNurbs = nullptr;
    QToolBar *mDigitizeToolBar = nullptr;
    QToolBar *mShapeDigitizeToolBar = nullptr;
    QList<QgsMapTool *> captureTools() const { return QList<QgsMapTool *>(); }

  public:
    static QgisApp *instance();

    static void initialize(
      QgsMapCanvas *canvas,
      QgsAdvancedDigitizingDockWidget *cadDock,
      QgsVectorLayerTools *vectorLayerTools,
      QgsMessageBar *messageBar,
      QMainWindow *mainWindow = nullptr,
      QgsClipboard *clipboard = nullptr,
      QObject *parent = nullptr
    );

    /**
     * Installs the data-plane edit-lease coordinator for the facade
     * editing paths (weak semantic: the coordinator must clear the seam in
     * its destructor — the shell does — because the facade singleton
     * outlives windows). @c nullptr restores native QGIS behavior.
     */
    void setEditLeaseCoordinator( SicnuEditLeaseCoordinator *coordinator );
    SicnuEditLeaseCoordinator *editLeaseCoordinator() const;

    /**
     * Temporarily rebind the singleton to another canvas/CAD/tools context
     * (e.g. the classification window). Call restoreContext() to undo.
     * Nested rebind is not supported — only one saved context is kept.
     */
    void rebind(
      QgsMapCanvas *canvas,
      QgsAdvancedDigitizingDockWidget *cadDock,
      QgsVectorLayerTools *vectorLayerTools,
      QgsMessageBar *messageBar,
      QMainWindow *mainWindow = nullptr
    );

    //! Restore the context saved by the last rebind(). No-op if none saved.
    void restoreContext();

    // ── Accessors ──────────────────────────────────────────────────────────

    QgsMapCanvas *mapCanvas() const;
    QgsAdvancedDigitizingDockWidget *cadDockWidget() const;
    QgsVectorLayerTools *vectorLayerTools() const;
    QgsMessageBar *messageBar() const;
    QMainWindow *mainWindow() const;
    QgsVertexEditor *vertexEditor() const;
    void addUserInputWidget( QWidget *widget );
    class QgsStatusBar *statusBarIface() const;
    QString styleSheet() const;

    // ── Stub methods for ported code ───────────────────────────────────────

    //! Returns a default attribute editor context
    QgsAttributeEditorContext createAttributeEditorContext() const;

    //! Clipboard — returns nullptr for now
    QgsClipboard *clipboard() const;

    //! Save edits for a specific layer
    bool saveEdits( QgsVectorLayer *layer, bool leaveEditable = true, bool triggerRepaint = true );

    //! Delete selected features from a layer
    void deleteSelected( QgsVectorLayer *layer, QWidget *parent = nullptr );

    //! Cut/copy/paste stubs
    void cutSelectionToClipboard( QgsVectorLayer *layer );
    void copySelectionToClipboard( QgsVectorLayer *layer );
    void pasteFromClipboard( QgsVectorLayer *layer );

    //! Toggle editing for a layer
    bool toggleEditing( QgsVectorLayer *layer, bool allowCancel = true );

    //! Save all edits (no-arg overload used by attribute table)
    void saveEdits();

    //! Freeze/unfreeze map canvases (no-op stub)
    void freezeCanvases( bool frozen = true );

    //! Ask user for datum transform — returns empty string (stub)
    QString askUserForDatumTransform( const QgsCoordinateReferenceSystem &sourceCrs, const QgsCoordinateReferenceSystem &destinationCrs );

    //! Paste features (stub)
    void pasteFeatures( QgsVectorLayer *layer, int propTypes, qsizetype count, QgsFeatureList &features );

    //! Snapping options action — returns nullptr (stub)
    QAction *snappingOptions();

  signals:
    void newProject();
    void projectRead();

  private:
    explicit QgisApp( QObject *parent = nullptr );
    ~QgisApp() override;

    QgisApp( const QgisApp & ) = delete;
    QgisApp &operator=( const QgisApp & ) = delete;

    static QgisApp *sInstance;

    QgsMapCanvas *mCanvas = nullptr;
    QMainWindow *mMainWindow = nullptr;
    QgsAdvancedDigitizingDockWidget *mCadDock = nullptr;
    QgsVectorLayerTools *mVectorLayerTools = nullptr;
    QgsMessageBar *mMessageBar = nullptr;
    QgsVertexEditor *mVertexEditor = nullptr;
    QgsClipboard *mClipboard = nullptr;
    mutable QgsStatusBar *mStatusBar = nullptr;

    SicnuEditLeaseCoordinator *mEditLeaseCoordinator = nullptr;

    // Snapshot for rebind()/restoreContext()
    bool mHasSavedContext = false;
    QgsMapCanvas *mSavedCanvas = nullptr;
    QMainWindow *mSavedMainWindow = nullptr;
    QgsAdvancedDigitizingDockWidget *mSavedCadDock = nullptr;
    QgsVectorLayerTools *mSavedVectorLayerTools = nullptr;
    QgsMessageBar *mSavedMessageBar = nullptr;
};
