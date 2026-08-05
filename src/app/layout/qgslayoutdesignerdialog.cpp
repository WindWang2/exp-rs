/***************************************************************************
 * qgslayoutdesignerdialog.cpp  —  Layout designer (print composer)
 ***************************************************************************/
#include "qgslayoutdesignerdialog.h"
#include "core/sicnu_logging.h"

#include <qgslayout.h>
#include <qgslayoutview.h>
#include <qgslayoutruler.h>
#include <qgslayoutitem.h>
#include <qgslayoutitemmap.h>
#include <qgslayoutitemmapgrid.h>
#include <qgslayoutitemlegend.h>
#include <qgslayoutitemscalebar.h>
#include <qgslayoutitempicture.h>
#include <qgslayoutitemlabel.h>
#include <qgslayoutexporter.h>
#include <qgslayoutnortharrowhandler.h>
#include <qgsmasterlayoutinterface.h>
#include <qgsmessagebar.h>
#include <qgsproject.h>
#include <qgsmapcanvas.h>
#include <qgsmapsettings.h>
#include <qgsrectangle.h>

#include <gui/layout/qgslayoutviewtoolselect.h>
#include <gui/layout/qgslayoutviewtoolpan.h>
#include <gui/layout/qgslayoutviewtoolzoom.h>
#include <gui/layout/qgslayoutviewtoolmoveitemcontent.h>

#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QDockWidget>
#include <QFileDialog>
#include <QGridLayout>
#include <QMessageBox>
#include <QActionGroup>
#include <QAction>

QgsLayoutDesignerDialog::QgsLayoutDesignerDialog(QgsMasterLayoutInterface *layout,
                                                 QgsMapCanvas *canvas,
                                                 QWidget *parent)
    : QgsLayoutDesignerInterface(parent)
    , mMasterLayout(layout)
    , mCanvas(canvas)
{
    SICNU_LOG_INFO(SicnuLogTags::Layout, "Layout Designer opened");

    // Create the window as a top-level window (not embedded in the main window).
    mWindow = new QMainWindow(nullptr, Qt::Window);

    // QgsMasterLayoutInterface is also a QgsLayout (via QgsPrintLayout).
    mLayout = dynamic_cast<QgsLayout *>(layout);

    setupUi();
    setupMenus();
    setupToolbars();

    mWindow->setWindowTitle(tr("Layout Designer"));
    mWindow->resize(1200, 800);
}

QgsLayoutDesignerDialog::~QgsLayoutDesignerDialog()
{
    // mWindow is top-level (no Qt parent), so we own it and must delete it.
    // With WA_DeleteOnClose set by the caller, mWindow may already be destroyed
    // when the destructor runs; guard with a null check.
    if (mWindow) {
        mWindow->deleteLater();
        mWindow = nullptr;
    }
}

QgsLayout *QgsLayoutDesignerDialog::layout() { return mLayout; }
QgsMasterLayoutInterface *QgsLayoutDesignerDialog::masterLayout() { return mMasterLayout; }
QgsLayoutView *QgsLayoutDesignerDialog::view() { return mView; }
QgsMessageBar *QgsLayoutDesignerDialog::messageBar() { return mMessageBar; }

void QgsLayoutDesignerDialog::selectItems(const QList<QgsLayoutItem *> &items)
{
    if (!mLayout) return;
    // Clear current selection then select the specified items.
    const QList<QgsLayoutItem *> current = mLayout->selectedLayoutItems();
    for (QgsLayoutItem *item : current)
        item->setSelected(false);
    for (QgsLayoutItem *item : items) {
        if (item) item->setSelected(true);
    }
}

void QgsLayoutDesignerDialog::setAtlasPreviewEnabled(bool enabled) { Q_UNUSED(enabled); }
void QgsLayoutDesignerDialog::setAtlasFeature(const QgsFeature &feature) { Q_UNUSED(feature); }
void QgsLayoutDesignerDialog::showItemOptions(QgsLayoutItem *item, bool bringPanelToFront)
{
    Q_UNUSED(item);
    Q_UNUSED(bringPanelToFront);
}

