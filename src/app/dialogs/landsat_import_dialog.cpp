// src/app/dialogs/landsat_import_dialog.cpp
#include "landsat_import_dialog.h"

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

using sicnu::ChildCandidate;
using sicnu::CollectionImportService;
using sicnu::CommitImportRequest;
using sicnu::CommitImportResult;
using sicnu::SatelliteProductsDiscoverer;
using sicnu::data::CollectionId;
using sicnu::data::DataManager;
using sicnu::data::PersistencePolicy;
using sicnu::data::Result;

LandsatImportDialog::LandsatImportDialog( QWidget *parent )
  : QDialog( parent )
{
  setObjectName( QStringLiteral( "landsatImportDialog" ) );
  setWindowTitle( tr( "导入 Landsat 产品" ) );
  setupUi();
}

void LandsatImportDialog::setDataManager( DataManager *dataManager )
{
  m_dataManager = dataManager;
  // Without a Data Manager there is no catalog to register into.
  m_importButton->setEnabled( m_dataManager != nullptr && previewCount() > 0 );
}

void LandsatImportDialog::setSourcePath( const QString &path, bool autoProbe )
{
  m_pathEdit->setText( path );
  if ( autoProbe )
    probe();
}

void LandsatImportDialog::setupUi()
{
  auto *layout = new QVBoxLayout( this );

  // Source directory row.
  auto *pathRow = new QHBoxLayout;
  m_pathEdit = new QLineEdit( this );
  m_pathEdit->setPlaceholderText( tr( "Landsat 场景目录（含 *_MTL.txt）" ) );
  m_browseButton = new QPushButton( tr( "浏览…" ), this );
  m_probeButton = new QPushButton( tr( "探测" ), this );
  pathRow->addWidget( m_pathEdit, 1 );
  pathRow->addWidget( m_browseButton );
  pathRow->addWidget( m_probeButton );
  layout->addLayout( pathRow );

  // Preview tree: one row per child candidate (grid group), with its bands.
  m_previewTree = new QTreeWidget( this );
  m_previewTree->setObjectName( QStringLiteral( "landsatPreviewTree" ) );
  m_previewTree->setColumnCount( 2 );
  m_previewTree->setHeaderLabels( { tr( "子项（网格组）" ), tr( "波段" ) } );
  m_previewTree->setRootIsDecorated( false );
  m_previewTree->header()->setSectionResizeMode( 0, QHeaderView::Stretch );
  layout->addWidget( m_previewTree, 1 );

  // Status label.
  m_statusLabel = new QLabel( this );
  m_statusLabel->setWordWrap( true );
  layout->addWidget( m_statusLabel );

  // Import / Cancel buttons.
  auto *buttonRow = new QHBoxLayout;
  buttonRow->addStretch( 1 );
  m_importButton = new QPushButton( tr( "导入" ), this );
  m_importButton->setDefault( true );
  m_importButton->setEnabled( false );
  m_cancelButton = new QPushButton( tr( "取消" ), this );
  buttonRow->addWidget( m_importButton );
  buttonRow->addWidget( m_cancelButton );
  layout->addLayout( buttonRow );

  connect( m_browseButton, &QPushButton::clicked, this,
           &LandsatImportDialog::onBrowse );
  connect( m_probeButton, &QPushButton::clicked, this,
           &LandsatImportDialog::onProbe );
  connect( m_importButton, &QPushButton::clicked, this,
           &LandsatImportDialog::onImport );
  connect( m_cancelButton, &QPushButton::clicked, this, &QDialog::reject );
}

void LandsatImportDialog::onBrowse()
{
  const QString dir = QFileDialog::getExistingDirectory(
    this, tr( "选择 Landsat 场景目录" ), m_pathEdit->text() );
  if ( dir.isEmpty() )
    return;
  m_pathEdit->setText( dir );
  // Auto-probe on directory confirm so the user sees the preview immediately.
  probe();
}

void LandsatImportDialog::onProbe()
{
  probe();
}

void LandsatImportDialog::onImport()
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

bool LandsatImportDialog::probe()
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
    m_lastError = tr( "请先选择 Landsat 场景目录。" );
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

void LandsatImportDialog::populatePreview()
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

int LandsatImportDialog::previewCount() const
{
  return m_previewTree->topLevelItemCount();
}

bool LandsatImportDialog::isChildChecked( int index ) const
{
  if ( index < 0 || index >= previewCount() )
    return false;
  return m_previewTree->topLevelItem( index )->checkState( 0 ) == Qt::Checked;
}

void LandsatImportDialog::setChildChecked( int index, bool checked )
{
  if ( index < 0 || index >= previewCount() )
    return;
  m_previewTree->topLevelItem( index )->setCheckState(
    0, checked ? Qt::Checked : Qt::Unchecked );
}

QVector<int> LandsatImportDialog::checkedChildIndices() const
{
  QVector<int> indices;
  for ( int i = 0; i < previewCount(); ++i )
  {
    if ( isChildChecked( i ) )
      indices.append( i );
  }
  return indices;
}

CollectionId LandsatImportDialog::commitSelection()
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

QString LandsatImportDialog::lastError() const
{
  return m_lastError;
}

CollectionId LandsatImportDialog::committedCollectionId() const
{
  return m_committedCollectionId;
}
