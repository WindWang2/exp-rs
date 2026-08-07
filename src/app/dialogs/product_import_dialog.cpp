// src/app/dialogs/product_import_dialog.cpp
#include "product_import_dialog.h"

#include <QDialogButtonBox>
#include <QFileDialog>
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
  return QStringLiteral( "遥感产品" );
}

} // namespace

ProductImportDialog::ProductImportDialog( QWidget *parent )
  : QDialog( parent )
{
  setObjectName( QStringLiteral( "productImportDialog" ) );
  setWindowTitle( familyTitle() );
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
  m_pathEdit->setPlaceholderText( tr( "%1 产品目录（Landsat 含 *_MTL.txt；Sentinel-2 含 .SAFE）" ).arg( display ) );
  SicnuDialogHelp::tip( m_pathEdit, tr( "%1 产品目录路径（自动识别产品类型）。" ).arg( display ) );
  SicnuDialogHelp::tip( m_browseButton, tr( "浏览选择 %1 产品目录。" ).arg( display ) );
  SicnuDialogHelp::tip( m_probeButton, tr( "解析产品元数据，列出可导入的子项与波段。" ) );
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

  // Source directory row.
  auto *pathRow = new QHBoxLayout;
  m_pathEdit = new QLineEdit( this );
  m_pathEdit->setPlaceholderText( tr( "产品目录（自动识别 Landsat / Sentinel-2 / MODIS）" ) );
  m_browseButton = new QPushButton( tr( "浏览…" ), this );
  m_probeButton = new QPushButton( tr( "探测" ), this );
  pathRow->addWidget( m_pathEdit, 1 );
  pathRow->addWidget( m_browseButton );
  pathRow->addWidget( m_probeButton );
  layout->addLayout( pathRow );

  // Preview tree: one row per child candidate (grid group), with its bands.
  m_previewTree = new QTreeWidget( this );
  m_previewTree->setObjectName( QStringLiteral( "productPreviewTree" ) );
  m_previewTree->setColumnCount( 2 );
  m_previewTree->setHeaderLabels( { tr( "子项（网格组）" ), tr( "波段" ) } );
  m_previewTree->setRootIsDecorated( false );
  m_previewTree->header()->setSectionResizeMode( 0, QHeaderView::Stretch );
  SicnuDialogHelp::tip( m_previewTree, tr( "预览探测到的子项（网格组）与波段；勾选需导入的波段。" ) );
  layout->addWidget( m_previewTree, 1 );

  // Status label.
  m_statusLabel = new QLabel( this );
  m_statusLabel->setWordWrap( true );
  layout->addWidget( m_statusLabel );

  // Import / Cancel / Help buttons.
  auto *buttonRow = new QHBoxLayout;
  auto *helpButton = new QPushButton( tr( "帮助" ), this );
  connect( helpButton, &QPushButton::clicked, this, [this]() {
    SicnuDialogHelp::showToolHelp( this, helpTool(), windowTitle() );
  } );
  buttonRow->addWidget( helpButton );
  buttonRow->addStretch( 1 );
  m_importButton = new QPushButton( tr( "导入" ), this );
  m_importButton->setDefault( true );
  m_importButton->setEnabled( false );
  SicnuDialogHelp::tip( m_importButton, tr( "导入选中的波段到工程（探测成功后可用）。" ) );
  m_cancelButton = new QPushButton( tr( "取消" ), this );
  SicnuDialogHelp::tip( m_cancelButton, tr( "关闭对话框，不执行导入。" ) );
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
    this, tr( "选择%1产品目录" ).arg( familyDisplayName( m_productFamily ) ),
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
    QMessageBox::warning( this, tr( "导入失败" ), m_lastError );
  }
}

bool ProductImportDialog::probe()
{
  m_lastError.clear();
  m_committedCollectionId = CollectionId();
  m_preview = sicnu::ImportPreview();

  if ( !m_dataManager )
  {
    m_lastError = tr( "数据管理器不可用，无法探测。" );
    m_statusLabel->setText( m_lastError );
    return false;
  }

  const QString source = m_pathEdit->text().trimmed();
  if ( source.isEmpty() )
  {
    m_lastError = tr( "请先选择产品目录。" );
    m_statusLabel->setText( m_lastError );
    return false;
  }

  CollectionImportService service( m_dataManager );
  const Result<sicnu::ImportPreview> result = service.probe( source );

  if ( !result )
  {
    m_lastError = result.diagnostics().isEmpty()
      ? tr( "产品探测失败。" )
      : result.diagnostics().first().message;
    m_statusLabel->setText( m_lastError );
    populatePreview();
    return false;
  }

  m_preview = result.value();
  populatePreview();
  m_statusLabel->setText(
    tr( "发现 %1 个波段/网格组；勾选要导入的波段。" )
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
    m_lastError = tr( "数据管理器不可用，无法导入。" );
    return CollectionId();
  }

  const QVector<int> selection = checkedChildIndices();
  if ( selection.isEmpty() )
  {
    m_lastError = tr( "请至少勾选一个波段组再导入。" );
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
      ? tr( "导入失败。" )
      : result.diagnostics.first().message;
    return CollectionId();
  }

  m_committedCollectionId = result.collectionId;
  m_statusLabel->setText(
    tr( "已将 %1 个波段导入集合 \"%2\"。" )
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
  return tr( "导入 %1" ).arg( familyDisplayName( m_productFamily ) );
}
