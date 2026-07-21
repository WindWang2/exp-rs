#pragma once

#include "qgsgeoref_shell_window.h"

/**
 * Image Registration · Image 2 Map shell (QGIS-style).
 *
 * Single source-image canvas only (no embedded base/map panel).
 * GCP destination: type X/Y in the table or MapCoords dialog, or pick from
 * the main application map canvas ("从地图取点").
 */
class QgsGeorefImageToMapWindow : public QgsGeorefShellWindow
{
    Q_OBJECT

  public:
    explicit QgsGeorefImageToMapWindow( QgisInterface *iface, QWidget *parent = nullptr );

    /// I2M has no dest canvas; kept for API compatibility (always null).
    QgsMapCanvas *mapCanvas() const { return mDstCanvas; }
    bool usesMapCoordsDialogForGcp() const override { return true; }

  public slots:
    void refreshMapLayersFromProject();

  protected:
    QString shellId() const override { return QStringLiteral( "i2m" ); }
    QString windowHelpText() const override;
    void onTransformMethodChangedExtra() override;
    void captureShellSpecific( RsGeorefSessionState::WorkflowSnapshot &s ) const override;
    bool hasDestReady() const override;
    void updateToolAvailability() override;

  private:
    void setupMenus();
    void setupToolbars();
    void setupCentralWidget();

    QToolBar *mToolBar = nullptr;
};
