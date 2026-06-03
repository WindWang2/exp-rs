#pragma once

#include <QMainWindow>
#include <QString>
#include <QVector>

#include <opencv2/core.hpp>

class QgisInterface;
class QAction;
class QDockWidget;
class QgsGeometry;
class QgsMapCanvas;
class QgsMapLayerStore;
class QgsRasterLayer;
class RsRoiCollection;
class RsClassTableWidget;
class RsClassQuickList;
class RsSpectralCurveWidget;
class RsJmMatrixWidget;
class RsRoiToolPoint;
class RsRoiToolRectangle;
class RsRoiToolPolygon;
class RsRoiToolFreehand;
class RsRoiToolMagicWand;
class RsClassifierSetupBar;

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

  public slots:
    /// Open the file picker and load a raster as the classification source.
    bool openSourceRaster();
    /// Load a raster from a known path. Sets mSourceRasterPath / W / H / Gt,
    /// installs the layer in mCanvas, propagates the path to the magic-wand
    /// tool and seeds the ClassifierBar band picker with the band count.
    bool openSourceRaster( const QString &path );

    /// Apply the configured classifier to the open source raster, writing
    /// the result to the path requested in the ClassifierBar (or a file
    /// picker if blank). Schedules an RsClassificationTask on the global
    /// task manager.
    void applyClassification();

  private:
    void setupMenus();
    void setupToolbars();
    void setupDocks();
    void setupStatusBar();
    void setupRoiTools();
    void setupClassifierBar();
    /// Build a CV_32F NxB sample matrix and CV_32S Nx1 label matrix from the
    /// current ROIs + open source raster. Returns true if there are >= 10
    /// total samples, populates `X`, `y` and `bandsOut` accordingly.
    bool buildTrainingData( const QVector<int> &bands,
                            cv::Mat &X,
                            cv::Mat &y ) const;

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
    RsJmMatrixWidget *mJmMatrix = nullptr;

    // Task 10.4 — ROI map tools (point/rectangle/polygon/freehand).
    RsRoiToolPoint *mToolPoint = nullptr;
    RsRoiToolRectangle *mToolRect = nullptr;
    RsRoiToolPolygon *mToolPolygon = nullptr;
    RsRoiToolFreehand *mToolFreehand = nullptr;

    // Task 10.7 — Magic-wand ROI map tool (L2-tolerance flood fill).
    RsRoiToolMagicWand *mToolMagicWand = nullptr;

    // Source raster metadata for pixel-index rasterization. Empty path means
    // no raster is loaded yet — onRoiDrawn skips rasterization and stores an
    // RsRoi with empty pixel indices (geometry-only, still useful for later
    // re-rasterization once a raster is set in Task 10.5/10.7/10.8).
    QString mSourceRasterPath;
    int mSourceWidth = 0;
    int mSourceHeight = 0;
    int mSourceBandCount = 0;
    double mSourceGt[6] = { 0, 1, 0, 0, 0, -1 };

    // Task 10.8 — ClassifierBar widget mounted in a bottom toolbar.
    RsClassifierSetupBar *mClassifierBar = nullptr;
    QAction *mApplyAction = nullptr;
    QAction *mOpenRasterAction = nullptr;
    // Lifetime owner for QgsRasterLayer instances handed to mCanvas.
    QgsMapLayerStore *mLayerStore = nullptr;
    QgsRasterLayer *mSourceLayer = nullptr; // non-owning

  private slots:
    void onRoiDrawn( const QgsGeometry &geom, int classId );
    void onCurrentClassChanged( int classId );
};
