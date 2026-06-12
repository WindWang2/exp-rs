// qgslayoutdesignerdialog.h — Minimal layout designer implementation
#pragma once

#include <gui/layout/qgslayoutdesignerinterface.h>
#include <QMainWindow>

class QgsLayout;
class QgsLayoutView;
class QgsMessageBar;
class QgsLayoutItem;
class QgsMasterLayoutInterface;

/**
 * Minimal layout designer dialog implementing QgsLayoutDesignerInterface.
 * Provides basic layout editing with map, legend, scale bar, and export.
 */
class QgsLayoutDesignerDialog : public QgsLayoutDesignerInterface
{
    Q_OBJECT

public:
    explicit QgsLayoutDesignerDialog(QgsMasterLayoutInterface *layout, QWidget *parent = nullptr);
    ~QgsLayoutDesignerDialog() override;

    // QgsLayoutDesignerInterface interface
    QgsLayout *layout() override;
    QgsMasterLayoutInterface *masterLayout() override;
    QWidget *window() override { return mWindow; }
    QgsLayoutView *view() override;
    QgsMessageBar *messageBar() override;
    void selectItems(const QList<QgsLayoutItem *> &items) override;
    void setAtlasPreviewEnabled(bool enabled) override;
    bool atlasPreviewEnabled() const override { return false; }
    void setAtlasFeature(const QgsFeature &feature) override;
    void showItemOptions(QgsLayoutItem *item, bool bringPanelToFront = true) override;

    // Menu access
    QMenu *layoutMenu() override;
    QMenu *editMenu() override;
    QMenu *viewMenu() override;
    QMenu *itemsMenu() override;
    QMenu *atlasMenu() override;
    QMenu *reportMenu() override;
    QMenu *settingsMenu() override;

    // Toolbar access
    QToolBar *layoutToolbar() override;
    QToolBar *navigationToolbar() override;
    QToolBar *actionsToolbar() override;
    QToolBar *atlasToolbar() override { return nullptr; }

    // Dock widget management
    void addDockWidget(Qt::DockWidgetArea area, QDockWidget *dock) override;
    void removeDockWidget(QDockWidget *dock) override;

    // Tool activation
    void activateTool(StandardTool tool) override;

    // Export results
    ExportResults *lastExportResults() const override;

public slots:
    void close() override;
    void showRulers(bool visible) override;

private slots:
    void onAddMap();
    void onAddLegend();
    void onAddScaleBar();
    void onAddNorthArrow();
    void onAddLabel();
    void onAddImage();
    void onExportToPdf();
    void onExportToImage();

private:
    void setupUi();
    void setupMenus();
    void setupToolbars();

    QMainWindow *mWindow = nullptr;
    QgsMasterLayoutInterface *mMasterLayout = nullptr;
    QgsLayout *mLayout = nullptr;
    QgsLayoutView *mView = nullptr;
    QgsMessageBar *mMessageBar = nullptr;

    // Menus
    QMenu *mLayoutMenu = nullptr;
    QMenu *mEditMenu = nullptr;
    QMenu *mViewMenu = nullptr;
    QMenu *mItemsMenu = nullptr;
    QMenu *mAtlasMenu = nullptr;
    QMenu *mReportMenu = nullptr;
    QMenu *mSettingsMenu = nullptr;

    // Toolbars
    QToolBar *mLayoutToolbar = nullptr;
    QToolBar *mNavigationToolbar = nullptr;
    QToolBar *mActionsToolbar = nullptr;
};