QMenu *QgsLayoutDesignerDialog::layoutMenu() { return mLayoutMenu; }
QMenu *QgsLayoutDesignerDialog::editMenu() { return mEditMenu; }
QMenu *QgsLayoutDesignerDialog::viewMenu() { return mViewMenu; }
QMenu *QgsLayoutDesignerDialog::itemsMenu() { return mItemsMenu; }
QMenu *QgsLayoutDesignerDialog::atlasMenu() { return mAtlasMenu; }
QMenu *QgsLayoutDesignerDialog::reportMenu() { return mReportMenu; }
QMenu *QgsLayoutDesignerDialog::settingsMenu() { return mSettingsMenu; }

QToolBar *QgsLayoutDesignerDialog::layoutToolbar() { return mLayoutToolbar; }
QToolBar *QgsLayoutDesignerDialog::navigationToolbar() { return mNavigationToolbar; }
QToolBar *QgsLayoutDesignerDialog::actionsToolbar() { return mActionsToolbar; }

void QgsLayoutDesignerDialog::addDockWidget(Qt::DockWidgetArea area, QDockWidget *dock)
{
    if (mWindow) mWindow->addDockWidget(area, dock);
}

void QgsLayoutDesignerDialog::removeDockWidget(QDockWidget *dock)
{
    if (mWindow) mWindow->removeDockWidget(dock);
}

void QgsLayoutDesignerDialog::activateTool(StandardTool tool)
{
    // StandardTool values from the QGIS interface contract:
    // ToolMoveItemContent and ToolMoveItemNodes. Select/Pan/Zoom are activated
    // directly from the navigation toolbar lambdas (they are not StandardTool values).
    if (!mView) return;
    if (tool == ToolMoveItemContent && mMoveContentTool) {
        mView->setTool(mMoveContentTool);
    }
    // ToolMoveItemNodes would require QgsLayoutViewToolEditNodes; not yet wired
    // since the designer doesn't expose node editing in its current scope.
}

QgsLayoutDesignerInterface::ExportResults *QgsLayoutDesignerDialog::lastExportResults() const
{
    return nullptr;
}

void QgsLayoutDesignerDialog::close()
{
    if (mWindow) mWindow->close();
}

void QgsLayoutDesignerDialog::showRulers(bool visible)
{
    if (mHorizontalRuler) mHorizontalRuler->setVisible(visible);
    if (mVerticalRuler) mVerticalRuler->setVisible(visible);
}

// ---------------------------------------------------------------------------
// UI setup
// ---------------------------------------------------------------------------

void QgsLayoutDesignerDialog::setupUi()
{
    // Central widget container: rulers (top + left) + layout view.
    auto *central = new QWidget(mWindow);
    auto *grid = new QGridLayout(central);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(0);

    mView = new QgsLayoutView(central);

    // Bind the view to the layout — critical: without this the canvas is blank.
    if (mLayout)
        mView->setCurrentLayout(mLayout);

    // Rulers.
    mHorizontalRuler = new QgsLayoutRuler(central, Qt::Horizontal);
    mVerticalRuler = new QgsLayoutRuler(central, Qt::Vertical);
    mView->setHorizontalRuler(mHorizontalRuler);
    mView->setVerticalRuler(mVerticalRuler);

    // Layout: corner spacer + horizontal ruler on top, vertical ruler + view.
    auto *corner = new QWidget(central);
    corner->setFixedWidth(mVerticalRuler->sizeHint().width());
    corner->setFixedHeight(mHorizontalRuler->sizeHint().height());

    grid->addWidget(corner, 0, 0);
    grid->addWidget(mHorizontalRuler, 0, 1);
    grid->addWidget(mVerticalRuler, 1, 0);
    grid->addWidget(mView, 1, 1);

    mWindow->setCentralWidget(central);

    // Message bar.
    mMessageBar = new QgsMessageBar(mWindow);
    mMessageBar->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    // Embed message bar at the top of the window via a toolbar-like container.
    auto *barContainer = new QToolBar(mWindow);
    barContainer->setMovable(false);
    barContainer->addWidget(mMessageBar);
    mWindow->addToolBar(Qt::TopToolBarArea, barContainer);
}

