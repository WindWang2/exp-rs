#pragma once

#include <QMainWindow>

#include "qgspointxy.h"
#include "rs_georef_mode_toggle.h"

class QToolBar;
class QLabel;
class QAction;
class QCloseEvent;
class QgisInterface;
class QgsMapCanvas;
class RsTwinCanvasSyncController;
class QgsGeorefToolAddPoint;

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

  public slots:
    /// Slot connected to QgsGeorefToolAddPoint::showCoordDialog — pops up
    /// the MapCoords dialog so the user can enter the destination coord.
    void showCoordDialog( const QgsPointXY &sourcePixel );

  protected:
    void closeEvent( QCloseEvent *e ) override;

  private:
    void setupMenus();
    void setupToolbars();
    void setupStatusBar();
    void setupCentralWidget();

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
};
