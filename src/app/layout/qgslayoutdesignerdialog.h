/***************************************************************************
 * qgslayoutdesignerdialog.h  —  Layout designer (print composer)
 ***************************************************************************/
#pragma once

#include <gui/layout/qgslayoutdesignerinterface.h>
#include <QMainWindow>
#include <QPointer>

class QgsLayout;
class QgsLayoutView;
class QgsLayoutRuler;
class QgsMessageBar;
class QgsLayoutItem;
class QgsLayoutItemMap;
class QgsMasterLayoutInterface;
class QgsMapCanvas;
class QgsLayoutViewToolSelect;
class QgsLayoutViewToolPan;
class QgsLayoutViewToolZoom;
class QgsLayoutViewToolMoveItemContent;
class QgsPanelWidgetStack;
class QDockWidget;
class QUndoView;
class QUndoStack;

/**
 * Layout designer implementing QgsLayoutDesignerInterface.
 *
 * Provides a print-composer window with: map items linked to the current map
 * canvas extent/layers, auto-linked legend / scale bar / north arrow, map grids
 * with coordinate annotations, rulers, and interactive select/pan/zoom tools.
 * Export to PDF / image / SVG is supported via QgsLayoutExporter.
 *
 * Item property editing pipeline:
 *
 *   canvas selection (view / select tool)
 *       → itemFocused
 *       → showItemOptions()
 *       → QgsLayoutItemGuiRegistry::createItemWidget()  (per-type item widget)
 *       → widget writes through QgsLayoutItem setters + QgsLayoutUndoStack
 *
 * The real QgsLayoutItem is the single source of truth: the property panel is
 * only a view/editor, and interactive drags push the same undo commands as
 * panel edits, so the panel and the scene stay synchronized in both
 * directions.
 */
class QgsLayoutDesignerDialog : public QgsLayoutDesignerInterface
{
    Q_OBJECT

public:
    explicit QgsLayoutDesignerDialog(QgsMasterLayoutInterface *layout,
                                     QgsMapCanvas *canvas,
                                     QWidget *parent = nullptr);
    ~QgsLayoutDesignerDialog() override;

    // QgsLayoutDesignerInterface interface
    QgsLayout *layout() override;
    QgsMasterLayoutInterface *masterLayout() override;
    QWidget *window() override { return mWindow.data(); }
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
    void onAddGrid();
    void onAddLabel();
    void onAddImage();
    void onAddShape();
    void onAddChart();
    void onExportToPdf();
    void onExportToImage();
    void onExportToSvg();
    void onDeleteSelectedItems();
    void onDuplicateSelectedItems();
    void onSelectAllItems();
    void onDeselectAllItems();
    void onZoomToPage();

    // Stacking / locking
    void onRaiseItems();
    void onLowerItems();
    void onBringToFront();
    void onSendToBack();
    void onLockItems(bool locked);

    // Page / template
    void onShowPageProperties();
    void onAutoArrange();
    void onSaveAsTemplate();
    void onLoadFromTemplate();

private:
    void setupUi();
    void setupMenus();
    void setupToolbars();
    void setupItemPropertiesPanel();
    void setupUndoRedo();
    void connectSelectionToInspector();
    void clearItemPanel();

    QWidget *buildMultiSelectionPanel(const QList<QgsLayoutItem *> &items);

    // Clamps a desired item rect (mm) onto the current page so newly added
    // items never land off-page on small formats.
    QRectF defaultItemRect(double x, double y, double width, double height) const;

    QPointer<QMainWindow> mWindow;
    QgsMasterLayoutInterface *mMasterLayout = nullptr;
    QPointer<QgsLayout> mLayout;  // QPointer: layouts can be removed from the project while open
    QgsLayoutView *mView = nullptr;
    QgsMessageBar *mMessageBar = nullptr;
    QgsMapCanvas *mCanvas = nullptr;

    // Track the most recently added map item for legend/scalebar/grid linkage.
    QPointer<QgsLayoutItemMap> mMapItem;

    // Rulers
    QgsLayoutRuler *mHorizontalRuler = nullptr;
    QgsLayoutRuler *mVerticalRuler = nullptr;

    // Interaction tools
    QgsLayoutViewToolSelect *mSelectTool = nullptr;
    QgsLayoutViewToolPan *mPanTool = nullptr;
    QgsLayoutViewToolZoom *mZoomTool = nullptr;
    QgsLayoutViewToolMoveItemContent *mMoveContentTool = nullptr;

    // Item property inspector (single source of truth stays on the item).
    // QPointer: children of mWindow die with the window, and mCurrentItem is
    // cleared by Qt when the bound item is destroyed.
    QPointer<QDockWidget> mItemPropertiesDock;
    QPointer<QgsPanelWidgetStack> mItemsStack;
    QPointer<QgsLayoutItem> mCurrentItem;
    QMetaObject::Connection mItemDestroyedConnection;

    // Undo / redo
    QDockWidget *mUndoDock = nullptr;
    QUndoView *mUndoView = nullptr;
    QAction *mActionUndo = nullptr;
    QAction *mActionRedo = nullptr;

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