void QgsLayoutDesignerDialog::setupMenus()
{
    QMenuBar *menuBar = mWindow->menuBar();

    mLayoutMenu = menuBar->addMenu(tr("&Layout"));
    mLayoutMenu->addAction(tr("Export to PDF..."), this, &QgsLayoutDesignerDialog::onExportToPdf);
    mLayoutMenu->addAction(tr("Export to Image..."), this, &QgsLayoutDesignerDialog::onExportToImage);
    mLayoutMenu->addSeparator();
    mLayoutMenu->addAction(tr("Close"), this, &QgsLayoutDesignerDialog::close);

    mEditMenu = menuBar->addMenu(tr("&Edit"));
    mEditMenu->addAction(tr("Delete Selected Items"), this,
                         &QgsLayoutDesignerDialog::onDeleteSelectedItems);

    mViewMenu = menuBar->addMenu(tr("&View"));
    mViewMenu->addAction(tr("Zoom to Page"), this, &QgsLayoutDesignerDialog::onZoomToPage);

    mItemsMenu = menuBar->addMenu(tr("&Items"));
    mItemsMenu->addAction(tr("Add Map"), this, &QgsLayoutDesignerDialog::onAddMap);
    mItemsMenu->addAction(tr("Add Legend"), this, &QgsLayoutDesignerDialog::onAddLegend);
    mItemsMenu->addAction(tr("Add Scale Bar"), this, &QgsLayoutDesignerDialog::onAddScaleBar);
    mItemsMenu->addAction(tr("Add North Arrow"), this, &QgsLayoutDesignerDialog::onAddNorthArrow);
    mItemsMenu->addAction(tr("Add Grid"), this, &QgsLayoutDesignerDialog::onAddGrid);
    mItemsMenu->addSeparator();
    mItemsMenu->addAction(tr("Add Label"), this, &QgsLayoutDesignerDialog::onAddLabel);
    mItemsMenu->addAction(tr("Add Image"), this, &QgsLayoutDesignerDialog::onAddImage);

    mAtlasMenu = menuBar->addMenu(tr("&Atlas"));
    mReportMenu = menuBar->addMenu(tr("&Report"));
    mSettingsMenu = menuBar->addMenu(tr("&Settings"));
}

void QgsLayoutDesignerDialog::setupToolbars()
{
    // Navigation toolbar with select / pan / zoom tools.
    mNavigationToolbar = mWindow->addToolBar(tr("Navigation"));

    // Create the interaction tools.
    mSelectTool = new QgsLayoutViewToolSelect(mView);
    mPanTool = new QgsLayoutViewToolPan(mView);
    mZoomTool = new QgsLayoutViewToolZoom(mView);
    mMoveContentTool = new QgsLayoutViewToolMoveItemContent(mView);

    auto *toolGroup = new QActionGroup(mNavigationToolbar);

    auto *selectAction = mNavigationToolbar->addAction(tr("Select"));
    selectAction->setCheckable(true);
    selectAction->setToolTip(tr("Select / move items"));
    selectAction->setActionGroup(toolGroup);
    connect(selectAction, &QAction::triggered, this, [this]() {
        if (mSelectTool && mView) mView->setTool(mSelectTool);
    });

    auto *panAction = mNavigationToolbar->addAction(tr("Pan"));
    panAction->setCheckable(true);
    panAction->setToolTip(tr("Pan layout view"));
    panAction->setActionGroup(toolGroup);
    connect(panAction, &QAction::triggered, this, [this]() {
        if (mPanTool && mView) mView->setTool(mPanTool);
    });

    auto *zoomAction = mNavigationToolbar->addAction(tr("Zoom"));
    zoomAction->setCheckable(true);
    zoomAction->setToolTip(tr("Zoom in / out"));
    zoomAction->setActionGroup(toolGroup);
    connect(zoomAction, &QAction::triggered, this, [this]() {
        if (mZoomTool && mView) mView->setTool(mZoomTool);
    });

    selectAction->setChecked(true);
    if (mSelectTool && mView) mView->setTool(mSelectTool);

    // Actions toolbar with item shortcuts.
    mActionsToolbar = mWindow->addToolBar(tr("Items"));
    mActionsToolbar->addAction(tr("Map"), this, &QgsLayoutDesignerDialog::onAddMap);
    mActionsToolbar->addAction(tr("Legend"), this, &QgsLayoutDesignerDialog::onAddLegend);
    mActionsToolbar->addAction(tr("Scale"), this, &QgsLayoutDesignerDialog::onAddScaleBar);
    mActionsToolbar->addAction(tr("North"), this, &QgsLayoutDesignerDialog::onAddNorthArrow);
    mActionsToolbar->addAction(tr("Grid"), this, &QgsLayoutDesignerDialog::onAddGrid);

    // Layout (export) toolbar.
    mLayoutToolbar = mWindow->addToolBar(tr("Export"));
    mLayoutToolbar->addAction(tr("PDF..."), this, &QgsLayoutDesignerDialog::onExportToPdf);
    mLayoutToolbar->addAction(tr("Image..."), this, &QgsLayoutDesignerDialog::onExportToImage);
}

