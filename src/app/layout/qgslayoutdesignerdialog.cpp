/***************************************************************************
 * qgslayoutdesignerdialog.cpp  —  Layout designer (print composer)
 ***************************************************************************/
#include "qgslayoutdesignerdialog.h"
#include "core/sicnu_logging.h"
#include "agent/layout_tools/layout_service.h"
#include <json/json.h>

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
#include <qgslayoutitemshape.h>
#include <qgslayoutitemchart.h>
#include <qgslayoutitemregistry.h>
#include <qgslayoutexporter.h>
#include <qgslayoutnortharrowhandler.h>
#include <qgslayoutitempage.h>
#include <qgslayoutpagecollection.h>
#include <qgslayoutsize.h>
#include <qgslayoutsnapper.h>
#include <qgsmasterlayoutinterface.h>
#include <qgsmessagebar.h>
#include <qgsproject.h>
#include <qgsmapcanvas.h>
#include <qgsmapsettings.h>
#include <qgsrectangle.h>
#include <qgsreadwritelocker.h>
#include <qgsreadwritecontext.h>

#include <gui/layout/qgslayoutviewtoolselect.h>
#include <gui/layout/qgslayoutviewtoolpan.h>
#include <gui/layout/qgslayoutviewtoolzoom.h>
#include <gui/layout/qgslayoutviewtoolmoveitemcontent.h>
#include <gui/layout/qgslayoutguiutils.h>
#include <gui/layout/qgslayoutaligner.h>
#include <gui/layout/qgslayoutitemwidget.h>
#include <gui/layout/qgslayoutpagepropertieswidget.h>
#include <gui/layout/qgslayoutitemguiregistry.h>
#include <gui/qgsgui.h>
#include <gui/qgspanelwidgetstack.h>

#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QDockWidget>
#include <QFileDialog>
#include <QGridLayout>
#include <QMessageBox>
#include <QActionGroup>
#include <QAction>
#include <QInputDialog>
#include <QUndoStack>
#include <QUndoView>
#include <QFormLayout>
#include <QGroupBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QVBoxLayout>

namespace
{

// The QGIS item-widget registry must be populated once per process before any
// layout item widget can be created. Upstream QGIS does this during app init;
// this fork lazily ensures it the first time a designer is opened. Registering
// twice would duplicate metadata, hence the guard.
void ensureItemWidgetsRegistered( QgsMapCanvas *canvas )
{
    QgsLayoutItemGuiRegistry *registry = QgsGui::layoutItemGuiRegistry();
    if ( registry->metadataIdForItemType( QgsLayoutItemRegistry::LayoutMap ) == -1 )
        QgsLayoutGuiUtils::registerGuiForKnownItemTypes( canvas );
}

// Estimated peak memory for a raster export: page size (mm) × dpi, RGBA,
// with an extra full-size copy for the encoder stage.
qint64 estimateImageExportBytes( QgsLayout *layout, double dpi )
{
    if ( !layout || layout->pageCollection()->pageCount() == 0 )
        return 0;
    const QgsLayoutSize size = layout->pageCollection()->pages().at( 0 )->pageSize();
    const double widthPx = size.width() / 25.4 * dpi;
    const double heightPx = size.height() / 25.4 * dpi;
    return static_cast< qint64 >( widthPx ) * static_cast< qint64 >( heightPx ) * 4 * 2;
}

// Hard limits protecting against uncontrolled multi-GB allocations: QImage
// dimensions are signed ints, so cap each edge and the total buffer.
constexpr double kMaxImageEdgePixels = 30000.0;
constexpr qint64 kWarnExportBytes = 1024LL * 1024 * 1024;        // 1 GiB
constexpr qint64 kMaxExportBytes = 4LL * 1024 * 1024 * 1024;     // 4 GiB

// Returns the DPI to use, or <= 0 if the user aborted / export was rejected.
double queryExportDpi( QgsLayout *layout, QWidget *parent, bool isRaster )
{
    bool ok = false;
    const double dpi = QInputDialog::getDouble( parent, QObject::tr( "Export Resolution" ),
                                                QObject::tr( "Resolution (DPI):" ), 300.0, 36.0, 1200.0, 1, &ok );
    if ( !ok )
        return -1.0;

    if ( isRaster )
    {
        const qint64 bytes = estimateImageExportBytes( layout, dpi );
        const QgsLayoutSize size = layout->pageCollection()->pageCount() > 0
                                       ? layout->pageCollection()->pages().at( 0 )->pageSize()
                                       : QgsLayoutSize( 0, 0 );
        const double wPx = size.width() / 25.4 * dpi;
        const double hPx = size.height() / 25.4 * dpi;
        if ( wPx > kMaxImageEdgePixels || hPx > kMaxImageEdgePixels )
        {
            QMessageBox::warning( parent, QObject::tr( "Export Too Large" ),
                                  QObject::tr( "The export would be %1 × %2 pixels, exceeding the %3 pixel edge limit. "
                                               "Please reduce the DPI." )
                                      .arg( qRound( wPx ) )
                                      .arg( qRound( hPx ) )
                                      .arg( qRound( kMaxImageEdgePixels ) ) );
            return -1.0;
        }
        if ( bytes > kMaxExportBytes )
        {
            QMessageBox::warning( parent, QObject::tr( "Export Too Large" ),
                                  QObject::tr( "The export would need roughly %1 GB of memory, above the %2 GB safety limit. "
                                               "Please reduce the DPI." )
                                      .arg( bytes / 1024.0 / 1024.0 / 1024.0, 0, 'f', 1 )
                                      .arg( kMaxExportBytes / 1024.0 / 1024.0 / 1024.0, 0, 'f', 1 ) );
            return -1.0;
        }
        if ( bytes > kWarnExportBytes )
        {
            const auto answer = QMessageBox::question(
                parent, QObject::tr( "Large Export" ),
                QObject::tr( "This export needs roughly %1 GB of memory and may take a while. Continue?" )
                    .arg( bytes / 1024.0 / 1024.0 / 1024.0, 0, 'f', 1 ),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No );
            if ( answer != QMessageBox::Yes )
                return -1.0;
        }
    }
    return dpi;
}

} // namespace

