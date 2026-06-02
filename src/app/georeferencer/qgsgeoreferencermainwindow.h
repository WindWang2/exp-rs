#pragma once

#include <QMainWindow>

#include "rs_georef_mode_toggle.h"

class QToolBar;
class QLabel;
class QCloseEvent;
class QgisInterface;

/**
 * \brief Georeferencer main window shell (Task 11.4.4).
 *
 * Skeleton only: menus, top toolbar (mode toggle + GCP/view ops + Apply),
 * status bar (coord / CRS / RMS), placeholder central widget. Twin canvas
 * comes in Task 11.4.5; GCP table in Task 11.4.6.
 */
class QgsGeoreferencerMainWindow : public QMainWindow
{
    Q_OBJECT

  public:
    explicit QgsGeoreferencerMainWindow( QgisInterface *iface, QWidget *parent = nullptr );

  protected:
    void closeEvent( QCloseEvent *e ) override;

  private:
    void setupMenus();
    void setupToolbars();
    void setupStatusBar();

    QgisInterface *mIface = nullptr;
    RsGeorefModeToggle *mModeToggle = nullptr;
    QToolBar *mModeBar = nullptr;
    QLabel *mCoordLabel = nullptr;
    QLabel *mCrsLabel = nullptr;
    QLabel *mRmsLabel = nullptr;
};
