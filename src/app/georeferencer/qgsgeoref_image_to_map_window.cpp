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

  if ( !mGeorefSession.enableWorkflowMirror() )
  {
    SICNU_LOG_WARN( SicnuLogTags::Georeferencing,
                    QStringLiteral( "Failed to open workflow session lab.georef.image_to_map" ) );
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
    "Source image canvas: loads the image to correct.\n"
    "Add GCP: after clicking an image point, a dialog pops up to enter map coordinates, or pick them from the main window map.")  );

  mDstCanvas = nullptr; // no embedded base / map preview panel

  QWidget *srcPanel = makeCanvasPanel(
    mSrcCanvas, &mSrcLayerLabel,
    tr( "Source Image (Warp)" ),
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
  loadPts->setToolTip( tr( "Import a saved control point file." ) );
  auto *savePts = fileMenu->addAction( tr( "Save .points..." ), this, &QgsGeorefShellWindow::savePoints );
  savePts->setToolTip( tr( "Exports the current control points." ) );
  fileMenu->addSeparator();
  fileMenu->addAction( tr( "Close" ), this, &QWidget::close )->setToolTip( tr( "Closes this window." ) );
  addStandardMenuBar();
}

void QgsGeorefImageToMapWindow::setupToolbars()
{
  mToolBar = addToolBar( tr( "Tools" ) );
  mToolBar->setObjectName( QStringLiteral( "rsGeorefI2MToolBar" ) );
  mToolBar->setMovable( false );
  mToolBar->setToolTip( tr(
    "Image to Map: pick points on the source image; enter map coordinates manually or pick them from the main window map (no base map panel).")  );

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
    "Aligned with the QGIS Georeferencer: only the source image to correct is shown; no base map is embedded in this window.<br><br>"
    "<b>Typical Workflow</b><br>"
    "1. Load a georeferenced base map / vector in the main window<br>"
    "2. Open the source image in this window (file or main project layer)<br>"
    "3. Press Add GCP and click an image point on the source image<br>"
    "4. In the 'Enter Map Coordinates' dialog: type X/Y, or press 'Pick Point from Map' and click on the main window map<br>"
    "5. You can also edit the target X/Y columns directly in the GCP table<br>"
    "6. Optionally RPC / polynomial → run the correction<br><br>"
    "No SIFT; no embedded base image panel." );
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
