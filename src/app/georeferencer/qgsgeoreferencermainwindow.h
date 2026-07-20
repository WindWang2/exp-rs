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

    QgsMapCanvas *pickCanvasForMode( RsGeorefModeToggle::Mode m ) const;
    QgsMapCanvas *pickCanvas() const;

  public slots:
    void loadReferenceRaster();
    bool loadReferenceRaster( const QString &path );

  protected:
    QString shellId() const override { return QStringLiteral( "i2i" ); }
    void captureShellSpecific( RsGeorefSessionState::WorkflowSnapshot &s ) const override;
    void applyShellSpecific( const RsGeorefSessionState::WorkflowSnapshot &s ) override;

  private slots:
    void onModeChanged( RsGeorefModeToggle::Mode m );

  private:
    void setupMenus();
    void setupToolbars();
    void setupCentralWidget();

    RsGeorefModeToggle *mModeToggle = nullptr;
    QToolBar *mModeBar = nullptr;
    QAction *mSyncZoomAction = nullptr;
    RsTwinCanvasSyncController *mSyncCtl = nullptr;
    QString mRefRasterPath;
    QgsRasterLayer *mRefRaster = nullptr; // non-owning; in mLayerStore
};
