#pragma once

#include "qgsgeoref_shell_window.h"
#include "rs_georef_mode_toggle.h"

class RsTwinCanvasSyncController;
class QgsRasterLayer;

/**
 * Image Registration · Image 2 Image shell.
 * Horizontal SRC | REF canvases, SIFT match, no RPC method/DEM.
 */
class QgsGeoreferencerMainWindow : public QgsGeorefShellWindow
{
    Q_OBJECT

  public:
    explicit QgsGeoreferencerMainWindow( QgisInterface *iface, QWidget *parent = nullptr );

    /// Destination canvas for MapCoords pick (always REF on I2I).
    QgsMapCanvas *pickCanvas() const { return mDstCanvas; }
    /// Compatibility with older tests: mode argument is ignored (I2I is fixed).
    QgsMapCanvas *pickCanvasForMode( RsGeorefModeToggle::Mode ) const { return mDstCanvas; }

  public slots:
    void loadReferenceRaster();
    bool loadReferenceRaster( const QString &path );

  protected:
    QString shellId() const override { return QStringLiteral( "i2i" ); }
    QString windowHelpText() const override;
    void captureShellSpecific( RsGeorefSessionState::WorkflowSnapshot &s ) const override;
    void applyShellSpecific( const RsGeorefSessionState::WorkflowSnapshot &s ) override;

  private slots:
    void runSiftMatch();

  private:
    void setupMenus();
    void setupToolbars();
    void setupCentralWidget();

    QToolBar *mToolBar = nullptr;
    QAction *mSyncZoomAction = nullptr;
    RsTwinCanvasSyncController *mSyncCtl = nullptr;
    QString mRefRasterPath;
    QgsRasterLayer *mRefRaster = nullptr; // non-owning; in mLayerStore
};
