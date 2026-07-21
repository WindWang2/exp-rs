#pragma once

#include "qgsgeoref_shell_window.h"

/**
 * Image Registration · Image 2 Map shell.
 * Vertical SRC | Map (project layers), RPC as transform method, no SIFT.
 */
class QgsGeorefImageToMapWindow : public QgsGeorefShellWindow
{
    Q_OBJECT

  public:
    explicit QgsGeorefImageToMapWindow( QgisInterface *iface, QWidget *parent = nullptr );

    QgsMapCanvas *mapCanvas() const { return mDstCanvas; }

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
