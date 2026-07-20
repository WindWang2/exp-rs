#pragma once

#include <QHash>
#include <QMainWindow>
#include <memory>

#include "qgsgcppoint.h"
#include "qgspointxy.h"
#include "qgsimagewarper.h"
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
class QgsGeorefToolAddPoint;
class QgsGeorefToolMovePoint;
class QgsGeorefToolDeletePoint;
class QgsGCPList;
class QgsGCPListWidget;
class QgsGeorefDataPoint;
class QgsGeorefTransform;
class RsGeorefParamsPanel;

/**
 * \brief Image Registration · Image 2 Map shell.
 *
 * Vertical splitter: SRC raster (top) + map preview that mirrors the main
 * QgsProject layers (bottom). RPC is available as a transform method on the
 * params panel (not a window-level mode toggle). No SIFT / Open Reference.
 */
class QgsGeorefImageToMapWindow : public QMainWindow
{
    Q_OBJECT

  public:
    explicit QgsGeorefImageToMapWindow( QgisInterface *iface, QWidget *parent = nullptr );
    ~QgsGeorefImageToMapWindow() override;

    QgsMapCanvas *srcCanvas() const { return mSrcCanvas; }
    QgsMapCanvas *mapCanvas() const { return mMapCanvas; }

    bool isDirtyForTest() const;
    void markDirtyForTest();
    void setWarpInProgressForTest( bool on );
    void setSourceRasterPath( const QString &p ) { mSourceRasterPath = p; }

  public slots:
    void openSourceRaster();
    void refreshMapLayersFromProject();
    void applyTransform();
    void showCoordDialog( const QgsPointXY &sourcePixel );

  protected:
    void closeEvent( QCloseEvent *e ) override;

  private slots:
    void recomputeFit();
    void onPointsChanged();
    void onTransformMethodChanged();
    void loadPoints();
    void savePoints();
    void deleteGcpRows( const QList<int> &rows );

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
    void emitStructuredLog( const QgsImageWarper::WarpResult &r );
    void applyWorkflowSnapshot( const RsGeorefSessionState::WorkflowSnapshot &s );
    RsGeorefSessionState::WorkflowSnapshot captureWorkflowSnapshot() const;
    QgsGeorefDataPoint *findDataPoint( const QgsPointXY &p, QgsGcpPoint::PointType type );

    QgisInterface *mIface = nullptr;
    QToolBar *mToolBar = nullptr;
    QLabel *mCoordLabel = nullptr;
    QLabel *mCrsLabel = nullptr;
    QLabel *mRmsLabel = nullptr;

    QgsMapCanvas *mSrcCanvas = nullptr;
    QgsMapCanvas *mMapCanvas = nullptr;
    QgsMapLayerStore *mSrcStore = nullptr;
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
    std::unique_ptr<QgsGeorefTransform> mTransform;
    double mLastRms = 0.0;
    QString mSourceRasterPath;

    RsGeorefSessionState mSession;
    bool mSuppressDirtyFromList = false;
    bool mWarpInProgress = false;
};
