#pragma once

#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <memory>

#include "qgsgcppoint.h"
#include "qgspointxy.h"
#include "qgsimagewarper.h"
#include "rs_georef_params_panel.h"
#include "rs_georef_session_state.h"
#include "rs_georeferencing_session.h"
#include "rs_georef_task_list.h"
#include "rs_georef_workflow_bridge.h"

class QAction;
class QActionGroup;
class QCloseEvent;
class QDockWidget;
class QLabel;
class QMenu;
class QToolBar;
class QgisInterface;
class QgsMapCanvas;
class QgsMapLayerStore;
class QgsMapToolPan;
class QgsMapToolZoom;
class QgsRasterLayer;
class QgsGeorefToolAddPoint;
class QgsGeorefToolMovePoint;
class QgsGeorefToolDeletePoint;
class QgsGCPList;
class QgsGCPListWidget;
class QgsGeorefDataPoint;
class QgsGeorefTransform;
class RsWarpTask;

/**
 * Shared Image Registration shell: GCP list, fit/warp, map tools, docks,
 * dirty-session close. Subclasses only specialize layout (SRC+REF vs SRC+Map)
 * and menus/toolbars.
 */
class QgsGeorefShellWindow : public QMainWindow
{
    Q_OBJECT

  public:
    bool isDirtyForTest() const;
    void markDirtyForTest();
    void setWarpInProgressForTest( bool on );
    void setSourceRasterPath( const QString &p ) { mSourceRasterPath = p; }
    RsGeorefSessionState *sessionStateForTest() { return &mSession; }
    /// Deep Georeferencing Session (GCP fit + warp snapshots + Task Center).
    RsGeoreferencingSession *georefSessionForTest() { return &mGeorefSession; }
    RsGeoreferencingSession &georefSession() { return mGeorefSession; }

    QgsMapCanvas *srcCanvas() const { return mSrcCanvas; }
    QgsMapCanvas *dstCanvas() const { return mDstCanvas; }
    RsGeorefTaskList *taskListForTest() { return mTaskList; }
    /// Runtime session for lab.georef.image_to_map (I2M only; null on I2I).
    RsGeorefWorkflowBridge *workflowBridgeForTest() { return mWorkflowBridge.get(); }
    /**
     * QGIS-style Image→Map GCP: click source image, then enter map X/Y or pick
     * from the main application canvas (no embedded base-image panel).
     * I2I keeps dual-canvas pick (default false).
     */
    virtual bool usesMapCoordsDialogForGcp() const { return false; }

    /// Test hooks for dual-canvas GCP pick (no MapCoords dialog).
    void pickSourceForTest( const QgsPointXY &p ) { onSourcePointPicked( p ); }
    void pickDestForTest( const QgsPointXY &p ) { onDestPointPicked( p ); }
    bool hasPendingSourceForTest() const { return mHasPendingSource; }
    int gcpCountForTest() const;

  public slots:
    /// I2M optional: open coordinate dialog for typed destination (advanced).
    void showCoordDialog( const QgsPointXY &sourcePixel );
    /// Open source (Warp) from file dialog.
    void openSourceRaster();
    /// Open source (Warp) by picking a raster layer from the main project.
    void openSourceFromProjectLayer();
    /// Load source raster from path (file or provider URI). Returns false on failure.
    bool loadSourceRaster( const QString &path, const QString &displayName = QString() );
    /// Validate params, enqueue a warp job into the task list, and run it.
    void applyTransform();
    void loadPoints();
    void savePoints();

  protected:
    explicit QgsGeorefShellWindow( QgisInterface *iface, QWidget *parent = nullptr );
    ~QgsGeorefShellWindow() override;

    void closeEvent( QCloseEvent *e ) override;

    /// After subclass builds canvases + toolbar actions: docks, wires, restore.
    void finishCommonSetup( RsGeorefParamsPanel::Profile profile,
                            const QString &gcpDockObjectName,
                            const QString &paramDockObjectName );

    void setupStatusBar( const QString &coordObj, const QString &crsObj, const QString &rmsObj );
    void createMapTools();
    void wireMapToolActions();

    /// File menu skeleton: Open source, optional extras, .points, Close.
    QMenu *createFileMenu();
    void addStandardMenuBar();

    /// Shared GCP toolbar block (Add/Move/Delete exclusive + load/export .gcp).
    void addGcpEditActions( QToolBar *bar, const QString &objectNamePrefix );
    /**
     * Pan / Zoom In / Zoom Out (exclusive with GCP tools) plus
     * Fit Source / Fit Dest / Fit Both (one-shot extent).
     * Call after canvases exist; tools created in createMapTools().
     */
    void addCanvasNavigationActions( QToolBar *bar, const QString &objectNamePrefix );
    void addApplyAction( QToolBar *bar, const QString &objectName );
    /// View menu: pan/zoom/fit + previous/next extent.
    void addViewMenu();
    /// Right-click on a canvas for navigation shortcuts.
    void installCanvasContextMenu( QgsMapCanvas *canvas, bool isSource );

