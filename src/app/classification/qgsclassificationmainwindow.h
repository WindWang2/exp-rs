#pragma once

#include <QMainWindow>

class QgisInterface;
class QDockWidget;
class QgsMapCanvas;
class RsRoiCollection;

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

    QgisInterface *mIface = nullptr;
    QgsMapCanvas *mCanvas = nullptr;
    RsRoiCollection *mRois = nullptr;

    QDockWidget *mClassListDock = nullptr;
    QDockWidget *mClassQuickListDock = nullptr;
    QDockWidget *mJmDock = nullptr;
    QDockWidget *mSpectralDock = nullptr;
};
