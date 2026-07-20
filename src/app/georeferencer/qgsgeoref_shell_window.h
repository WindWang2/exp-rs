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
#include "rs_georef_task_list.h"

class QAction;
class QCloseEvent;
class QDockWidget;
class QLabel;
class QMenu;
class QToolBar;
class QgisInterface;
class QgsMapCanvas;
class QgsMapLayerStore;
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

    QgsMapCanvas *srcCanvas() const { return mSrcCanvas; }
    QgsMapCanvas *dstCanvas() const { return mDstCanvas; }
    RsGeorefTaskList *taskListForTest() { return mTaskList; }

  public slots:
    void showCoordDialog( const QgsPointXY &sourcePixel );
    void openSourceRaster();
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
    void addApplyAction( QToolBar *bar, const QString &objectName );

    /// Log shell tag for structured warp events ("i2i" / "i2m").
    virtual QString shellId() const { return QStringLiteral( "georef" ); }
    /// Multi-line help text for Help → 关于本窗口.
    virtual QString windowHelpText() const;
    /// Extra snapshot fields (e.g. ref path, sync zoom).
    virtual void captureShellSpecific( RsGeorefSessionState::WorkflowSnapshot & ) const {}
    virtual void applyShellSpecific( const RsGeorefSessionState::WorkflowSnapshot & ) {}
    /// Called when transform method combo changes (I2M toggles DEM).
    virtual void onTransformMethodChangedExtra() {}

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

  protected:
    void emitStructuredLog( const QgsImageWarper::WarpResult &r );
    void applyWorkflowSnapshot( const RsGeorefSessionState::WorkflowSnapshot &s );
    RsGeorefSessionState::WorkflowSnapshot captureWorkflowSnapshot() const;
    QgsGeorefDataPoint *findDataPoint( const QgsPointXY &p, QgsGcpPoint::PointType type );
    void cancelWarpTask( int taskId );
    void loadWarpOutputToProject( const QString &path );

    QgisInterface *mIface = nullptr;

    QLabel *mCoordLabel = nullptr;
    QLabel *mCrsLabel = nullptr;
    QLabel *mRmsLabel = nullptr;

    QgsMapCanvas *mSrcCanvas = nullptr;
    QgsMapCanvas *mDstCanvas = nullptr; ///< REF (I2I) or Map (I2M)
    QgsMapLayerStore *mLayerStore = nullptr;
    QgsRasterLayer *mSrcRaster = nullptr;

    QgsGeorefToolAddPoint *mAddPointTool = nullptr;
    QgsGeorefToolMovePoint *mToolMoveSrc = nullptr;
    QgsGeorefToolMovePoint *mToolMoveDst = nullptr;
    QgsGeorefToolDeletePoint *mToolDeleteSrc = nullptr;
    QgsGeorefToolDeletePoint *mToolDeleteDst = nullptr;
    QAction *mAddPointAction = nullptr;
    QAction *mMovePointAction = nullptr;
    QAction *mDeletePointAction = nullptr;
    QAction *mApplyAction = nullptr;
    QgsGeorefDataPoint *mMovingPoint = nullptr;
    QgsGeorefDataPoint *mHoveredPoint = nullptr;
    QgsPointXY mMoveOrigin;

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
    bool mSuppressDirtyFromList = false;
    bool mWarpInProgress = false;
    /// task-list id → live QgsTask (for cancel / progress).
    QHash<int, QPointer<RsWarpTask>> mActiveWarpTasks;
};
