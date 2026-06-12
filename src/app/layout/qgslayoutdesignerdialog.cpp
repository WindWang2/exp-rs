// qgslayoutdesignerdialog.cpp — Minimal layout designer implementation
#include "qgslayoutdesignerdialog.h"

#include <qgslayout.h>
#include <qgslayoutview.h>
#include <qgslayoutitem.h>
#include <qgslayoutitemmap.h>
#include <qgslayoutitemlegend.h>
#include <qgslayoutitemscalebar.h>
#include <qgslayoutitempicture.h>
#include <qgslayoutitemlabel.h>
#include <qgslayoutexporter.h>
#include <qgsmasterlayoutinterface.h>
#include <qgsmessagebar.h>
#include <qgsproject.h>

#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QDockWidget>
#include <QFileDialog>
#include <QMessageBox>

QgsLayoutDesignerDialog::QgsLayoutDesignerDialog(QgsMasterLayoutInterface *layout, QWidget *parent)
    : QgsLayoutDesignerInterface(parent)
    , mMasterLayout(layout)
{
    // Create a separate QMainWindow for the UI
    mWindow = new QMainWindow(parent);

    // QgsMasterLayoutInterface is also a QgsLayout (via QgsPrintLayout)
    mLayout = dynamic_cast<QgsLayout *>(layout);

    setupUi();
    setupMenus();
    setupToolbars();

    mWindow->setWindowTitle(tr("Layout Designer"));
    mWindow->resize(1200, 800);
}

QgsLayoutDesignerDialog::~QgsLayoutDesignerDialog()
{
    delete mWindow;
}

QgsLayout *QgsLayoutDesignerDialog::layout()
{
    return mLayout;
}

QgsMasterLayoutInterface *QgsLayoutDesignerDialog::masterLayout()
{
    return mMasterLayout;
}

QgsLayoutView *QgsLayoutDesignerDialog::view()
{
    return mView;
}

QgsMessageBar *QgsLayoutDesignerDialog::messageBar()
{
    return mMessageBar;
}

void QgsLayoutDesignerDialog::selectItems(const QList<QgsLayoutItem *> &items)
{
    Q_UNUSED(items);
}

void QgsLayoutDesignerDialog::setAtlasPreviewEnabled(bool enabled)
{
    Q_UNUSED(enabled);
}

void QgsLayoutDesignerDialog::setAtlasFeature(const QgsFeature &feature)
{
    Q_UNUSED(feature);
}

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
    Q_UNUSED(tool);
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
    Q_UNUSED(visible);
}

void QgsLayoutDesignerDialog::setupUi()
{
    mView = new QgsLayoutView(mWindow);
    mWindow->setCentralWidget(mView);

    mMessageBar = new QgsMessageBar(mWindow);
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
    mViewMenu = menuBar->addMenu(tr("&View"));

    mItemsMenu = menuBar->addMenu(tr("&Items"));
    mItemsMenu->addAction(tr("Add Map"), this, &QgsLayoutDesignerDialog::onAddMap);
    mItemsMenu->addAction(tr("Add Legend"), this, &QgsLayoutDesignerDialog::onAddLegend);
    mItemsMenu->addAction(tr("Add Scale Bar"), this, &QgsLayoutDesignerDialog::onAddScaleBar);
    mItemsMenu->addAction(tr("Add North Arrow"), this, &QgsLayoutDesignerDialog::onAddNorthArrow);
    mItemsMenu->addAction(tr("Add Label"), this, &QgsLayoutDesignerDialog::onAddLabel);
    mItemsMenu->addAction(tr("Add Image"), this, &QgsLayoutDesignerDialog::onAddImage);

    mAtlasMenu = menuBar->addMenu(tr("&Atlas"));
    mReportMenu = menuBar->addMenu(tr("&Report"));
    mSettingsMenu = menuBar->addMenu(tr("&Settings"));
}

void QgsLayoutDesignerDialog::setupToolbars()
{
    mLayoutToolbar = mWindow->addToolBar(tr("Layout"));
    mNavigationToolbar = mWindow->addToolBar(tr("Navigation"));
    mActionsToolbar = mWindow->addToolBar(tr("Actions"));
}

