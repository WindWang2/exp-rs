// rs_post_process_dialog.cpp — one algorithm per dialog
#include "rs_post_process_dialog.h"
#include "dialogs/dialog_help_catalog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include "dialogs/dialog_utils.h"

QString RsPostProcessDialog::algorithmTitle( Algorithm a )
{
  switch ( a )
  {
    case Algorithm::Sieve:
      return tr( "Sieve Small-Region Removal" );
    case Algorithm::Majority:
      return tr( "Majority Filter" );
    case Algorithm::Clump:
      return tr( "Clump Connected-Component Labeling" );
    case Algorithm::Recode:
      return tr( "Recode" );
    case Algorithm::Polygonize:
      return tr( "Polygonize" );
  }
  return tr( "Post-Processing" );
}

QString RsPostProcessDialog::algorithmId( Algorithm a )
{
  switch ( a )
  {
    case Algorithm::Sieve:
      return QStringLiteral( "module:classify:postprocess:sieve" );
    case Algorithm::Majority:
      return QStringLiteral( "module:classify:postprocess:majority" );
    case Algorithm::Clump:
      return QStringLiteral( "module:classify:postprocess:clump" );
    case Algorithm::Recode:
      return QStringLiteral( "module:classify:postprocess:recode" );
    case Algorithm::Polygonize:
      return QStringLiteral( "module:classify:postprocess:polygonize" );
  }
  return QStringLiteral( "module:classify:postprocess" );
}

RsPostProcessDialog::RsPostProcessDialog( Algorithm algo, QWidget *parent )
  : QDialog( parent )
  , m_algo( algo )
{
  setWindowTitle( algorithmTitle( algo ) );
  setObjectName( QStringLiteral( "rsPostProcessDialog_%1" )
                   .arg( static_cast<int>( algo ) ) );
  SicnuUi::polishDialog( this, 520 );
  setModal( true );
  resize( 540, 420 );
  setupUi();
}

