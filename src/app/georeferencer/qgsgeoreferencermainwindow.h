#pragma once

#include <QHash>
#include <QMainWindow>
#include <memory>

#include "qgsgcppoint.h"
#include "qgspointxy.h"
#include "qgsimagewarper.h"
#include "rs_georef_mode_toggle.h"
#include "rs_georef_session_state.h"

class QToolBar;
class QLabel;
class QAction;
class QCloseEvent;
class QDockWidget;
class QgisInterface;
class QgsMapCanvas;
class QgsMapLayerStore;
class QgsRasterLayer;
class RsTwinCanvasSyncController;
class QgsGeorefToolAddPoint;
class QgsGeorefToolMovePoint;
class QgsGeorefToolDeletePoint;
class QgsGCPList;
class QgsGCPListWidget;
class QgsGeorefDataPoint;
class QgsGeorefTransform;
class RsGeorefParamsPanel;

/**
 * \brief Georeferencer main window shell.
 *
 * Twin SRC/REF canvases inside a horizontal splitter, throttle-synchronized
 * by an RsTwinCanvasSyncController. The "Add GCP" toolbar action installs
 * QgsGeorefToolAddPoint on the source canvas; clicking the canvas pops a
 * QgsMapCoordsDialog for the matching destination coordinate.
 *
 * Tasks 11.4.4 (shell) + 11.4.5 (twin canvas, sync, add-point flow).
 */
class QgsGeoreferencerMainWindow : public QMainWindow
{
    Q_OBJECT

  public:
    explicit QgsGeoreferencerMainWindow( QgisInterface *iface, QWidget *parent = nullptr );
    ~QgsGeoreferencerMainWindow() override;

    /// Test hook: expose dirty state without UI.
    bool isDirtyForTest() const;
    void markDirtyForTest();
    RsGeorefSessionState *sessionStateForTest() { return &mSession; }

  public slots:
    /// Slot connected to QgsGeorefToolAddPoint::showCoordDialog — pops up
    /// the MapCoords dialog so the user can enter the destination coord.
    void showCoordDialog( const QgsPointXY &sourcePixel );

    /// Test hook (Task 11.4.7) — flips the GCP table + Apply action enabled
    /// state without launching a real warp.
    void setWarpInProgressForTest( bool on );

    /// Setter so future File→Open wiring can supply the source raster path.
    void setSourceRasterPath( const QString &p ) { mSourceRasterPath = p; }

    /// Task 11.5.3 — File menu hooks. The no-arg overloads open a QFileDialog;
    /// the `bool loadReferenceRaster(const QString&)` overload is the testable
    /// entry point that bypasses the dialog.
    void openSourceRaster();
    void loadReferenceRaster();
    bool loadReferenceRaster( const QString &path );

  protected:
    void closeEvent( QCloseEvent *e ) override;

  private slots:
    /// Recompute transform fit + residuals when GCP list changes.
    void recomputeFit();
    /// Wired to the Apply toolbar action — runs a RsWarpTask.
    void applyTransform();
    /**
     * Reconciles `mDataPoints` with `mGcps`: creates a QgsGeorefDataPoint
     * for any new QgsGcpPoint, refreshes existing ones, and deletes any
     * data point whose backing QgsGcpPoint is no longer in the list.
     * Wired to `QgsGCPList::changed`.
     */
    void onPointsChanged();
    /// Task 11.5.3 — repaint the REF canvas + params panel for the new mode.
    void onModeChanged( RsGeorefModeToggle::Mode m );
    void loadPoints();
    void savePoints();
    void deleteSelectedGcp();
    void deleteGcpRows( const QList<int> &rows );

    // Task 11.6.3 — Move / Delete map-tool slots
    void selectPoint( const QgsPointXY &p );
    void movePoint( const QgsPointXY &p );
    void releasePoint( const QgsPointXY &p );
    void cancelPoint( const QgsPointXY &p );
    void hoverPoint( const QgsPointXY &p );
    void deletePointAt( const QgsPointXY &p );

  private:
    void setupMenus();
    void setupToolbars();
    void setupStatusBar();
    void setupCentralWidget();

    /// Emit structured JSON to QgsMessageLog tag "Georeferencer".
    void emitStructuredLog( const QgsImageWarper::WarpResult &r );

    void applyWorkflowSnapshot( const RsGeorefSessionState::WorkflowSnapshot &s );
    RsGeorefSessionState::WorkflowSnapshot captureWorkflowSnapshot() const;

    /// Nearest GCP whose marker contains \a p on the given canvas side.
    QgsGeorefDataPoint *findDataPoint( const QgsPointXY &p, QgsGcpPoint::PointType type );

    QgisInterface *mIface = nullptr;
    RsGeorefModeToggle *mModeToggle = nullptr;
    QToolBar *mModeBar = nullptr;
    QLabel *mCoordLabel = nullptr;
    QLabel *mCrsLabel = nullptr;
    QLabel *mRmsLabel = nullptr;

    // Task 11.4.5 — twin canvas
    QgsMapCanvas *mSrcCanvas = nullptr;
    QgsMapCanvas *mRefCanvas = nullptr;
    RsTwinCanvasSyncController *mSyncCtl = nullptr;
    QgsGeorefToolAddPoint *mAddPointTool = nullptr;
    QAction *mAddPointAction = nullptr;
    QAction *mSyncZoomAction = nullptr;

    // Task 11.6.3 — Move / Delete map tools (SRC + REF)
    QgsGeorefToolMovePoint *mToolMoveSrc = nullptr;
    QgsGeorefToolMovePoint *mToolMoveDst = nullptr;
    QgsGeorefToolDeletePoint *mToolDeleteSrc = nullptr;
    QgsGeorefToolDeletePoint *mToolDeleteDst = nullptr;
    QAction *mMovePointAction = nullptr;
    QAction *mDeletePointAction = nullptr;
    QgsGeorefDataPoint *mMovingPoint = nullptr;
    QgsGeorefDataPoint *mHoveredPoint = nullptr;
    QgsPointXY mMoveOrigin;

    // Task 11.4.6 — GCP list + bottom dock
    QgsGCPList *mGcps = nullptr;
    QgsGCPListWidget *mGcpTable = nullptr;
    QDockWidget *mGcpDock = nullptr;
    /// Task 11.5.2 — owns the GUI adapter (canvas markers) for each live GCP.
    QHash<QgsGcpPoint *, QgsGeorefDataPoint *> mDataPoints;

    // Task 11.4.7 — right param dock, transform fit cache, source raster
    RsGeorefParamsPanel *mParamsPanel = nullptr;
    QDockWidget *mParamDock = nullptr;
    QAction *mApplyAction = nullptr;
    std::unique_ptr<QgsGeorefTransform> mTransform;
    double mLastRms = 0.0;
    QString mSourceRasterPath;
    /// Last successfully chosen reference raster path (settings only; not auto-loaded).
    QString mRefRasterPath;

    // Task 11.5.3 — Image-to-Image mode owns its own layer store so the REF
    // canvas can show a raster independent of the main application project.
    QgsMapLayerStore *mRefStore = nullptr;
    QgsRasterLayer *mRefRaster = nullptr; // non-owning; owned by mRefStore
    QgsRasterLayer *mSrcRaster = nullptr; // non-owning; owned by mRefStore

    // Task 11.6.2 — dirty flag / settings session
    RsGeorefSessionState mSession;
    bool mSuppressDirtyFromList = false;
    bool mWarpInProgress = false;
};