// ---------------------------------------------------------------------------
// Item creation slots
// ---------------------------------------------------------------------------

void QgsLayoutDesignerDialog::onAddMap()
{
    if (!mLayout) return;

    auto *map = new QgsLayoutItemMap(mLayout);
    map->setRect(20, 20, 200, 150);

    // Link to the current map canvas extent + rotation.
    if (mCanvas) {
        map->zoomToExtent(mCanvas->mapSettings().visibleExtent());
        map->setMapRotation(mCanvas->rotation());
    }

    mLayout->addLayoutItem(map);
    mMapItem = map;  // track for legend/scalebar/grid linkage

    if (mWindow) mWindow->statusBar()->showMessage(tr("Map added (linked to canvas)"), 3000);
}

void QgsLayoutDesignerDialog::onAddLegend()
{
    if (!mLayout) return;

    if (!mMapItem) {
        if (mMessageBar)
            mMessageBar->pushInfo(tr("Legend"), tr("Add a map item first."));
        return;
    }

    auto *legend = new QgsLayoutItemLegend(mLayout);
    legend->setLinkedMap(mMapItem);
    legend->setTitle(tr("图例"));
    legend->setRect(230, 20, 80, 100);
    mLayout->addLayoutItem(legend);
    legend->update();
    legend->adjustBoxSize();

    if (mWindow) mWindow->statusBar()->showMessage(tr("Legend added (linked to map)"), 3000);
}

void QgsLayoutDesignerDialog::onAddScaleBar()
{
    if (!mLayout) return;

    if (!mMapItem) {
        if (mMessageBar)
            mMessageBar->pushInfo(tr("Scale Bar"), tr("Add a map item first."));
        return;
    }

    auto *scaleBar = new QgsLayoutItemScaleBar(mLayout);
    scaleBar->setLinkedMap(mMapItem);
    scaleBar->setRect(20, 180, 80, 15);
    mLayout->addLayoutItem(scaleBar);
    scaleBar->applyDefaultSettings();
    scaleBar->update();

    if (mWindow) mWindow->statusBar()->showMessage(tr("Scale bar added (linked to map)"), 3000);
}

void QgsLayoutDesignerDialog::onAddNorthArrow()
{
    if (!mLayout) return;

    if (!mMapItem) {
        if (mMessageBar)
            mMessageBar->pushInfo(tr("North Arrow"), tr("Add a map item first."));
        return;
    }

    auto *picture = new QgsLayoutItemPicture(mLayout);
    picture->setPicturePath(QStringLiteral(":/north_arrows/default.svg"));
    picture->setLinkedMap(mMapItem);
    picture->setNorthMode(QgsLayoutItemPicture::GridNorth);
    picture->setRect(230, 130, 30, 30);
    mLayout->addLayoutItem(picture);

    if (mWindow) mWindow->statusBar()->showMessage(tr("North arrow added (linked to map)"), 3000);
}

void QgsLayoutDesignerDialog::onAddGrid()
{
    if (!mLayout || !mMapItem) {
        if (mMessageBar)
            mMessageBar->pushInfo(tr("Grid"), tr("Add a map item first."));
        return;
    }

    auto *grid = new QgsLayoutItemMapGrid(tr("Grid 1"), mMapItem);
    grid->setEnabled(true);
    grid->setUnits(Qgis::MapGridUnit::MapUnits);
    grid->setStyle(Qgis::MapGridStyle::Lines);
    grid->setGridLineColor(QColor(100, 100, 100, 128));

    // Estimate a reasonable grid interval from the map extent.
    const QgsRectangle extent = mMapItem->extent();
    const double rangeX = extent.xMaximum() - extent.xMinimum();
    const double rangeY = extent.yMaximum() - extent.yMinimum();
    // Target ~10 grid lines; round to a nice number (1/2/5 × 10^n).
    const double rawIntervalX = rangeX / 10.0;
    const double rawIntervalY = rangeY / 10.0;
    const auto niceInterval = [](double raw) -> double {
        if (raw <= 0) return 1.0;
        const double mag = std::pow(10.0, std::floor(std::log10(raw)));
        const double norm = raw / mag;
        double nice;
        if (norm < 1.5) nice = 1.0;
        else if (norm < 3.0) nice = 2.0;
        else if (norm < 7.0) nice = 5.0;
        else nice = 10.0;
        return nice * mag;
    };
    grid->setIntervalX(niceInterval(rawIntervalX));
    grid->setIntervalY(niceInterval(rawIntervalY));

    // Coordinate annotations.
    grid->setAnnotationEnabled(true);
    grid->setAnnotationPrecision(2);

    mMapItem->grids()->addGrid(grid);
    mMapItem->updateBoundingRect();
    mMapItem->update();

    if (mWindow) mWindow->statusBar()->showMessage(tr("Grid added"), 3000);
}

