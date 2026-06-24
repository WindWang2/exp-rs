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
      QObject *parent = nullptr
    );

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
};