void RsPostProcessDialog::setupUi()
{
  auto *root = SicnuUi::makeDialogRootLayout( this );

  QString hint;
  switch ( m_algo )
  {
    case Algorithm::Sieve:
      hint = tr( "Removes connected patches smaller than the threshold and fills them with the neighbourhood majority class." );
      break;
    case Algorithm::Majority:
      hint = tr( "Sliding-window majority filter that smooths classification boundaries (kernel size must be odd)." );
      break;
    case Algorithm::Clump:
      hint = tr( "Labels connected regions of like pixels and outputs a patch-id raster." );
      break;
    case Algorithm::Recode:
      hint = tr( "Remaps class ids by the 'old class → new class' table; unlisted classes stay unchanged." );
      break;
    case Algorithm::Polygonize:
      hint = tr( "Vectorizes a classification / label raster into polygon features (.gpkg or .shp)." );
      break;
  }
  m_hintLabel = SicnuUi::makeHintLabel( this, hint );
  m_hintLabel->setWordWrap( true );
  root->addWidget( m_hintLabel );

  auto *ioGroup = SicnuUi::makeGroup( this, tr( "Input and Output Data" ) );
  auto *ioForm = SicnuUi::makeFormLayout( ioGroup );

  auto makeBrowse = [this]( QLineEdit **editOut, const QString &obj,
                            const QString &placeholder, bool save,
                            const QString &filter ) {
    auto *row = new QHBoxLayout;
    row->setSpacing( 8 );
    auto *edit = new QLineEdit( this );
    edit->setObjectName( obj );
    edit->setPlaceholderText( placeholder );
    *editOut = edit;
    auto *btn = new QPushButton( tr( "Browse..." ), this );
    SicnuUi::markSecondary( btn );
    SicnuDialogHelp::tip( btn, save ? tr( "Choose the Output File Save Path" ) : tr( "Select Input File Path" ) );
    connect( btn, &QPushButton::clicked, this, [this, edit, save, filter, placeholder]() {
      const QString p = save
                          ? QFileDialog::getSaveFileName( this, placeholder, edit->text(), filter )
                          : QFileDialog::getOpenFileName( this, placeholder, edit->text(), filter );
      if ( !p.isEmpty() )
        edit->setText( p );
    } );
    row->addWidget( edit, 1 );
    row->addWidget( btn );
    return row;
  };

  auto *inRow = makeBrowse( &m_inputEdit, QStringLiteral( "ppInput" ),
                            tr( "Classification / label raster" ), false,
                            tr( "GeoTIFF (*.tif *.tiff);;All files (*)" ) );
  SicnuDialogHelp::tip( m_inputEdit, tr( "Input classification or label raster file for post-processing" ) );
  ioForm->addRow( tr( "Input Raster" ), inRow );

  const bool isVectorOut = ( m_algo == Algorithm::Polygonize );
  auto *outRow = makeBrowse( &m_outputEdit, QStringLiteral( "ppOutput" ),
                             isVectorOut ? tr( "Output .gpkg / .shp" ) : tr( "Output GeoTIFF" ),
                             true,
                             isVectorOut
                               ? tr( "GeoPackage (*.gpkg);;ESRI Shapefile (*.shp)" )
                               : tr( "GeoTIFF (*.tif)" ) );
  SicnuDialogHelp::tip( m_outputEdit, isVectorOut ? tr( "Vectorized output file path (*.gpkg or *.shp)" ) : tr( "Post-processing result raster output path (*.tif)" ) );
  ioForm->addRow( isVectorOut ? tr( "Output Vector" ) : tr( "Output Raster" ), outRow );
  root->addWidget( ioGroup );

  // Algorithm-specific parameters
  if ( m_algo != Algorithm::Polygonize )
  {
    auto *paramGroup = SicnuUi::makeGroup( this, tr( "Algorithm Control Parameters" ) );
    auto *paramForm = SicnuUi::makeFormLayout( paramGroup );

    switch ( m_algo )
    {
      case Algorithm::Sieve:
        m_sieveSpin = new QSpinBox( paramGroup );
        m_sieveSpin->setObjectName( QStringLiteral( "ppSieveSpin" ) );
        m_sieveSpin->setRange( 1, 1000000 );
        m_sieveSpin->setValue( 10 );
        SicnuDialogHelp::tip( m_sieveSpin, tr( "Area threshold (pixels): connected patches below this pixel count are filtered out and filled from the neighbourhood" ) );
        paramForm->addRow( tr( "Area Threshold (pixels)" ), m_sieveSpin );
        m_connectSpin = new QSpinBox( paramGroup );
        m_connectSpin->setObjectName( QStringLiteral( "ppConnectSpin" ) );
        m_connectSpin->setRange( 4, 8 );
        m_connectSpin->setSingleStep( 4 );
        m_connectSpin->setValue( 8 );
        SicnuDialogHelp::tip( m_connectSpin, tr( "Pixel connectivity: 4-connected (edge neighbours) or 8-connected (incl. diagonals)" ) );
        paramForm->addRow( tr( "Connectivity (4/8)" ), m_connectSpin );
        break;
      case Algorithm::Majority:
        m_majoritySpin = new QSpinBox( paramGroup );
        m_majoritySpin->setObjectName( QStringLiteral( "ppMajoritySpin" ) );
        m_majoritySpin->setRange( 3, 7 );
        m_majoritySpin->setSingleStep( 2 );
        m_majoritySpin->setValue( 3 );
        SicnuDialogHelp::tip( m_majoritySpin, tr( "Majority filter window size (odd 3/5/7); larger values smooth more" ) );
        paramForm->addRow( tr( "Kernel Size (odd)" ), m_majoritySpin );
        break;
      case Algorithm::Clump:
        m_connectSpin = new QSpinBox( paramGroup );
        m_connectSpin->setObjectName( QStringLiteral( "ppConnectSpin" ) );
        m_connectSpin->setRange( 4, 8 );
        m_connectSpin->setSingleStep( 4 );
        m_connectSpin->setValue( 8 );
        SicnuDialogHelp::tip( m_connectSpin, tr( "Connected-region rule: 4-connectivity or 8-connectivity" ) );
        paramForm->addRow( tr( "Connectivity (4/8)" ), m_connectSpin );
        break;
      case Algorithm::Recode:
        m_recodeTable = new QTableWidget( 6, 2, paramGroup );
        m_recodeTable->setObjectName( QStringLiteral( "ppRecodeTable" ) );
        m_recodeTable->setHorizontalHeaderLabels( { tr( "Old Class" ), tr( "New Class" ) } );
        m_recodeTable->horizontalHeader()->setStretchLastSection( true );
        m_recodeTable->verticalHeader()->setVisible( false );
        m_recodeTable->setAlternatingRowColors( true );
        m_recodeTable->setMinimumHeight( 140 );
        SicnuDialogHelp::tip( m_recodeTable, tr( "Mapping table from old class IDs to new class IDs" ) );
        paramForm->addRow( tr( "Recoding Table" ), m_recodeTable );
        break;
      case Algorithm::Polygonize:
        break;
    }
    root->addWidget( paramGroup );
  }

  SicnuDialogHelp::applyDialogChrome( this, QStringLiteral( "post_process" ) );

  m_loadToLayersCb = new QCheckBox(
    tr( "Load results into the classification window layer management when finished" ), this );
  m_loadToLayersCb->setObjectName( QStringLiteral( "ppLoadToLayers" ) );
  m_loadToLayersCb->setChecked( true ); // default on
  m_loadToLayersCb->setToolTip(
    tr( "Ticked by default: result rasters / vectors join this classification window's layer tree on the left instead of only being written to files." ) );
  root->addWidget( m_loadToLayersCb );

  auto *buttons = new QDialogButtonBox(
    QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this );
  buttons->button( QDialogButtonBox::Ok )->setText( tr( "Run" ) );
  buttons->button( QDialogButtonBox::Cancel )->setText( tr( "Cancel" ) );
  SicnuUi::markPrimary( buttons->button( QDialogButtonBox::Ok ) );
  SicnuUi::markSecondary( buttons->button( QDialogButtonBox::Cancel ) );

  auto *helpBtn = buttons->addButton( tr( "Help" ), QDialogButtonBox::HelpRole );
  helpBtn->setToolTip( tr( "Opens the help for this dialog." ) );
  SicnuUi::markSecondary( helpBtn );
  connect( helpBtn, &QPushButton::clicked, this, [this]() {
    SicnuDialogHelp::showToolHelp( this, QStringLiteral( "post_process" ), windowTitle() );
  } );
  connect( buttons, &QDialogButtonBox::accepted, this, &QDialog::accept );
  connect( buttons, &QDialogButtonBox::rejected, this, &QDialog::reject );
  root->addWidget( buttons );
}

