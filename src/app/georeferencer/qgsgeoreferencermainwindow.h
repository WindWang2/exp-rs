#pragma once

#include <QMainWindow>
#include <memory>

#include "qgspointxy.h"
#include "qgsimagewarper.h"
#include "rs_georef_mode_toggle.h"

class QToolBar;
class QLabel;
class QAction;
class QCloseEvent;
class QDockWidget;
class QgisInterface;
class QgsMapCanvas;
class RsTwinCanvasSyncController;
class QgsGeorefToolAddPoint;
class QgsGCPList;
class QgsGCPListWidget;
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

  public slots:
    /// Slot connected to QgsGeorefToolAddPoint::showCoordDialog — pops up
    /// the MapCoords dialog so the user can enter the destination coord.
    void showCoordDialog( const QgsPointXY &sourcePixel );

    /// Test hook (Task 11.4.7) — flips the GCP table + Apply action enabled
    /// state without launching a real warp.
    void setWarpInProgressForTest( bool on );

    /// Setter so future File→Open wiring can supply the source raster path.
    void setSourceRasterPath( const QString &p ) { mSourceRasterPath = p; }

  protected:
    void closeEvent( QCloseEvent *e ) override;

  private slots:
    /// Recompute transform fit + residuals when GCP list changes.
    void recomputeFit();
    /// Wired to the Apply toolbar action — runs a RsWarpTask.
    void applyTransform();

  private:
    void setupMenus();
    void setupToolbars();
    void setupStatusBar();
    void setupCentralWidget();

    /// Emit structured JSON to QgsMessageLog tag "Georeferencer".
    void emitStructuredLog( const QgsImageWarper::WarpResult &r );

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

    // Task 11.4.6 — GCP list + bottom dock
    QgsGCPList *mGcps = nullptr;
    QgsGCPListWidget *mGcpTable = nullptr;
    QDockWidget *mGcpDock = nullptr;

    // Task 11.4.7 — right param dock, transform fit cache, source raster
    RsGeorefParamsPanel *mParamsPanel = nullptr;
    QDockWidget *mParamDock = nullptr;
    QAction *mApplyAction = nullptr;
    std::unique_ptr<QgsGeorefTransform> mTransform;
    double mLastRms = 0.0;
    QString mSourceRasterPath;
};
