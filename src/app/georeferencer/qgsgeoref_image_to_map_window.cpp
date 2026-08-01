#include "qgsgeoref_image_to_map_window.h"

#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QToolBar>
#include <QWidget>
#include <QVBoxLayout>

#include "core/sicnu_logging.h"
#include "dialogs/dialog_help_catalog.h"
#include "qgsgcplist.h"
#include "qgsgcptransformer.h"
#include "qgsmapcanvas.h"
#include "qgsmaplayer.h"
#include "qgsproject.h"
#include "rs_georef_workflow_bridge.h"

QgsGeorefImageToMapWindow::QgsGeorefImageToMapWindow( QgisInterface *iface, QWidget *parent )
  : QgsGeorefShellWindow( iface, parent )
{
  SICNU_LOG_INFO( SicnuLogTags::Georeferencing, QStringLiteral( "Image 2 Map georef window opened" ) );
  setWindowTitle( tr( "Image Registration · Image 2 Map" ) );
  resize( 1000, 800 );
  setWhatsThis( SicnuDialogHelp::htmlForTool( QStringLiteral( "georef_i2m" ), windowTitle() ) );
  setToolTip( SicnuDialogHelp::shortForTool( QStringLiteral( "georef_i2m" ), windowTitle() ) );

  setupMenus();
  setupToolbars();
  setupStatusBar( QStringLiteral( "rsGeorefI2MCoordLabel" ),
                  QStringLiteral( "rsGeorefI2MCrsLabel" ),
                  QStringLiteral( "rsGeorefI2MRmsLabel" ) );
  setupCentralWidget();
  finishCommonSetup( RsGeorefParamsPanel::Profile::ImageToMap,
                     QStringLiteral( "rsGcpDockI2M" ),
                     QStringLiteral( "rsParamDockI2M" ) );

  // Runtime session for lab.georef.image_to_map — step/artifact mirror.
  // The workflow bridge is synced from session signals (gcpsChanged,
  // warpFinished); the only constructor-side write is source raster + open
  // step if a raster is already loaded (interaction-driven, not session-driven).
  mWorkflowBridge = std::make_unique<RsGeorefWorkflowBridge>();
  if ( !mWorkflowBridge->open() )
  {
    SICNU_LOG_WARN( SicnuLogTags::Georeferencing,
                    QStringLiteral( "Failed to open workflow session lab.georef.image_to_map" ) );
  }
  else
  {
    if ( !mSourceRasterPath.isEmpty() )
    {
      mWorkflowBridge->setSourceRasterArtifact( mSourceRasterPath.toStdString() );
      mWorkflowBridge->markStepComplete( "open_image" );
      mWorkflowBridge->gotoStep( "gcp" );
    }
    // GCP count artifact is synced by syncWorkflowGcps on gcpsChanged;
    // seed it once now to cover GCPs loaded before the bridge opened.
    syncWorkflowGcps();
  }
}

void QgsGeorefImageToMapWindow::setupCentralWidget()
{
  // QGIS-style Image→Map: only the unreferenced source image.
  // Destination map coordinates are typed or picked on the main window canvas.
  mSrcCanvas = new QgsMapCanvas( this );
  mSrcCanvas->setObjectName( QStringLiteral( "rsGeorefI2MSrcCanvas" ) );
  mSrcCanvas->setCanvasColor( Qt::white );
  mSrcCanvas->setToolTip( tr(
    "源影像画布：加载待校正影像。\n"
    "Add GCP：在影像上点击像点后，弹出对话框填写地图坐标，或从主窗口地图取点。" ) );

  mDstCanvas = nullptr; // no embedded base / map preview panel

  QWidget *srcPanel = makeCanvasPanel(
    mSrcCanvas, &mSrcLayerLabel,
    tr( "源影像 (Warp)" ),
    QStringLiteral( "rsGeorefI2MSrcPanel" ),
    QStringLiteral( "rsGeorefI2MSrcLayerLabel" ) );
  updateSourceLayerCaption();

  auto *central = new QWidget( this );
  central->setObjectName( QStringLiteral( "rsGeorefI2MCentral" ) );
  auto *lay = new QVBoxLayout( central );
  lay->setContentsMargins( 0, 0, 0, 0 );
  lay->setSpacing( 0 );
  lay->addWidget( srcPanel, 1 );
  setCentralWidget( central );
}

void QgsGeorefImageToMapWindow::setupMenus()
{
  QMenu *fileMenu = createFileMenu();
  fileMenu->addSeparator();
  auto *loadPts = fileMenu->addAction( tr( "Load .points..." ), this, &QgsGeorefShellWindow::loadPoints );
  loadPts->setToolTip( tr( "导入已保存的控制点文件。" ) );
  auto *savePts = fileMenu->addAction( tr( "Save .points..." ), this, &QgsGeorefShellWindow::savePoints );
  savePts->setToolTip( tr( "导出当前控制点。" ) );
  fileMenu->addSeparator();
  fileMenu->addAction( tr( "Close" ), this, &QWidget::close )->setToolTip( tr( "关闭本窗口。" ) );
  addStandardMenuBar();
}

void QgsGeorefImageToMapWindow::setupToolbars()
{
  mToolBar = addToolBar( tr( "Tools" ) );
  mToolBar->setObjectName( QStringLiteral( "rsGeorefI2MToolBar" ) );
  mToolBar->setMovable( false );
  mToolBar->setToolTip( tr(
    "Image 2 Map：在源影像上取点，地图坐标手填或从主窗口地图拾取（无底图面板）。" ) );

  addCanvasNavigationActions( mToolBar, QStringLiteral( "rsGeorefI2M" ) );
  mToolBar->addSeparator();
  addGcpEditActions( mToolBar, QStringLiteral( "rsGeorefI2M" ) );
  mToolBar->addSeparator();
  addApplyAction( mToolBar, QStringLiteral( "rsGeorefI2MApplyAction" ) );
}

QString QgsGeorefImageToMapWindow::windowHelpText() const
{
  return tr(
    "<b>Image Registration · Image 2 Map</b><br>"
    "对齐 QGIS Georeferencer：仅显示待校正源影像，不在本窗口嵌入底图。<br><br>"
    "<b>典型流程</b><br>"
    "1. 主窗口加载已有地理参考的底图/矢量<br>"
    "2. 本窗口打开源影像（文件或主工程图层）<br>"
    "3. 点 Add GCP，在源影像上点击像点<br>"
    "4. 在「输入地图坐标」对话框中：手填 X/Y，或点「从地图取点」在主窗口地图上点选<br>"
    "5. 也可在 GCP 表中直接编辑目标 X/Y 列<br>"
    "6. 可选 RPC / 多项式 → 运行校正<br><br>"
    "无 SIFT；无内嵌 Base 影像面板。" );
}

bool QgsGeorefImageToMapWindow::hasDestReady() const
{
  // Destination is the main map / typed coordinates — always available.
  return true;
}

void QgsGeorefImageToMapWindow::updateToolAvailability()
{
  QgsGeorefShellWindow::updateToolAvailability();
}

void QgsGeorefImageToMapWindow::onTransformMethodChangedExtra()
{
  // Params panel already toggles DEM for RPC on ImageToMap profile.
}

void QgsGeorefImageToMapWindow::captureShellSpecific( RsGeoreferencingSession::WorkflowSnapshot & ) const
{
}

void QgsGeorefImageToMapWindow::refreshMapLayersFromProject()
{
  // No embedded map panel — destination is the main application canvas.
}