void RsPostProcessDialog::setDefaultInputPath( const QString &path )
{
  if ( m_inputEdit && !path.isEmpty() )
    m_inputEdit->setText( path );
}

void RsPostProcessDialog::setDefaultOutputPath( const QString &path )
{
  if ( m_outputEdit && !path.isEmpty() )
    m_outputEdit->setText( path );
}

bool RsPostProcessDialog::loadToLayerTree() const
{
  return m_loadToLayersCb ? m_loadToLayersCb->isChecked() : true;
}

QString RsPostProcessDialog::defaultOutputSuffix() const
{
  switch ( m_algo )
  {
    case Algorithm::Sieve:
      return QStringLiteral( "_sieve.tif" );
    case Algorithm::Majority:
      return QStringLiteral( "_majority.tif" );
    case Algorithm::Clump:
      return QStringLiteral( "_clump.tif" );
    case Algorithm::Recode:
      return QStringLiteral( "_recode.tif" );
    case Algorithm::Polygonize:
      return QStringLiteral( "_poly.gpkg" );
  }
  return QStringLiteral( "_post.tif" );
}

QMap<int, int> RsPostProcessDialog::collectRecodeMap() const
{
  QMap<int, int> map;
  if ( !m_recodeTable )
    return map;
  for ( int r = 0; r < m_recodeTable->rowCount(); ++r )
  {
    auto *oldItem = m_recodeTable->item( r, 0 );
    auto *newItem = m_recodeTable->item( r, 1 );
    if ( !oldItem || !newItem )
      continue;
    bool okOld = false;
    bool okNew = false;
    const int oldId = oldItem->text().trimmed().toInt( &okOld );
    const int newId = newItem->text().trimmed().toInt( &okNew );
    if ( okOld && okNew )
      map.insert( oldId, newId );
  }
  return map;
}

