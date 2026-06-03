#pragma once

#include <QMainWindow>
#include <QString>

class QgisInterface;
class QAction;
class QDockWidget;
class QgsGeometry;
class QgsMapCanvas;
class RsRoiCollection;
class RsClassTableWidget;
class RsClassQuickList;
class RsSpectralCurveWidget;
class RsRoiToolPoint;
class RsRoiToolRectangle;
class RsRoiToolPolygon;
class RsRoiToolFreehand;

/**
 * \brief Phase 10A — Pixel-Based Classification main window shell.
 *
 * Task 10.2: Provides a QMainWindow with a central QgsMapCanvas, an ROI
 * toolbar (point/rectangle/polygon/freehand/magic wand + spectra /
 * separability / export / preview / apply), 4 dock placeholders for the
 * class table, quick list, JM matrix and spectral curve panels, plus a
 * minimal status bar.
 *
 * Subsequent tasks populate the docks (Task 10.3), wire ROI map tools
 * (Task 10.4), JM dock (Task 10.6), spectral dock (Task 10.5) and the
 * Apply action (Task 10.8).
 */
class QgsClassificationMainWindow : public QMainWindow
{
    Q_OBJECT

  public:
    explicit QgsClassificationMainWindow( QgisInterface *iface, QWidget *parent = nullptr );
    ~QgsClassificationMainWindow() override;

  private:
    void setupMenus();
    void setupToolbars();
    void setupDocks();
    void setupStatusBar();
    void setupRoiTools();

    QgisInterface *mIface = nullptr;
    QgsMapCanvas *mCanvas = nullptr;
    RsRoiCollection *mRois = nullptr;

    QDockWidget *mClassListDock = nullptr;
    QDockWidget *mClassQuickListDock = nullptr;
    QDockWidget *mJmDock = nullptr;
    QDockWidget *mSpectralDock = nullptr;

    RsClassTableWidget *mClassTableWidget = nullptr;
    RsClassQuickList *mClassQuickListWidget = nullptr;
    RsSpectralCurveWidget *mSpectralCurve = nullptr;

    // Task 10.4 — ROI map tools (point/rectangle/polygon/freehand).
    RsRoiToolPoint *mToolPoint = nullptr;
    RsRoiToolRectangle *mToolRect = nullptr;
    RsRoiToolPolygon *mToolPolygon = nullptr;
    RsRoiToolFreehand *mToolFreehand = nullptr;

    // Source raster metadata for pixel-index rasterization. Empty path means
    // no raster is loaded yet — onRoiDrawn skips rasterization and stores an
    // RsRoi with empty pixel indices (geometry-only, still useful for later
    // re-rasterization once a raster is set in Task 10.5/10.7/10.8).
    QString mSourceRasterPath;
    int mSourceWidth = 0;
    int mSourceHeight = 0;
    double mSourceGt[6] = { 0, 1, 0, 0, 0, -1 };

  private slots:
    void onRoiDrawn( const QgsGeometry &geom, int classId );
    void onCurrentClassChanged( int classId );
};