void QgsLayoutDesignerDialog::onAddMap()
{
    if (!mLayout) return;

    QgsLayoutItemMap *map = new QgsLayoutItemMap(mLayout);
    map->setRect(20, 20, 200, 150);
    mLayout->addLayoutItem(map);

    if (mWindow) mWindow->statusBar()->showMessage(tr("Map added"), 3000);
}

void QgsLayoutDesignerDialog::onAddLegend()
{
    if (!mLayout) return;

    QgsLayoutItemLegend *legend = new QgsLayoutItemLegend(mLayout);
    legend->setRect(230, 20, 60, 80);
    mLayout->addLayoutItem(legend);

    if (mWindow) mWindow->statusBar()->showMessage(tr("Legend added"), 3000);
}

void QgsLayoutDesignerDialog::onAddScaleBar()
{
    if (!mLayout) return;

    QgsLayoutItemScaleBar *scaleBar = new QgsLayoutItemScaleBar(mLayout);
    scaleBar->setRect(20, 180, 60, 10);
    mLayout->addLayoutItem(scaleBar);

    if (mWindow) mWindow->statusBar()->showMessage(tr("Scale bar added"), 3000);
}

void QgsLayoutDesignerDialog::onAddNorthArrow()
{
    if (!mLayout) return;

    QgsLayoutItemPicture *picture = new QgsLayoutItemPicture(mLayout);
    picture->setRect(230, 110, 30, 30);
    mLayout->addLayoutItem(picture);

    if (mWindow) mWindow->statusBar()->showMessage(tr("North arrow added"), 3000);
}

void QgsLayoutDesignerDialog::onAddLabel()
{
    if (!mLayout) return;

    QgsLayoutItemLabel *label = new QgsLayoutItemLabel(mLayout);
    label->setText(tr("Map Title"));
    label->setRect(20, 200, 100, 20);
    mLayout->addLayoutItem(label);

    if (mWindow) mWindow->statusBar()->showMessage(tr("Label added"), 3000);
}

void QgsLayoutDesignerDialog::onAddImage()
{
    if (!mLayout || !mWindow) return;

    QString filePath = QFileDialog::getOpenFileName(mWindow, tr("Select Image"), QString(),
                                                    tr("Images (*.png *.jpg *.svg)"));
    if (filePath.isEmpty()) return;

    QgsLayoutItemPicture *picture = new QgsLayoutItemPicture(mLayout);
    picture->setPicturePath(filePath);
    picture->setRect(230, 150, 60, 60);
    mLayout->addLayoutItem(picture);

    mWindow->statusBar()->showMessage(tr("Image added"), 3000);
}

void QgsLayoutDesignerDialog::onExportToPdf()
{
    if (!mLayout || !mWindow) return;

    QString filePath = QFileDialog::getSaveFileName(mWindow, tr("Export to PDF"), QString(),
                                                    tr("PDF (*.pdf)"));
    if (filePath.isEmpty()) return;

    QgsLayoutExporter exporter(mLayout);
    QgsLayoutExporter::ExportResult result = exporter.exportToPdf(filePath, QgsLayoutExporter::PdfExportSettings());

    if (result == QgsLayoutExporter::Success) {
        mWindow->statusBar()->showMessage(tr("Exported to %1").arg(filePath), 5000);
    } else {
        QMessageBox::warning(mWindow, tr("Export Failed"), tr("Failed to export to PDF."));
    }
}

void QgsLayoutDesignerDialog::onExportToImage()
{
    if (!mLayout || !mWindow) return;

    QString filePath = QFileDialog::getSaveFileName(mWindow, tr("Export to Image"), QString(),
                                                    tr("PNG (*.png);;JPEG (*.jpg)"));
    if (filePath.isEmpty()) return;

    QgsLayoutExporter exporter(mLayout);
    QgsLayoutExporter::ExportResult result = exporter.exportToImage(filePath, QgsLayoutExporter::ImageExportSettings());

    if (result == QgsLayoutExporter::Success) {
        mWindow->statusBar()->showMessage(tr("Exported to %1").arg(filePath), 5000);
    } else {
        QMessageBox::warning(mWindow, tr("Export Failed"), tr("Failed to export to image."));
    }
}