bool RsPostProcessDialog::buildConfig( RsPostProcessConfig &cfg, QString *errorMessage ) const
{
  cfg = RsPostProcessConfig{};
  // Only one operator enabled for this dialog
  cfg.runSieve = false;
  cfg.runMajority = false;
  cfg.runClump = false;
  cfg.runRecode = false;
  cfg.runPolygonize = false;

  cfg.inputPath = m_inputEdit ? m_inputEdit->text().trimmed() : QString();
  if ( cfg.inputPath.isEmpty() )
  {
    if ( errorMessage )
      *errorMessage = tr( "Please specify the input raster path" );
    return false;
  }
  if ( !QFileInfo::exists( cfg.inputPath ) )
  {
    if ( errorMessage )
      *errorMessage = tr( "Input file does not exist: %1" ).arg( cfg.inputPath );
    return false;
  }

  QString out = m_outputEdit ? m_outputEdit->text().trimmed() : QString();
  if ( out.isEmpty() )
  {
    const QFileInfo fi( cfg.inputPath );
    out = fi.absolutePath() + QLatin1Char( '/' ) + fi.completeBaseName()
          + defaultOutputSuffix();
  }

  switch ( m_algo )
  {
    case Algorithm::Sieve:
      cfg.runSieve = true;
      cfg.sieveThreshold = m_sieveSpin ? m_sieveSpin->value() : 10;
      cfg.connectedness = m_connectSpin ? m_connectSpin->value() : 8;
      if ( cfg.connectedness != 4 && cfg.connectedness != 8 )
        cfg.connectedness = 8;
      cfg.outputRasterPath = out;
      break;
    case Algorithm::Majority:
    {
      cfg.runMajority = true;
      int k = m_majoritySpin ? m_majoritySpin->value() : 3;
      if ( k % 2 == 0 )
        ++k;
      cfg.majorityKernel = k;
      cfg.outputRasterPath = out;
      break;
    }
    case Algorithm::Clump:
      cfg.runClump = true;
      cfg.connectedness = m_connectSpin ? m_connectSpin->value() : 8;
      if ( cfg.connectedness != 4 && cfg.connectedness != 8 )
        cfg.connectedness = 8;
      cfg.outputRasterPath = out;
      break;
    case Algorithm::Recode:
      cfg.runRecode = true;
      cfg.recodeMap = collectRecodeMap();
      if ( cfg.recodeMap.isEmpty() )
      {
        if ( errorMessage )
          *errorMessage = tr( "Fill in at least one old class → new class row in the recoding table" );
        return false;
      }
      cfg.outputRasterPath = out;
      break;
    case Algorithm::Polygonize:
      cfg.runPolygonize = true;
      // Polygonize needs an intermediate: task saves raster then polygonizes.
      // Use input as "processed" labels path: load → save copy? Looking at task:
      // it runs operators then save then polygonize from output raster.
      // For polygonize-only, we need save of input (or identity) then polygonize.
      // RsPostProcessTask: load → ops → save → polygonize from output raster.
      // So set output raster to a temp-like path next to vector, then polygonize.
      cfg.outputVectorPath = out;
      {
        const QFileInfo fi( out );
        cfg.outputRasterPath = fi.absolutePath() + QLatin1Char( '/' )
                               + fi.completeBaseName() + QStringLiteral( "_labels.tif" );
      }
      // No filter ops: need at least save. Task requires one of the flags;
      // polygonize alone is OK in task if runPolygonize - check task.
      break;
  }

  return true;
}
