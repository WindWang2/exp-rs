#include "data_manager_panel.h"

#include <QHeaderView>
#include <QMenu>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include "data/data_manager.h"

namespace sicnu
{

namespace
{

QString kindText( sicnu::data::AssetKind kind )
{
  switch ( kind )
  {
    case sicnu::data::AssetKind::Raster:
      return QStringLiteral( "Raster" );
    case sicnu::data::AssetKind::Vector:
      return QStringLiteral( "Vector" );
    case sicnu::data::AssetKind::RemoteMap:
      return QStringLiteral( "Remote Map" );
    case sicnu::data::AssetKind::VirtualRaster:
      return QStringLiteral( "Virtual Raster" );
  }
  return QStringLiteral( "Unknown" );
}

QString statusText( sicnu::data::AssetState state )
{
  switch ( state )
  {
    case sicnu::data::AssetState::Registered:
      return QStringLiteral( "Registered" );
    case sicnu::data::AssetState::Resolving:
      return QStringLiteral( "Resolving" );
    case sicnu::data::AssetState::Ready:
      return QStringLiteral( "Ready" );
    case sicnu::data::AssetState::Missing:
      return QStringLiteral( "Missing" );
    case sicnu::data::AssetState::Offline:
      return QStringLiteral( "Offline" );
    case sicnu::data::AssetState::AuthenticationRequired:
      return QStringLiteral( "Auth Required" );
    case sicnu::data::AssetState::Error:
      return QStringLiteral( "Error" );
    case sicnu::data::AssetState::Stale:
      return QStringLiteral( "Stale" );
  }
  return QStringLiteral( "Unknown" );
}

QString persistenceText( sicnu::data::PersistencePolicy persistence )
{
  switch ( persistence )
  {
    case sicnu::data::PersistencePolicy::ProjectPersistent:
      return QStringLiteral( "Persistent" );
    case sicnu::data::PersistencePolicy::SessionTemporary:
      return QStringLiteral( "Session" );
    case sicnu::data::PersistencePolicy::TaskTemporary:
      return QStringLiteral( "Task" );
  }
  return QStringLiteral( "Unknown" );
}

constexpr int kAssetIdRole = Qt::UserRole;

} // namespace

DataManagerPanel::DataManagerPanel( sicnu::data::DataManager *dataManager,
                                    QWidget *parent )
  : QDockWidget( tr( "Data Manager" ), parent )
  , m_dataManager( dataManager )
{
  setObjectName( QStringLiteral( "DataManagerPanel" ) );

  m_tree = new QTreeWidget( this );
  m_tree->setObjectName( QStringLiteral( "dataManagerTree" ) );
  m_tree->setColumnCount( 5 );
  m_tree->setHeaderLabels(
    { tr( "Name" ), tr( "Kind" ), tr( "Status" ), tr( "Persistence" ), tr( "Refs" ) } );
  m_tree->setRootIsDecorated( false );
  m_tree->setSelectionMode( QAbstractItemView::SingleSelection );
  m_tree->setContextMenuPolicy( Qt::CustomContextMenu );
  m_tree->header()->setStretchLastSection( false );
  m_tree->header()->setSectionResizeMode( 0, QHeaderView::Stretch );

  auto *container = new QWidget( this );
  auto *layout = new QVBoxLayout( container );
  layout->setContentsMargins( 0, 0, 0, 0 );
  layout->addWidget( m_tree );
  setWidget( container );

  connect( m_tree, &QTreeWidget::itemActivated, this,
           &DataManagerPanel::onItemActivated );
  connect( m_tree, &QTreeWidget::customContextMenuRequested, this,
           &DataManagerPanel::onContextMenu );

  if ( m_dataManager )
  {
    connect( m_dataManager, &sicnu::data::DataManager::assetAdded, this,
             &DataManagerPanel::refresh );
    connect( m_dataManager, &sicnu::data::DataManager::assetChanged, this,
             &DataManagerPanel::refresh );
    connect( m_dataManager, &sicnu::data::DataManager::assetRemoved, this,
             &DataManagerPanel::refresh );
  }

  refresh();
}

int DataManagerPanel::rowCount() const
{
  return m_tree->topLevelItemCount();
}

QString DataManagerPanel::rowText( sicnu::data::AssetId id, int column ) const
{
  for ( int row = 0; row < m_tree->topLevelItemCount(); ++row )
  {
    const QTreeWidgetItem *item = m_tree->topLevelItem( row );
    if ( item->data( 0, kAssetIdRole ).toString() == id.toString() )
      return item->text( column );
  }
  return QString();
}

sicnu::data::AssetId DataManagerPanel::selectedAssetId() const
{
  return assetForItem( m_tree->currentItem() );
}

void DataManagerPanel::selectAsset( sicnu::data::AssetId id )
{
  for ( int row = 0; row < m_tree->topLevelItemCount(); ++row )
  {
    QTreeWidgetItem *item = m_tree->topLevelItem( row );
    if ( item->data( 0, kAssetIdRole ).toString() == id.toString() )
    {
      m_tree->setCurrentItem( item );
      return;
    }
  }
}

void DataManagerPanel::activateAsset( sicnu::data::AssetId id )
{
  if ( !id.isNull() )
    emit displayRequested( id );
}

void DataManagerPanel::requestRemove( sicnu::data::AssetId id )
{
  if ( !id.isNull() )
    emit unloadRequested( id );
}

void DataManagerPanel::refresh()
{
  // Remember the current selection so a refresh does not change which asset the
  // user had selected (and never touches any renderer).
  const QString previouslySelected = selectedAssetId().toString();

  m_tree->clear();
  if ( !m_dataManager )
    return;

  for ( const sicnu::data::AssetSnapshot &snapshot : m_dataManager->assets() )
  {
    auto *item = new QTreeWidgetItem( m_tree );
    item->setText( 0, snapshot.displayName() );
    item->setText( 1, kindText( snapshot.kind() ) );
    item->setText( 2, statusText( snapshot.state() ) );
    item->setText( 3, persistenceText( snapshot.persistence() ) );
    item->setText( 4, QString::number( referenceCount( snapshot.id() ) ) );
    item->setData( 0, kAssetIdRole, snapshot.id().toString() );
    if ( snapshot.state() == sicnu::data::AssetState::Missing )
      item->setToolTip( 2, tr( "The source is missing; relocate to recover" ) );
  }

  if ( !previouslySelected.isEmpty() )
  {
    const auto restored = sicnu::data::AssetId::fromString( previouslySelected );
    if ( restored )
      selectAsset( *restored );
  }
}

void DataManagerPanel::onItemActivated( QTreeWidgetItem *item, int column )
{
  Q_UNUSED( column );
  const sicnu::data::AssetId id = assetForItem( item );
  if ( !id.isNull() )
    emit displayRequested( id );
}

void DataManagerPanel::onContextMenu( const QPoint &pos )
{
  QTreeWidgetItem *item = m_tree->itemAt( pos );
  const sicnu::data::AssetId id = assetForItem( item );
  if ( id.isNull() )
    return;

  QMenu menu( this );
  QAction *displayAction = menu.addAction( tr( "Add to Display" ) );
  QAction *unloadAction = menu.addAction( tr( "Unload…" ) );

  QAction *chosen = menu.exec( m_tree->viewport()->mapToGlobal( pos ) );
  if ( chosen == displayAction )
    emit displayRequested( id );
  else if ( chosen == unloadAction )
    emit unloadRequested( id );
}

sicnu::data::AssetId DataManagerPanel::assetForItem( QTreeWidgetItem *item ) const
{
  if ( !item )
    return sicnu::data::AssetId();
  const auto id = sicnu::data::AssetId::fromString(
    item->data( 0, kAssetIdRole ).toString() );
  return id.value_or( sicnu::data::AssetId() );
}

int DataManagerPanel::referenceCount( sicnu::data::AssetId id ) const
{
  if ( !m_dataManager )
    return 0;

  int count = 0;
  for ( const sicnu::data::LeaseRef &lease : m_dataManager->leases( id ) )
  {
    if ( lease.kind == sicnu::data::LeaseKind::View )
      ++count;
  }
  return count;
}

} // namespace sicnu