QgsLayoutDesignerDialog::QgsLayoutDesignerDialog(QgsMasterLayoutInterface *layout,
                                                 QgsMapCanvas *canvas,
                                                 QWidget *parent)
    : QgsLayoutDesignerInterface(parent)
    , mMasterLayout(layout)
    , mCanvas(canvas)
{
    SICNU_LOG_INFO(SicnuLogTags::Layout, "Layout Designer opened");

    // Populate the layout item GUI registry (item property widgets, add-item
    // defaults, double-click behaviors). Without this, createItemWidget()
    // returns nullptr for every item type and no item properties can be shown.
    ensureItemWidgetsRegistered(mCanvas);

    // Create the window as a top-level window (not embedded in the main window).
    mWindow = new QMainWindow(nullptr, Qt::Window);
    connect(mWindow.data(), &QObject::destroyed, this, &QObject::deleteLater);

    // QgsMasterLayoutInterface is also a QgsLayout (via QgsPrintLayout).
    mLayout = dynamic_cast<QgsLayout *>(layout);

    setupUi();
    setupItemPropertiesPanel();
    setupMenus();
    setupToolbars();
    setupUndoRedo();
    connectSelectionToInspector();

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

// ---------------------------------------------------------------------------
// Item property inspector
// ---------------------------------------------------------------------------

void QgsLayoutDesignerDialog::setupItemPropertiesPanel()
{
    mItemsStack = new QgsPanelWidgetStack();
    mItemPropertiesDock = new QDockWidget(tr("Item Properties"), mWindow);
    mItemPropertiesDock->setObjectName(QStringLiteral("ItemPropertiesDock"));
    mItemPropertiesDock->setWidget(mItemsStack);
    mItemPropertiesDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    mWindow->addDockWidget(Qt::RightDockWidgetArea, mItemPropertiesDock);

    showItemOptions(nullptr, false);
}

void QgsLayoutDesignerDialog::clearItemPanel()
{
    if (mItemDestroyedConnection) {
        QObject::disconnect(mItemDestroyedConnection);
        mItemDestroyedConnection = QMetaObject::Connection();
    }
    if (!mItemsStack)
        return;
    if (QgsPanelWidget *panel = mItemsStack->mainPanel()) {
        mItemsStack->takeMainPanel();
        panel->deleteLater();
    }
}

void QgsLayoutDesignerDialog::showItemOptions(QgsLayoutItem *item, bool bringPanelToFront)
{
    if (!mItemsStack)
        return;

    // Avoid rebuilding the panel when the focused item did not change: QGIS
    // item widgets already refresh themselves incrementally from item signals
    // (sizePositionChanged / changed), so a rebuild here would be both slow
    // and would reset transient widget state on every drag update.
    if (mCurrentItem == item && mItemsStack->mainPanel())
        return;

    mCurrentItem = item;
    clearItemPanel();

    if (item) {
        // Drop the panel as soon as the backing item is destroyed (e.g. deleted
        // or removed by an undo step) so the panel never references a dangling
        // item. mCurrentItem is a QPointer and is already null by the time
        // destroyed() is emitted from ~QObject. Handlers connected for earlier
        // items fire too, but mCurrentItem is non-null then, so they no-op.
        mItemDestroyedConnection = connect(item, &QObject::destroyed, this, [this]() {
            if (!mCurrentItem && mWindow)
                showItemOptions(nullptr);
        });
    }

    if (!item || !mLayout) {
        auto *placeholder = new QgsPanelWidget();
        auto *lay = new QVBoxLayout(placeholder);
        auto *label = new QLabel(tr("Select an item to edit its properties.\n\nPage setup is available via Layout → Page Properties."), placeholder);
        label->setAlignment(Qt::AlignCenter);
        label->setWordWrap(true);
        lay->addWidget(label, 1);
        mItemsStack->setMainPanel(placeholder);
    } else {
        // Multi-selection: batch editing of shared common properties instead of
        // wrongly binding the panel to just one of the selected items.
        const QList<QgsLayoutItem *> selected = mLayout->selectedLayoutItems();
        if (selected.size() > 1) {
            mItemsStack->setMainPanel(qobject_cast<QgsPanelWidget *>(buildMultiSelectionPanel(selected)));
        } else {
            QgsLayoutItemBaseWidget *widget = nullptr;
            if (item->type() == QgsLayoutItemRegistry::LayoutPage) {
                widget = new QgsLayoutPagePropertiesWidget(nullptr, item);
            } else {
                widget = QgsGui::layoutItemGuiRegistry()->createItemWidget(item);
            }

            QgsPanelWidget *panel = nullptr;
            if (widget) {
                widget->setMasterLayout(mMasterLayout);
                widget->setDesignerInterface(this);
                widget->setReportTypeString(QString());
                widget->setDockMode(true);
                panel = widget;
            } else {
                // No type-specific widget registered (group items, unregistered
                // types): still expose the common geometry/appearance editor so
                // every item remains editable.
                auto *container = new QgsPanelWidget();
                auto *lay = new QVBoxLayout(container);
                auto *common = new QgsLayoutItemPropertiesWidget(container, item);
                common->setMasterLayout(mMasterLayout);
                lay->addWidget(common);
                lay->addStretch(1);
                panel = container;
            }
            mItemsStack->setMainPanel(panel);
        }
    }

    if (bringPanelToFront && mItemPropertiesDock) {
        mItemPropertiesDock->show();
        mItemPropertiesDock->raise();
    }
}

QRectF QgsLayoutDesignerDialog::defaultItemRect(double x, double y, double width, double height) const
{
    double pageW = 210.0, pageH = 297.0;
    if (mLayout && mLayout->pageCollection()->pageCount() > 0) {
        const QgsLayoutSize size = mLayout->pageCollection()->pages().constFirst()->pageSize();
        pageW = size.width();
        pageH = size.height();
    }
    width = qMin(width, pageW - 4.0);
    height = qMin(height, pageH - 4.0);
    x = qBound(2.0, x, qMax(2.0, pageW - width - 2.0));
    y = qBound(2.0, y, qMax(2.0, pageH - height - 2.0));
    return QRectF(x, y, width, height);
}

QWidget *QgsLayoutDesignerDialog::buildMultiSelectionPanel(const QList<QgsLayoutItem *> &items)
{
    auto *container = new QgsPanelWidget();
    auto *outer = new QVBoxLayout(container);
    outer->setContentsMargins(0, 0, 0, 0);

    auto *info = new QLabel(tr("%1 items selected — edits apply to all selected items.").arg(items.size()), container);
    info->setWordWrap(true);
    outer->addWidget(info);

    auto *group = new QGroupBox(tr("Common Properties"), container);
    auto *form = new QFormLayout(group);

    auto *opacity = new QDoubleSpinBox(group);
    opacity->setObjectName(QStringLiteral("mMultiOpacitySpin"));
    opacity->setRange(0.0, 100.0);
    opacity->setSuffix(tr(" %"));
    opacity->setValue(items.isEmpty() ? 100.0 : items.first()->itemOpacity() * 100.0);
    form->addRow(tr("Opacity"), opacity);

    auto *rotation = new QDoubleSpinBox(group);
    rotation->setObjectName(QStringLiteral("mMultiRotationSpin"));
    rotation->setRange(-360.0, 360.0);
    rotation->setSuffix(tr(" °"));
    rotation->setValue(items.isEmpty() ? 0.0 : items.first()->itemRotation());
    form->addRow(tr("Rotation"), rotation);

    auto *visible = new QCheckBox(tr("Visible"), group);
    visible->setObjectName(QStringLiteral("mMultiVisibleCheck"));
    visible->setChecked(items.isEmpty() || items.first()->isVisible());
    form->addRow(QString(), visible);

    auto *locked = new QCheckBox(tr("Locked"), group);
    locked->setObjectName(QStringLiteral("mMultiLockedCheck"));
    locked->setChecked(items.isEmpty() || items.first()->isLocked());
    form->addRow(QString(), locked);

    // Apply a mutator to every selected item as one undoable macro. Each item
    // gets its own mergeable command so individual edits stay collapsible. The
    // selection is re-resolved at apply time so the panel never writes through
    // stale pointers if items were deleted while the panel was open.
    const auto applyToAll = [this](const QString &undoText, const std::function<void(QgsLayoutItem *)> &mutator) {
        if (!mLayout)
            return;
        const QList<QgsLayoutItem *> current = mLayout->selectedLayoutItems();
        if (current.isEmpty())
            return;
        mLayout->undoStack()->beginMacro(undoText);
        for (QgsLayoutItem *item : current) {
            if (item)
                mutator(item);
        }
        mLayout->undoStack()->endMacro();
    };

    // Commit on editingFinished rather than valueChanged: spinner drags emit
    // dozens of ticks per second and each tick snapshots every selected item
    // for undo, which would freeze large layouts.
    QObject::connect(opacity, &QAbstractSpinBox::editingFinished, this, [applyToAll, opacity]() {
        const double value = opacity->value();
        applyToAll(QObject::tr("Change Opacity"), [value](QgsLayoutItem *item) {
            item->beginCommand(QObject::tr("Change Opacity"), QgsLayoutItem::UndoOpacity);
            item->setItemOpacity(value / 100.0);
            item->endCommand();
        });
    });

    QObject::connect(rotation, &QAbstractSpinBox::editingFinished, this, [applyToAll, rotation]() {
        const double value = rotation->value();
        applyToAll(QObject::tr("Change Rotation"), [value](QgsLayoutItem *item) {
            item->beginCommand(QObject::tr("Change Rotation"), QgsLayoutItem::UndoRotation);
            item->setItemRotation(value);
            item->endCommand();
        });
    });

    QObject::connect(visible, &QCheckBox::toggled, this, [applyToAll](bool value) {
        applyToAll(value ? QObject::tr("Show Items") : QObject::tr("Hide Items"), [value](QgsLayoutItem *item) {
            item->setVisibility(value);
        });
    });

    QObject::connect(locked, &QCheckBox::toggled, this, [applyToAll](bool value) {
        applyToAll(value ? QObject::tr("Lock Items") : QObject::tr("Unlock Items"), [value](QgsLayoutItem *item) {
            item->setLocked(value);
        });
    });

    outer->addWidget(group);
    outer->addStretch(1);
    return container;
}

void QgsLayoutDesignerDialog::connectSelectionToInspector()
{
    if (!mView || !mLayout)
        return;

    // Canvas selection → inspector. Both the view (selectAll/paste/ungroup)
    // and the select tool (click, rubber-band) emit itemFocused.
    connect(mView, &QgsLayoutView::itemFocused, this, [this](QgsLayoutItem *item) {
        showItemOptions(item);
    });
    // Empty selection (e.g. items deselected programmatically or deleted)
    // clears the inspector. Selection with content is handled by itemFocused.
    connect(mLayout, &QGraphicsScene::selectionChanged, this, [this]() {
        // Rubber-band selection emits this once per (de)selected item; only
        // do work when a panel is actually showing.
        if (!mCurrentItem)
            return;
        if (mLayout && mLayout->selectedLayoutItems().isEmpty())
            showItemOptions(nullptr);
    });
}

// ---------------------------------------------------------------------------
// Undo / redo
// ---------------------------------------------------------------------------

void QgsLayoutDesignerDialog::setupUndoRedo()
{
    if (!mLayout || !mEditMenu)
        return;

    QUndoStack *undoStack = mLayout->undoStack()->stack();
    mActionUndo = undoStack->createUndoAction(this);
    mActionUndo->setShortcut(QKeySequence::Undo);
    mActionRedo = undoStack->createRedoAction(this);
    mActionRedo->setShortcut(QKeySequence::Shift | Qt::Key_Z);

    // Prepend undo/redo to the Edit menu (before Delete).
    QAction *first = mEditMenu->actions().isEmpty() ? nullptr : mEditMenu->actions().first();
    mEditMenu->insertAction(first, mActionUndo);
    if (first)
        mEditMenu->insertAction(first, mActionRedo);
    else
        mEditMenu->addAction(mActionRedo);
    if (first)
        mEditMenu->insertSeparator(first);

    if (mNavigationToolbar) {
        mNavigationToolbar->addSeparator();
        mNavigationToolbar->addAction(mActionUndo);
        mNavigationToolbar->addAction(mActionRedo);
    }

    // History dock mirrors the layout undo stack.
    mUndoView = new QUndoView(undoStack);
    mUndoDock = new QDockWidget(tr("History"), mWindow);
    mUndoDock->setObjectName(QStringLiteral("LayoutUndoDock"));
    mUndoDock->setWidget(mUndoView);
    mWindow->addDockWidget(Qt::RightDockWidgetArea, mUndoDock);
    mWindow->tabifyDockWidget(mItemPropertiesDock, mUndoDock);
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

    // Rulers.
    mHorizontalRuler = new QgsLayoutRuler(central, Qt::Horizontal);
    mVerticalRuler = new QgsLayoutRuler(central, Qt::Vertical);
    mView->setHorizontalRuler(mHorizontalRuler);
    mView->setVerticalRuler(mVerticalRuler);

    // Bind the view to the layout — critical: without this the canvas is blank.
    if (mLayout)
        mView->setCurrentLayout(mLayout);

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
    mLayoutMenu->addAction(tr("Page Properties..."), this, &QgsLayoutDesignerDialog::onShowPageProperties);
    mLayoutMenu->addAction(tr("Auto Arrange (Thematic Composition)..."), this,
                           &QgsLayoutDesignerDialog::onAutoArrange);
    mLayoutMenu->addSeparator();
    mLayoutMenu->addAction(tr("Save as Template..."), this, &QgsLayoutDesignerDialog::onSaveAsTemplate);
    mLayoutMenu->addAction(tr("Load from Template..."), this, &QgsLayoutDesignerDialog::onLoadFromTemplate);
    mLayoutMenu->addSeparator();
    mLayoutMenu->addAction(tr("Export to PDF..."), this, &QgsLayoutDesignerDialog::onExportToPdf);
    mLayoutMenu->addAction(tr("Export to Image..."), this, &QgsLayoutDesignerDialog::onExportToImage);
    mLayoutMenu->addAction(tr("Export to SVG..."), this, &QgsLayoutDesignerDialog::onExportToSvg);
    mLayoutMenu->addSeparator();
    mLayoutMenu->addAction(tr("Close"), this, &QgsLayoutDesignerDialog::close);

    mEditMenu = menuBar->addMenu(tr("&Edit"));
    // Undo/Redo actions are inserted at the top of this menu by setupUndoRedo().
    mEditMenu->addAction(tr("Duplicate Selected Items"), this,
                         &QgsLayoutDesignerDialog::onDuplicateSelectedItems,
                         QKeySequence(Qt::CTRL | Qt::Key_D));
    mEditMenu->addAction(tr("Delete Selected Items"), this,
                         &QgsLayoutDesignerDialog::onDeleteSelectedItems);
    mEditMenu->addSeparator();
    mEditMenu->addAction(tr("Select All"), this,
                         &QgsLayoutDesignerDialog::onSelectAllItems, QKeySequence::SelectAll);
    mEditMenu->addAction(tr("Deselect All"), this,
                         &QgsLayoutDesignerDialog::onDeselectAllItems);

    // Stacking.
    auto *stackMenu = mEditMenu->addMenu(tr("Ordering"));
    stackMenu->addAction(tr("Raise Items"), this, &QgsLayoutDesignerDialog::onRaiseItems);
    stackMenu->addAction(tr("Lower Items"), this, &QgsLayoutDesignerDialog::onLowerItems);
    stackMenu->addAction(tr("Bring to Front"), this, &QgsLayoutDesignerDialog::onBringToFront);
    stackMenu->addAction(tr("Send to Back"), this, &QgsLayoutDesignerDialog::onSendToBack);

    // Locking.
    auto *lockAction = mEditMenu->addAction(tr("Lock Items"), this, [this]() { onLockItems(true); });
    lockAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
    mEditMenu->addAction(tr("Unlock Items"), this, [this]() { onLockItems(false); });
    mEditMenu->addAction(tr("Unlock All Items"), this, [this]() {
        if (!mLayout) return;
        const QList<QgsLayoutItem *> all = mLayout->layoutItems();
        for (QgsLayoutItem *item : all)
            item->setLocked(false);
    });

    // Align / distribute (delegates to QgsLayoutAligner via the view, which
    // wraps each operation in an undo macro).
    auto *alignMenu = mEditMenu->addMenu(tr("Align Items"));
    const auto addAlignAction = [this, alignMenu](const QString &text, QgsLayoutAligner::Alignment alignment) {
        alignMenu->addAction(text, this, [this, alignment]() {
            if (mView) mView->alignSelectedItems(alignment);
        });
    };
    addAlignAction(tr("Align Left"), QgsLayoutAligner::AlignLeft);
    addAlignAction(tr("Align Horizontal Center"), QgsLayoutAligner::AlignHCenter);
    addAlignAction(tr("Align Right"), QgsLayoutAligner::AlignRight);
    addAlignAction(tr("Align Top"), QgsLayoutAligner::AlignTop);
    addAlignAction(tr("Align Vertical Center"), QgsLayoutAligner::AlignVCenter);
    addAlignAction(tr("Align Bottom"), QgsLayoutAligner::AlignBottom);

    auto *distributeMenu = mEditMenu->addMenu(tr("Distribute Items"));
    const auto addDistributeAction = [this, distributeMenu](const QString &text, QgsLayoutAligner::Distribution distribution) {
        distributeMenu->addAction(text, this, [this, distribution]() {
            if (mView) mView->distributeSelectedItems(distribution);
        });
    };
    addDistributeAction(tr("Distribute Left Edges"), QgsLayoutAligner::DistributeLeft);
    addDistributeAction(tr("Distribute Centers"), QgsLayoutAligner::DistributeHCenter);
    addDistributeAction(tr("Distribute Horizontal Spacing"), QgsLayoutAligner::DistributeHSpace);
    addDistributeAction(tr("Distribute Right Edges"), QgsLayoutAligner::DistributeRight);
    addDistributeAction(tr("Distribute Top Edges"), QgsLayoutAligner::DistributeTop);
    addDistributeAction(tr("Distribute Vertical Centers"), QgsLayoutAligner::DistributeVCenter);
    addDistributeAction(tr("Distribute Vertical Spacing"), QgsLayoutAligner::DistributeVSpace);
    addDistributeAction(tr("Distribute Bottom Edges"), QgsLayoutAligner::DistributeBottom);

    mViewMenu = menuBar->addMenu(tr("&View"));
    mViewMenu->addAction(tr("Zoom to Page"), this, &QgsLayoutDesignerDialog::onZoomToPage);
    mViewMenu->addAction(tr("Zoom In"), this, [this]() { if (mView) mView->zoomIn(); }, QKeySequence::ZoomIn);
    mViewMenu->addAction(tr("Zoom Out"), this, [this]() { if (mView) mView->zoomOut(); }, QKeySequence::ZoomOut);
    mViewMenu->addAction(tr("Zoom to 100%"), this, [this]() { if (mView) mView->zoomActual(); });

    mItemsMenu = menuBar->addMenu(tr("&Items"));
    mItemsMenu->addAction(tr("Add Map"), this, &QgsLayoutDesignerDialog::onAddMap);
    mItemsMenu->addAction(tr("Add Legend"), this, &QgsLayoutDesignerDialog::onAddLegend);
    mItemsMenu->addAction(tr("Add Scale Bar"), this, &QgsLayoutDesignerDialog::onAddScaleBar);
    mItemsMenu->addAction(tr("Add North Arrow"), this, &QgsLayoutDesignerDialog::onAddNorthArrow);
    mItemsMenu->addAction(tr("Add Grid"), this, &QgsLayoutDesignerDialog::onAddGrid);
    mItemsMenu->addSeparator();
    mItemsMenu->addAction(tr("Add Label"), this, &QgsLayoutDesignerDialog::onAddLabel);
    mItemsMenu->addAction(tr("Add Image"), this, &QgsLayoutDesignerDialog::onAddImage);
    mItemsMenu->addAction(tr("Add Shape"), this, &QgsLayoutDesignerDialog::onAddShape);
    mItemsMenu->addAction(tr("Add Chart"), this, &QgsLayoutDesignerDialog::onAddChart);

    mAtlasMenu = menuBar->addMenu(tr("&Atlas"));
    mReportMenu = menuBar->addMenu(tr("&Report"));

    mSettingsMenu = menuBar->addMenu(tr("&Settings"));
    if (mLayout) {
        QgsLayoutSnapper *snapper = mLayout->snapper();
        auto *snapGrid = mSettingsMenu->addAction(tr("Snap to Grid"), this, [this, snapper](bool on) {
            snapper->setSnapToGrid(on);
        });
        snapGrid->setCheckable(true);
        snapGrid->setChecked(snapper->snapToGrid());

        auto *snapGuides = mSettingsMenu->addAction(tr("Snap to Guides"), this, [this, snapper](bool on) {
            snapper->setSnapToGuides(on);
        });
        snapGuides->setCheckable(true);
        snapGuides->setChecked(snapper->snapToGuides());

        auto *snapItems = mSettingsMenu->addAction(tr("Snap to Items"), this, [this, snapper](bool on) {
            snapper->setSnapToItems(on);
        });
        snapItems->setCheckable(true);
        snapItems->setChecked(snapper->snapToItems());

        mSettingsMenu->addAction(tr("Snap Tolerance..."), this, [this, snapper]() {
            bool ok = false;
            const int tol = QInputDialog::getInt(mWindow, tr("Snap Tolerance"),
                                                 tr("Tolerance (pixels):"), snapper->snapTolerance(),
                                                 1, 100, 1, &ok);
            if (ok)
                snapper->setSnapTolerance(tol);
        });
    }
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
    map->attemptSetSceneRect(defaultItemRect(20, 20, 170, 130));

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
    legend->attemptSetSceneRect(defaultItemRect(195, 20, 60, 100));
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
    scaleBar->attemptSetSceneRect(defaultItemRect(20, 155, 60, 8));
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
    picture->setPicturePath(QStringLiteral(":/images/north_arrows/default.svg"));
    picture->setLinkedMap(mMapItem);
    picture->setNorthMode(QgsLayoutItemPicture::GridNorth);
    picture->attemptSetSceneRect(defaultItemRect(195, 125, 14, 14));
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
    label->attemptSetSceneRect(defaultItemRect(30, 4, 150, 12));
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
    picture->attemptSetSceneRect(defaultItemRect(140, 160, 50, 40));
    mLayout->addLayoutItem(picture);

    mWindow->statusBar()->showMessage(tr("Image added"), 3000);
}

void QgsLayoutDesignerDialog::onAddShape()
{
    if (!mLayout) return;

    auto *shape = new QgsLayoutItemShape(mLayout);
    shape->setShapeType(QgsLayoutItemShape::Rectangle);
    shape->attemptSetSceneRect(defaultItemRect(20, 160, 40, 20));
    mLayout->addLayoutItem(shape);

    if (mWindow) mWindow->statusBar()->showMessage(tr("Shape added"), 3000);
}

void QgsLayoutDesignerDialog::onAddChart()
{
    if (!mLayout) return;

    auto *chart = QgsLayoutItemChart::create(mLayout);
    chart->attemptSetSceneRect(defaultItemRect(140, 120, 60, 50));
    mLayout->addLayoutItem(chart);

    if (mWindow) mWindow->statusBar()->showMessage(tr("Chart added (select a vector layer in its properties)"), 3000);
}

void QgsLayoutDesignerDialog::onDeleteSelectedItems()
{
    if (!mLayout) return;

    const QList<QgsLayoutItem *> selected = mLayout->selectedLayoutItems();
    if (selected.isEmpty()) return;

    // Drop the inspector first: the panel widgets hold raw pointers to the
    // items being deleted.
    showItemOptions(nullptr);

    for (QgsLayoutItem *item : selected) {
        // Clear mMapItem if the tracked map is being deleted.
        if (item == mMapItem)
            mMapItem = nullptr;
        mLayout->removeLayoutItem(item);
    }

    if (mWindow) mWindow->statusBar()->showMessage(tr("Deleted %1 item(s)").arg(selected.size()), 3000);
}

void QgsLayoutDesignerDialog::onDuplicateSelectedItems()
{
    if (!mLayout) return;

    const QList<QgsLayoutItem *> selected = mLayout->selectedLayoutItems();
    if (selected.isEmpty()) {
        if (mMessageBar)
            mMessageBar->pushInfo(tr("Duplicate"), tr("Select item(s) to duplicate first."));
        return;
    }

    QList<QgsLayoutItem *> created;
    for (QgsLayoutItem *item : selected) {
        QDomDocument doc;
        QDomElement parent = doc.createElement(QStringLiteral("LayoutItemCopy"));
        doc.appendChild(parent);
        QgsReadWriteContext context;
        if (!item->writeXml(parent, doc, context))
            continue;

        const QList<QgsLayoutItem *> added = mLayout->addItemsFromXml(parent, doc, context);
        // Offset duplicates from their originals and select the copies.
        for (QgsLayoutItem *copy : added) {
            copy->attemptMoveBy(5.0, 5.0);
            copy->setSelected(true);
            created.append(copy);
        }
        item->setSelected(false);
    }

    if (!created.isEmpty() && mView)
        mView->setFocus();

    if (mWindow) mWindow->statusBar()->showMessage(tr("Duplicated %1 item(s)").arg(created.size()), 3000);
}

void QgsLayoutDesignerDialog::onSelectAllItems()
{
    if (!mView) return;
    mView->selectAll();
}

void QgsLayoutDesignerDialog::onDeselectAllItems()
{
    if (!mView) return;
    mView->deselectAll();
}

void QgsLayoutDesignerDialog::onZoomToPage()
{
    if (mView && mLayout)
        mView->zoomWidth();
}

// ---------------------------------------------------------------------------
// Stacking / locking
// ---------------------------------------------------------------------------

void QgsLayoutDesignerDialog::onRaiseItems()
{
    if (!mLayout) return;
    const QList<QgsLayoutItem *> selected = mLayout->selectedLayoutItems();
    if (selected.isEmpty()) return;
    mLayout->undoStack()->beginMacro(tr("Raise Items"));
    for (QgsLayoutItem *item : selected) {
        item->beginCommand(tr("Raise Item"));
        mLayout->raiseItem(item, /*deferUpdate=*/true);
        item->endCommand();
    }
    mLayout->updateZValues();
    mLayout->undoStack()->endMacro();
}

void QgsLayoutDesignerDialog::onLowerItems()
{
    if (!mLayout) return;
    const QList<QgsLayoutItem *> selected = mLayout->selectedLayoutItems();
    if (selected.isEmpty()) return;
    mLayout->undoStack()->beginMacro(tr("Lower Items"));
    for (QgsLayoutItem *item : selected) {
        item->beginCommand(tr("Lower Item"));
        mLayout->lowerItem(item, /*deferUpdate=*/true);
        item->endCommand();
    }
    mLayout->updateZValues();
    mLayout->undoStack()->endMacro();
}

void QgsLayoutDesignerDialog::onBringToFront()
{
    if (!mLayout) return;
    const QList<QgsLayoutItem *> selected = mLayout->selectedLayoutItems();
    if (selected.isEmpty()) return;
    mLayout->undoStack()->beginMacro(tr("Bring to Front"));
    for (QgsLayoutItem *item : selected) {
        item->beginCommand(tr("Bring to Front"));
        // Just below the page grid/guides band: top of the normal item range.
        item->setZValue(QgsLayout::ZGrid - 1.0);
        item->endCommand();
    }
    mLayout->updateZValues();
    mLayout->undoStack()->endMacro();
}

void QgsLayoutDesignerDialog::onSendToBack()
{
    if (!mLayout) return;
    const QList<QgsLayoutItem *> selected = mLayout->selectedLayoutItems();
    if (selected.isEmpty()) return;
    mLayout->undoStack()->beginMacro(tr("Send to Back"));
    for (QgsLayoutItem *item : selected) {
        item->beginCommand(tr("Send to Back"));
        item->setZValue(QgsLayout::ZItem);  // minimum z value for items
        item->endCommand();
    }
    mLayout->updateZValues();
    mLayout->undoStack()->endMacro();
}

void QgsLayoutDesignerDialog::onLockItems(bool locked)
{
    if (!mLayout) return;
    const QList<QgsLayoutItem *> selected = mLayout->selectedLayoutItems();
    if (selected.isEmpty()) return;
    for (QgsLayoutItem *item : selected)
        item->setLocked(locked);
    if (mWindow)
        mWindow->statusBar()->showMessage(locked ? tr("Locked %1 item(s)").arg(selected.size())
                                                 : tr("Unlocked %1 item(s)").arg(selected.size()), 3000);
}

// ---------------------------------------------------------------------------
// Page / template
// ---------------------------------------------------------------------------

void QgsLayoutDesignerDialog::onShowPageProperties()
{
    if (!mLayout || mLayout->pageCollection()->pageCount() == 0) {
        if (mMessageBar)
            mMessageBar->pushInfo(tr("Page"), tr("Layout has no pages."));
        return;
    }
    const QList<QgsLayoutItem *> selected = mLayout->selectedLayoutItems();
    for (QgsLayoutItem *item : selected)
        item->setSelected(false);
    QgsLayoutItemPage *page = mLayout->pageCollection()->pages().constFirst();
    if (page) {
        page->setSelected(true);
        showItemOptions(page);
    }
}

void QgsLayoutDesignerDialog::onAutoArrange()
{
    if (!mLayout || !mWindow) return;

    // Same shared layer as the layout:auto_arrange MCP tool: human and agent
    // edits have identical semantics.
    const Json::Value result = sicnu::agent::layout_tools::LayoutService::instance().autoArrange(mLayout, /*apply=*/true);
    int created = 0;
    if (result.isMember("components") && result["components"].isArray()) {
        for (const Json::Value &component : result["components"]) {
            if (component.get("created", false).asBool())
                created++;
        }
    }
    mWindow->statusBar()->showMessage(
        tr("Thematic composition arranged (%1 new component(s); existing non-auto items untouched)")
            .arg(created), 5000);
}

void QgsLayoutDesignerDialog::onSaveAsTemplate()
{
    if (!mLayout || !mWindow) return;

    const QString path = QFileDialog::getSaveFileName(mWindow, tr("Save as Template"),
                                                      QString(), tr("QGIS Layout Template (*.qpt)"));
    if (path.isEmpty()) return;

    QgsReadWriteContext context;
    const bool ok = mLayout->saveAsTemplate(path, context);
    if (ok) {
        mWindow->statusBar()->showMessage(tr("Template saved to %1").arg(path), 5000);
    } else {
        QMessageBox::warning(mWindow, tr("Save Template"), tr("Failed to save template."));
    }
}

void QgsLayoutDesignerDialog::onLoadFromTemplate()
{
    if (!mLayout || !mWindow) return;

    const QString path = QFileDialog::getOpenFileName(mWindow, tr("Load from Template"),
                                                      QString(), tr("QGIS Layout Template (*.qpt)"));
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(mWindow, tr("Load Template"), tr("Cannot read %1").arg(path));
        return;
    }

    QDomDocument doc;
    if (!doc.setContent(&file)) {
        QMessageBox::warning(mWindow, tr("Load Template"), tr("%1 is not a valid template file.").arg(path));
        return;
    }

    // Clear the inspector first: loading replaces all items.
    showItemOptions(nullptr);
    mMapItem = nullptr;

    QgsReadWriteContext context;
    bool ok = false;
    mLayout->loadFromTemplate(doc, context, /*clearExisting=*/true, &ok);
    if (ok) {
        // Re-track the reference map for follow-up legend/scalebar linkage.
        QList<QgsLayoutItemMap *> maps;
        mLayout->layoutItems(maps);
        if (!maps.isEmpty())
            mMapItem = maps.constFirst();
        mWindow->statusBar()->showMessage(tr("Template loaded from %1").arg(path), 5000);
    } else {
        QMessageBox::warning(mWindow, tr("Load Template"), tr("Failed to load template from %1").arg(path));
    }
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

    const double dpi = queryExportDpi(mLayout, mWindow, /*isRaster=*/false);
    if (dpi <= 0) return;

    QgsLayoutExporter exporter(mLayout);
    QgsLayoutExporter::PdfExportSettings settings;
    settings.dpi = dpi;
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

    // Preflight the raster allocation before rendering: high-DPI large-format
    // exports can otherwise request multi-GB buffers.
    const double dpi = queryExportDpi(mLayout, mWindow, /*isRaster=*/true);
    if (dpi <= 0) return;

    QgsLayoutExporter exporter(mLayout);
    QgsLayoutExporter::ImageExportSettings settings;
    settings.dpi = dpi;
    QgsLayoutExporter::ExportResult result = exporter.exportToImage(filePath, settings);

    if (result == QgsLayoutExporter::Success) {
        mWindow->statusBar()->showMessage(tr("Exported to %1").arg(filePath), 5000);
        emit layoutExported();
    } else {
        QMessageBox::warning(mWindow, tr("Export Failed"),
                             tr("Failed to export to image: %1").arg(exporter.errorMessage()));
    }
}

void QgsLayoutDesignerDialog::onExportToSvg()
{
    if (!mLayout || !mWindow) return;

    QString filePath = QFileDialog::getSaveFileName(mWindow, tr("Export to SVG"), QString(),
                                                    tr("SVG (*.svg)"));
    if (filePath.isEmpty()) return;

    QgsLayoutExporter exporter(mLayout);
    QgsLayoutExporter::SvgExportSettings settings;
    QgsLayoutExporter::ExportResult result = exporter.exportToSvg(filePath, settings);

    if (result == QgsLayoutExporter::Success) {
        mWindow->statusBar()->showMessage(tr("Exported to %1").arg(filePath), 5000);
        emit layoutExported();
    } else {
        QMessageBox::warning(mWindow, tr("Export Failed"),
                             tr("Failed to export to SVG: %1").arg(exporter.errorMessage()));
    }
}
