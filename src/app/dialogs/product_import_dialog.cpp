// src/app/dialogs/product_import_dialog.cpp
#include "product_import_dialog.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include "data/data_manager.h"
#include "dialog_help_catalog.h"

using sicnu::ChildCandidate;
using sicnu::CollectionImportService;
using sicnu::CommitImportRequest;
using sicnu::CommitImportResult;
using sicnu::SatelliteProductsDiscoverer;
using sicnu::data::CollectionId;
using sicnu::data::DataManager;
using sicnu::data::PersistencePolicy;
using sicnu::data::Result;

namespace
{

/// Human-readable product family name for labels ("Landsat", "Sentinel-2",
/// "MODIS", or "遥感产品" for auto).
QString familyDisplayName( const QString &family )
{
  if ( family == QLatin1String( "landsat" ) )
    return QStringLiteral( "Landsat" );
  if ( family == QLatin1String( "sentinel2" ) )
    return QStringLiteral( "Sentinel-2" );
  if ( family == QLatin1String( "modis" ) )
    return QStringLiteral( "MODIS" );
  return QObject::tr("Remote-Sensing Products" );
}

} // namespace

ProductImportDialog::ProductImportDialog( QWidget *parent )
  : QDialog( parent )
{
  setObjectName( QStringLiteral( "productImportDialog" ) );
  setWindowTitle( familyTitle() );
  setMinimumWidth( 560 );
  resize( 600, 480 );
  setupUi();
  setWhatsThis( SicnuDialogHelp::htmlForTool( helpTool(), windowTitle() ) );
  setToolTip( SicnuDialogHelp::shortForTool( helpTool(), windowTitle() ) );
}

void ProductImportDialog::setDataManager( DataManager *dataManager )
{
  m_dataManager = dataManager;
  // Without a Data Manager there is no catalog to register into.
  m_importButton->setEnabled( m_dataManager != nullptr && previewCount() > 0 );
}

void ProductImportDialog::setProductFamily( const QString &family )
{
  m_productFamily = family;
  setWindowTitle( familyTitle() );
  const QString display = familyDisplayName( family );
  m_pathEdit->setPlaceholderText( tr( "%1 product directory (Landsat: *_MTL.txt; Sentinel-2: .SAFE)" ).arg( display ) );
  SicnuDialogHelp::tip( m_pathEdit, tr( "%1 product directory (product type auto-detected)." ).arg( display ) );
  SicnuDialogHelp::tip( m_browseButton, tr( "Browse and choose the %1 product directory." ).arg( display ) );
  SicnuDialogHelp::tip( m_probeButton, tr( "Parses the product metadata and lists importable sub-items and bands." ) );
  setWhatsThis( SicnuDialogHelp::htmlForTool( helpTool(), windowTitle() ) );
  setToolTip( SicnuDialogHelp::shortForTool( helpTool(), windowTitle() ) );
}

void ProductImportDialog::setSourcePath( const QString &path, bool autoProbe )
{
  m_pathEdit->setText( path );
  if ( autoProbe )
    probe();
}