void QgsLayoutDesignerDialog::onAddLabel()
{
    if (!mLayout) return;

    auto *label = new QgsLayoutItemLabel(mLayout);
    label->setText(tr("地图标题"));
    label->setRect(20, 0, 150, 18);
    mLayout->addLayoutItem(label);

    if (mWindow) mWindow->statusBar()->showMessage(tr("Label added"), 3000);
}

void QgsLayoutDesignerDialog::onAddImage()
{
    if (!mLayout || !mWindow) return;

    QString filePath = QFileDialog::getOpenFileName(mWindow, tr("Select Image"), QString(),
                                                    tr("Images (*.png *.jpg *.svg)"));
    if (filePath.isEmpty()) return;

    auto *picture = new QgsLayoutItemPicture(mLayout);
    picture->setPicturePath(filePath);
    picture->setRect(230, 170, 60, 60);
    mLayout->addLayoutItem(picture);

    mWindow->statusBar()->showMessage(tr("Image added"), 3000);
}

void QgsLayoutDesignerDialog::onDeleteSelectedItems()
{
    if (!mLayout) return;

    const QList<QgsLayoutItem *> selected = mLayout->selectedLayoutItems();
    if (selected.isEmpty()) return;

    for (QgsLayoutItem *item : selected) {
        // Clear mMapItem if the tracked map is being deleted.
        if (item == mMapItem)
            mMapItem = nullptr;
        mLayout->removeLayoutItem(item);
    }

    if (mWindow) mWindow->statusBar()->showMessage(tr("Deleted %1 item(s)").arg(selected.size()), 3000);
}

void QgsLayoutDesignerDialog::onZoomToPage()
{
    if (mView && mLayout)
        mView->zoomWidth();
}

// ---------------------------------------------------------------------------
// Export
// ---------------------------------------------------------------------------

void QgsLayoutDesignerDialog::onExportToPdf()
{
    if (!mLayout || !mWindow) return;

    QString filePath = QFileDialog::getSaveFileName(mWindow, tr("Export to PDF"), QString(),
                                                    tr("PDF (*.pdf)"));
    if (filePath.isEmpty()) return;

    QgsLayoutExporter exporter(mLayout);
    QgsLayoutExporter::PdfExportSettings settings;
    settings.dpi = 300.0;
    QgsLayoutExporter::ExportResult result = exporter.exportToPdf(filePath, settings);

    if (result == QgsLayoutExporter::Success) {
        mWindow->statusBar()->showMessage(tr("Exported to %1").arg(filePath), 5000);
        emit layoutExported();
    } else {
        QMessageBox::warning(mWindow, tr("Export Failed"),
                             tr("Failed to export to PDF: %1").arg(exporter.errorMessage()));
    }
}

void QgsLayoutDesignerDialog::onExportToImage()
{
    if (!mLayout || !mWindow) return;

    QString filePath = QFileDialog::getSaveFileName(mWindow, tr("Export to Image"), QString(),
                                                    tr("PNG (*.png);;JPEG (*.jpg)"));
    if (filePath.isEmpty()) return;

    QgsLayoutExporter exporter(mLayout);
    QgsLayoutExporter::ImageExportSettings settings;
    settings.dpi = 300.0;
    QgsLayoutExporter::ExportResult result = exporter.exportToImage(filePath, settings);

    if (result == QgsLayoutExporter::Success) {
        mWindow->statusBar()->showMessage(tr("Exported to %1").arg(filePath), 5000);
        emit layoutExported();
    } else {
        QMessageBox::warning(mWindow, tr("Export Failed"),
                             tr("Failed to export to image: %1").arg(exporter.errorMessage()));
    }
}