    /// Exclusive map-tool action group (pan/zoom/GCP).
    QActionGroup *mapToolActionGroup();

    /**
     * Wrap \a canvas with a caption bar (role + layer/file name).
     * Sets *\a labelOut to the caption QLabel (owned by returned panel).
     */
    QWidget *makeCanvasPanel( QgsMapCanvas *canvas,
                              QLabel **labelOut,
                              const QString &roleTitle,
                              const QString &panelObjectName,
                              const QString &labelObjectName );
    /// Update SRC / Warp caption from mSourceRasterPath + mSrcRaster.
    void updateSourceLayerCaption();
    /// Update REF / Map / Base caption (subclass or shell after load).
    void updateDestLayerCaption( const QString &displayName,
                                 const QString &fullPathOrTip = QString() );

    /// Log shell tag for structured warp events ("i2i" / "i2m").
    virtual QString shellId() const { return QStringLiteral( "georef" ); }
    /// Multi-line help text for Help → 关于本窗口.
    virtual QString windowHelpText() const;
    /// Extra snapshot fields (e.g. ref path, sync zoom).
    virtual void captureShellSpecific( RsGeorefSessionState::WorkflowSnapshot & ) const {}
    virtual void applyShellSpecific( const RsGeorefSessionState::WorkflowSnapshot & ) {}
    /// Called when transform method combo changes (I2M toggles DEM).
    virtual void onTransformMethodChangedExtra() {}
    /// Source (Warp) raster is loaded and valid.
    virtual bool hasSourceReady() const;
    /// Dest side ready: REF raster (I2I) or map canvas (I2M, default true).
    virtual bool hasDestReady() const;
    /// Enable/disable GCP tools according to open layers. Call after load.
    virtual void updateToolAvailability();
    /// Pick a raster layer from QgsProject (nullptr if cancelled / none).
    QgsRasterLayer *pickProjectRasterLayer( const QString &dialogTitle );

  protected slots:
    void recomputeFit();
    void onPointsChanged();
    void deleteGcpRows( const QList<int> &rows );
    void selectPoint( const QgsPointXY &p );
    void movePoint( const QgsPointXY &p );
    void releasePoint( const QgsPointXY &p );
    void cancelPoint( const QgsPointXY &p );
    void hoverPoint( const QgsPointXY &p );
    void deletePointAt( const QgsPointXY &p );
    void onTransformMethodChanged();
    /// Dual-canvas GCP: left-click SRC / REF (or Map).
    void onSourcePointPicked( const QgsPointXY &sourceMap );
    void onDestPointPicked( const QgsPointXY &destMap );
    void clearPendingGcpPick();
    void zoomToGcpSource( int row );
    void zoomToGcpDest( int row );
    void zoomToGcpBoth( int row );
    void onGcpTableRowChanged( int row );
    /// Canvas navigation (toolbar).
    void fitSourceExtent();
    void fitDestExtent();
    void fitBothExtents();
    void zoomSourceIn();
    void zoomSourceOut();
    void zoomDestIn();
    void zoomDestOut();
    void zoomPreviousSource();
    void zoomNextSource();
    void zoomPreviousDest();
    void zoomNextDest();
    void zoomPreviousBoth();
    void zoomNextBoth();

  protected:
    void emitStructuredLog( const QgsImageWarper::WarpResult &r );
    void applyWorkflowSnapshot( const RsGeorefSessionState::WorkflowSnapshot &s );
    RsGeorefSessionState::WorkflowSnapshot captureWorkflowSnapshot() const;
    QgsGeorefDataPoint *findDataPoint( const QgsPointXY &p, QgsGcpPoint::PointType type );
    void cancelWarpTask( int taskId );
    void loadWarpOutputToProject( const QString &path );
    void beginPendingSourcePick( const QgsPointXY &sourceMap );
    void commitGcpPair( const QgsPointXY &sourceMap, const QgsPointXY &destMap );
    void panCanvasToPoint( QgsMapCanvas *canvas, const QgsPointXY &mapPoint );
    void setSelectedGcpRow( int row );
    void syncAllMarkers();
    /// Keep Add-GCP tools armed on both canvases (no shared QAction on tools).
    void rearmAddPointTools();
    /// Convert canvas map pick into the raster layer's CRS (avoids CRS mix-ups).
    QgsPointXY mapPickToLayerCrs( QgsMapCanvas *canvas, QgsRasterLayer *layer,
                                  const QgsPointXY &canvasMapPt ) const;
    /// Push source/dest raster paths into GCP table for col/row display.
    void updateGcpTableRasterPaths();
    /// Main app map canvas for I2M "from map" pick (may be null in tests).
    QgsMapCanvas *mainApplicationMapCanvas() const;