void ProductImportDialog::setupUi()
{
  auto *layout = new QVBoxLayout( this );
  layout->setContentsMargins( 12, 12, 12, 12 );
  layout->setSpacing( 10 );

  // Source directory group
  auto *sourceGroup = new QGroupBox( tr( "Product Data Source Directory" ), this );
  sourceGroup->setObjectName( QStringLiteral( "rsDialogGroup" ) );
  sourceGroup->setToolTip( tr( "Specify the root directory containing the satellite metadata and per-band rasters." ) );
  auto *sourceLayout = new QVBoxLayout( sourceGroup );
  sourceLayout->setContentsMargins( 10, 8, 10, 8 );
  sourceLayout->setSpacing( 8 );

  auto *pathRow = new QHBoxLayout;
  pathRow->setSpacing( 8 );
  m_pathEdit = new QLineEdit( sourceGroup );
  m_pathEdit->setPlaceholderText( tr( "Product directory (Landsat / Sentinel-2 / MODIS auto-detected)" ) );
  m_browseButton = new QPushButton( tr( "Browse..." ), sourceGroup );
  m_browseButton->setProperty( "sicnuSecondary", true );
  m_probeButton = new QPushButton( tr( "Detection and Identification" ), sourceGroup );
  m_probeButton->setProperty( "sicnuPrimary", true );
  pathRow->addWidget( m_pathEdit, 1 );
  pathRow->addWidget( m_browseButton );
  pathRow->addWidget( m_probeButton );
  sourceLayout->addLayout( pathRow );
  layout->addWidget( sourceGroup );

  // Preview tree group
  auto *previewGroup = new QGroupBox( tr( "Discovered Data Sub-items and Band Preview" ), this );
  previewGroup->setObjectName( QStringLiteral( "rsDialogGroup" ) );
  previewGroup->setToolTip( tr( "Tick the sub-items and bands to import into the project asset manager." ) );
  auto *previewLayout = new QVBoxLayout( previewGroup );
  previewLayout->setContentsMargins( 10, 8, 10, 8 );
  previewLayout->setSpacing( 8 );

  m_previewTree = new QTreeWidget( previewGroup );
  m_previewTree->setObjectName( QStringLiteral( "productPreviewTree" ) );
  m_previewTree->setColumnCount( 2 );
  m_previewTree->setHeaderLabels( { tr( "Sub-items (grid groups)" ), tr( "Included Bands" ) } );
  m_previewTree->setRootIsDecorated( false );
  m_previewTree->setAlternatingRowColors( true );
  m_previewTree->header()->setSectionResizeMode( 0, QHeaderView::Stretch );
  SicnuDialogHelp::tip( m_previewTree, tr( "Preview the discovered sub-items (grid groups) and bands; tick the bands to import." ) );
  previewLayout->addWidget( m_previewTree, 1 );
  layout->addWidget( previewGroup, 1 );

  // Status label.
  m_statusLabel = new QLabel( this );
  m_statusLabel->setObjectName( QStringLiteral( "rsDialogHint" ) );
  m_statusLabel->setWordWrap( true );
  layout->addWidget( m_statusLabel );

  // Import / Cancel / Help buttons.
  auto *buttonRow = new QHBoxLayout;
  buttonRow->setSpacing( 8 );
  auto *helpButton = new QPushButton( tr( "Help" ), this );
  helpButton->setProperty( "sicnuSecondary", true );
  connect( helpButton, &QPushButton::clicked, this, [this]() {
    SicnuDialogHelp::showToolHelp( this, helpTool(), windowTitle() );
  } );
  buttonRow->addWidget( helpButton );
  buttonRow->addStretch( 1 );
  m_importButton = new QPushButton( tr( "Import Selected" ), this );
  m_importButton->setProperty( "sicnuPrimary", true );
  m_importButton->setDefault( true );
  m_importButton->setEnabled( false );
  SicnuDialogHelp::tip( m_importButton, tr( "Imports the selected bands into the project (available after successful detection)." ) );
  m_cancelButton = new QPushButton( tr( "Cancel" ), this );
  m_cancelButton->setProperty( "sicnuSecondary", true );
  SicnuDialogHelp::tip( m_cancelButton, tr( "Closes the dialog without importing." ) );
  buttonRow->addWidget( m_importButton );
  buttonRow->addWidget( m_cancelButton );
  layout->addLayout( buttonRow );

  connect( m_browseButton, &QPushButton::clicked, this,
           &ProductImportDialog::onBrowse );
  connect( m_probeButton, &QPushButton::clicked, this,
           &ProductImportDialog::onProbe );
  connect( m_importButton, &QPushButton::clicked, this,
           &ProductImportDialog::onImport );
  connect( m_cancelButton, &QPushButton::clicked, this, &QDialog::reject );
}

void ProductImportDialog::onBrowse()
{
  const QString dir = QFileDialog::getExistingDirectory(
    this, tr( "Choose the %1 product directory" ).arg( familyDisplayName( m_productFamily ) ),
    m_pathEdit->text() );
  if ( dir.isEmpty() )
    return;
  m_pathEdit->setText( dir );
  // Auto-probe on directory confirm so the user sees the preview immediately.
  probe();
}

void ProductImportDialog::onProbe()
{
  probe();
}

void ProductImportDialog::onImport()
{
  const CollectionId collectionId = commitSelection();
  if ( !collectionId.isNull() )
  {
    // The collection is committed; the dialog closes accepted. The Data
    // Manager panel refreshes via collectionAdded/assetAdded.
    accept();
  }
  else
  {
    QMessageBox::warning( this, tr( "Import Failed" ), m_lastError );
  }
}

bool ProductImportDialog::probe()
{
  m_lastError.clear();
  m_committedCollectionId = CollectionId();
  m_preview = sicnu::ImportPreview();

  if ( !m_dataManager )
  {
    m_lastError = tr( "The data manager is unavailable; cannot probe." );
    m_statusLabel->setText( m_lastError );
    return false;
  }

  const QString source = m_pathEdit->text().trimmed();
  if ( source.isEmpty() )
  {
    m_lastError = tr( "Choose the product directory first." );
    m_statusLabel->setText( m_lastError );
    return false;
  }

  CollectionImportService service( m_dataManager );
  const Result<sicnu::ImportPreview> result = service.probe( source );

  if ( !result )
  {
    m_lastError = result.diagnostics().isEmpty()
      ? tr( "Product detection failed." )
      : result.diagnostics().first().message;
    m_statusLabel->setText( m_lastError );
    populatePreview();
    return false;
  }

  m_preview = result.value();
  populatePreview();
  m_statusLabel->setText(
    tr( "Found %1 bands / grid groups; tick the bands to import." )
      .arg( m_preview.children.size() ) );
  return true;
}

void ProductImportDialog::populatePreview()
{
  m_previewTree->clear();
  for ( const ChildCandidate &child : m_preview.children )
  {
    auto *item = new QTreeWidgetItem( m_previewTree );
    item->setText( 0, child.displayName );
    QStringList bandNames;
    for ( const sicnu::ChildBandInfo &band : child.bands )
      bandNames.append( band.name );
    item->setText( 1, bandNames.join( QStringLiteral( ", " ) ) );
    item->setCheckState( 0, Qt::Checked );
    item->setFlags( item->flags() | Qt::ItemIsUserCheckable );
  }
  m_importButton->setEnabled( m_dataManager != nullptr && previewCount() > 0 );
}

int ProductImportDialog::previewCount() const
{
  return m_previewTree->topLevelItemCount();
}

bool ProductImportDialog::isChildChecked( int index ) const
{
  if ( index < 0 || index >= previewCount() )
    return false;
  return m_previewTree->topLevelItem( index )->checkState( 0 ) == Qt::Checked;
}

void ProductImportDialog::setChildChecked( int index, bool checked )
{
  if ( index < 0 || index >= previewCount() )
    return;
  m_previewTree->topLevelItem( index )->setCheckState(
    0, checked ? Qt::Checked : Qt::Unchecked );
}

QVector<int> ProductImportDialog::checkedChildIndices() const
{
  QVector<int> indices;
  for ( int i = 0; i < previewCount(); ++i )
  {
    if ( isChildChecked( i ) )
      indices.append( i );
  }
  return indices;
}

CollectionId ProductImportDialog::commitSelection()
{
  m_lastError.clear();

  if ( !m_dataManager )
  {
    m_lastError = tr( "The data manager is unavailable; cannot import." );
    return CollectionId();
  }

  const QVector<int> selection = checkedChildIndices();
  if ( selection.isEmpty() )
  {
    m_lastError = tr( "Tick at least one band group before importing." );
    return CollectionId();
  }

  CommitImportRequest request;
  request.preview = m_preview;
  request.selectedChildIndices = selection;
  request.persistence = PersistencePolicy::ProjectPersistent;

  CollectionImportService service( m_dataManager );
  const CommitImportResult result = service.commit( request );

  if ( result.collectionId.isNull() )
  {
    m_lastError = result.diagnostics.isEmpty()
      ? tr( "Import failed." )
      : result.diagnostics.first().message;
    return CollectionId();
  }

  m_committedCollectionId = result.collectionId;
  m_statusLabel->setText(
    tr( "Imported %1 bands into collection \"%2\"." )
      .arg( result.childAssetIds.size() )
      .arg( m_preview.collectionDisplayName ) );
  return result.collectionId;
}

QString ProductImportDialog::lastError() const
{
  return m_lastError;
}

CollectionId ProductImportDialog::committedCollectionId() const
{
  return m_committedCollectionId;
}

QString ProductImportDialog::helpTool() const
{
  if ( m_productFamily == QLatin1String( "landsat" ) )
    return QStringLiteral( "landsat_import" );
  if ( m_productFamily == QLatin1String( "sentinel2" ) )
    return QStringLiteral( "sentinel2_import" );
  if ( m_productFamily == QLatin1String( "modis" ) )
    return QStringLiteral( "modis_import" );
  return QStringLiteral( "product_import" );
}

QString ProductImportDialog::familyTitle() const
{
  return tr( "Import %1" ).arg( familyDisplayName( m_productFamily ) );
}