    QgisInterface *mIface = nullptr;

    QLabel *mCoordLabel = nullptr;
    QLabel *mCrsLabel = nullptr;
    QLabel *mRmsLabel = nullptr;
    /// Caption above SRC canvas: "源 (Warp): file.tif"
    QLabel *mSrcLayerLabel = nullptr;
    /// Caption above REF/Map canvas: "参考 (Base): …" / "地图 (Base): …"
    QLabel *mDstLayerLabel = nullptr;

    QgsMapCanvas *mSrcCanvas = nullptr;
    QgsMapCanvas *mDstCanvas = nullptr; ///< REF (I2I) or Map (I2M)
    QgsMapLayerStore *mLayerStore = nullptr;
    QgsRasterLayer *mSrcRaster = nullptr;
    /// REF raster for I2I (null for I2M map mode). Used for pixel/CRS helpers.
    QgsRasterLayer *mDstRaster = nullptr;
    QString mDestRasterPath;

    QgsGeorefToolAddPoint *mAddPointTool = nullptr;     ///< SRC canvas
    QgsGeorefToolAddPoint *mAddPointToolDst = nullptr;  ///< REF / Map canvas
    QgsGeorefToolMovePoint *mToolMoveSrc = nullptr;
    QgsGeorefToolMovePoint *mToolMoveDst = nullptr;
    QgsGeorefToolDeletePoint *mToolDeleteSrc = nullptr;
    QgsGeorefToolDeletePoint *mToolDeleteDst = nullptr;
    QgsMapToolPan *mPanSrc = nullptr;
    QgsMapToolPan *mPanDst = nullptr;
    QgsMapToolZoom *mZoomInSrc = nullptr;
    QgsMapToolZoom *mZoomOutSrc = nullptr;
    QgsMapToolZoom *mZoomInDst = nullptr;
    QgsMapToolZoom *mZoomOutDst = nullptr;
    QActionGroup *mMapToolActionGroup = nullptr;
    QAction *mPanAction = nullptr;
    QAction *mZoomInAction = nullptr;
    QAction *mZoomOutAction = nullptr;
    QAction *mFitSrcAction = nullptr;
    QAction *mFitDstAction = nullptr;
    QAction *mFitBothAction = nullptr;
    QAction *mZoomPrevAction = nullptr;
    QAction *mZoomNextAction = nullptr;
    QAction *mAddPointAction = nullptr;
    QAction *mMovePointAction = nullptr;
    QAction *mDeletePointAction = nullptr;
    QAction *mLoadGcpAction = nullptr;
    QAction *mSaveGcpAction = nullptr;
    QAction *mApplyAction = nullptr;
    QAction *mOpenSourceFileAction = nullptr;
    QAction *mOpenSourceLayerAction = nullptr;
    QgsGeorefDataPoint *mMovingPoint = nullptr;
    QgsGeorefDataPoint *mHoveredPoint = nullptr;
    QgsPointXY mMoveOrigin;

    /// Dual-canvas pick: SRC click stored until REF/Map click completes the pair.
    bool mHasPendingSource = false;
    QgsPointXY mPendingSource;
    QgsGcpPoint *mPendingGcp = nullptr;              // owned while pending
    QgsGeorefDataPoint *mPendingDataPoint = nullptr; // owned; parented to this

    QgsGCPList *mGcps = nullptr;
    QgsGCPListWidget *mGcpTable = nullptr;
    QDockWidget *mGcpDock = nullptr;
    QHash<QgsGcpPoint *, QgsGeorefDataPoint *> mDataPoints;

    RsGeorefParamsPanel *mParamsPanel = nullptr;
    QDockWidget *mParamDock = nullptr;
    RsGeorefTaskList *mTaskList = nullptr;
    QDockWidget *mTaskDock = nullptr;
    std::unique_ptr<QgsGeorefTransform> mTransform;
    double mLastRms = 0.0;
    QString mSourceRasterPath;

    RsGeorefSessionState mSession;
    /// Deep Georeferencing Session: GCP fit + immutable warp snapshots + Task Center (#32).
    RsGeoreferencingSession mGeorefSession;
    bool mSuppressDirtyFromList = false;
    bool mWarpInProgress = false;
    /// task-list id → Task Center task id for the running warp.
    QHash<int, long> mActiveWarpTaskCenterIds;

    /// lab.georef.image_to_map session (opened by I2M shell only).
    std::unique_ptr<RsGeorefWorkflowBridge> mWorkflowBridge;
};
