#include "rs_georef_task_list.h"

#include "design_tokens.h"

#include <QAbstractItemView>
#include <QColor>
#include <QItemSelectionModel>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

RsGeorefTaskList::RsGeorefTaskList( QWidget *parent )
  : QWidget( parent )
{
  auto *root = new QVBoxLayout( this );
  root->setContentsMargins( 4, 4, 4, 4 );
  root->setSpacing( 4 );

  auto *top = new QHBoxLayout;
  mSummary = new QLabel( tr( "Tasks: 0" ), this );
  mSummary->setObjectName( QStringLiteral( "rsGeorefTaskSummary" ) );
  mSummary->setToolTip( tr( "Task statistics: total / running / done / failed." ) );
  mCancelBtn = new QPushButton( tr( "Deselect" ), this );
  mCancelBtn->setObjectName( QStringLiteral( "rsGeorefTaskCancelBtn" ) );
  mCancelBtn->setEnabled( false );
  mCancelBtn->setToolTip( tr( "Cancels the selected correction task if it is still running." ) );
  mCancelBtn->setStatusTip( mCancelBtn->toolTip() );
  mClearBtn = new QPushButton( tr( "Clear Finished" ), this );
  mClearBtn->setObjectName( QStringLiteral( "rsGeorefTaskClearBtn" ) );
  mClearBtn->setToolTip( tr( "Removes finished/failed/cancelled tasks from the list; running tasks are not affected." ) );
  mClearBtn->setStatusTip( mClearBtn->toolTip() );
  top->addWidget( mSummary, 1 );
  top->addWidget( mCancelBtn );
  top->addWidget( mClearBtn );
  root->addLayout( top );

  mTable = new QTableWidget( 0, 9, this );
  mTable->setObjectName( QStringLiteral( "rsGeorefTaskTable" ) );
  mTable->setHorizontalHeaderLabels( {
    tr( "#" ),
    tr( "Type" ),
    tr( "Method" ),
    tr( "Status" ),
    tr( "Progress" ),
    tr( "GCP" ),
    tr( "RMS" ),
    tr( "Elapsed" ),
    tr( "Outputs" ),
  } );
  mTable->verticalHeader()->setVisible( false );
  mTable->verticalHeader()->setDefaultSectionSize( 24 );
  mTable->setSelectionBehavior( QAbstractItemView::SelectRows );
  mTable->setSelectionMode( QAbstractItemView::SingleSelection );
  mTable->setEditTriggers( QAbstractItemView::NoEditTriggers );
  mTable->setAlternatingRowColors( true );
  mTable->setSortingEnabled( false );
  mTable->setContextMenuPolicy( Qt::CustomContextMenu );
  mTable->setColumnWidth( 0, 36 );
  mTable->setColumnWidth( 1, 72 );
  mTable->setColumnWidth( 3, 64 );
  mTable->setColumnWidth( 4, 52 );
  mTable->setColumnWidth( 5, 44 );
  mTable->setColumnWidth( 6, 64 );
  mTable->setColumnWidth( 7, 64 );
  mTable->horizontalHeader()->setSectionResizeMode( 2, QHeaderView::Stretch );
  mTable->horizontalHeader()->setSectionResizeMode( 8, QHeaderView::Stretch );
  mTable->setToolTip( tr( "Double-click a finished task to load its results into the main project; right-click a running task to cancel it" ) );
  root->addWidget( mTable, 1 );

  connect( mClearBtn, &QPushButton::clicked, this, &RsGeorefTaskList::onClearClicked );
  connect( mCancelBtn, &QPushButton::clicked, this, &RsGeorefTaskList::onCancelClicked );
  connect( mTable, &QTableWidget::cellDoubleClicked,
           this, &RsGeorefTaskList::onDoubleClick );
  connect( mTable, &QTableWidget::customContextMenuRequested,
           this, &RsGeorefTaskList::onContextMenu );
  connect( mTable, &QTableWidget::itemSelectionChanged, this, [this]() {
    const auto rows = mTable->selectionModel()->selectedRows();
    bool canCancel = false;
    if ( !rows.isEmpty() )
    {
      const int row = rows.first().row();
      if ( row >= 0 && row < mEntries.size() )
        canCancel = ( mEntries.at( row ).status == Status::Running );
    }
    mCancelBtn->setEnabled( canCancel );
  } );

  updateSummary();
}

int RsGeorefTaskList::beginTask( Kind kind, const QString &title,
                                 const QString &methodLabel,
                                 const QString &sourcePath,
                                 const QString &outputPath,
                                 int gcpCount, double rmsPx )
{
  Entry e;
  e.id = mNextId++;
  e.kind = kind;
  e.status = Status::Running;
  e.title = title;
  e.methodLabel = methodLabel;
  e.sourcePath = sourcePath;
  e.outputPath = outputPath;
  e.gcpCount = gcpCount;
  e.rmsPx = rmsPx;
  e.progress = 0.0;
  e.startedAt = QDateTime::currentDateTime();
  mEntries.prepend( e );
  rebuildTable();
  return e.id;
}

void RsGeorefTaskList::setProgress( int id, double percent )
{
  for ( Entry &e : mEntries )
  {
    if ( e.id != id || e.status != Status::Running )
      continue;
    e.progress = qBound( 0.0, percent, 100.0 );
    refreshRow( id );
    return;
  }
}

void RsGeorefTaskList::finishSuccess( int id, int durationMs, qint64 outputBytes,
                                      const QString &detail )
{
  for ( Entry &e : mEntries )
  {
    if ( e.id != id )
      continue;
    e.status = Status::Success;
    e.progress = 100.0;
    e.durationMs = durationMs;
    e.outputBytes = outputBytes;
    e.detail = detail;
    e.finishedAt = QDateTime::currentDateTime();
    break;
  }
  rebuildTable();
}

void RsGeorefTaskList::finishFailed( int id, const QString &error, int durationMs )
{
  for ( Entry &e : mEntries )
  {
    if ( e.id != id )
      continue;
    e.status = Status::Failed;
    e.detail = error;
    e.durationMs = durationMs;
    e.finishedAt = QDateTime::currentDateTime();
    break;
  }
  rebuildTable();
}

void RsGeorefTaskList::finishCancelled( int id, int durationMs )
{
  for ( Entry &e : mEntries )
  {
    if ( e.id != id )
      continue;
    e.status = Status::Cancelled;
    e.durationMs = durationMs;
    e.finishedAt = QDateTime::currentDateTime();
    break;
  }
  rebuildTable();
}

void RsGeorefTaskList::clearFinished()
{
  QVector<Entry> keep;
  for ( const Entry &e : mEntries )
  {
    if ( e.status == Status::Running )
      keep.append( e );
  }
  mEntries = keep;
  rebuildTable();
}

void RsGeorefTaskList::clearAll()
{
  mEntries.clear();
  rebuildTable();
}

int RsGeorefTaskList::runningCount() const
{
  int n = 0;
  for ( const Entry &e : mEntries )
  {
    if ( e.status == Status::Running )
      ++n;
  }
  return n;
}

RsGeorefTaskList::Entry RsGeorefTaskList::entryAt( int row ) const
{
  if ( row < 0 || row >= mEntries.size() )
    return {};
  return mEntries.at( row );
}

RsGeorefTaskList::Entry RsGeorefTaskList::entryById( int id ) const
{
  for ( const Entry &e : mEntries )
  {
    if ( e.id == id )
      return e;
  }
  return {};
}

void RsGeorefTaskList::onClearClicked()
{
  clearFinished();
}

void RsGeorefTaskList::onCancelClicked()
{
  const auto rows = mTable->selectionModel()->selectedRows();
  if ( rows.isEmpty() )
    return;
  const int row = rows.first().row();
  if ( row < 0 || row >= mEntries.size() )
    return;
  const Entry &e = mEntries.at( row );
  if ( e.status == Status::Running )
    emit cancelTaskRequested( e.id );
}

void RsGeorefTaskList::onDoubleClick( int row, int /*column*/ )
{
  if ( row < 0 || row >= mEntries.size() )
    return;
  const Entry &e = mEntries.at( row );
  if ( e.status != Status::Success || e.outputPath.isEmpty() )
    return;
  emit loadOutputRequested( e.outputPath );
  emit openOutputRequested( e.outputPath );
}

void RsGeorefTaskList::onContextMenu( const QPoint &pos )
{
  const QModelIndex idx = mTable->indexAt( pos );
  if ( !idx.isValid() )
    return;
  mTable->selectRow( idx.row() );
  const Entry e = entryAt( idx.row() );

  QMenu menu( this );
  QAction *cancelAct = menu.addAction( tr( "Cancel Task" ) );
  cancelAct->setEnabled( e.status == Status::Running );
  QAction *loadAct = menu.addAction( tr( "Load Results into Main Project" ) );
  loadAct->setEnabled( e.status == Status::Success && !e.outputPath.isEmpty() );
  QAction *chosen = menu.exec( mTable->viewport()->mapToGlobal( pos ) );
  if ( chosen == cancelAct )
    emit cancelTaskRequested( e.id );
  else if ( chosen == loadAct )
    emit loadOutputRequested( e.outputPath );
}

int RsGeorefTaskList::rowForId( int id ) const
{
  for ( int i = 0; i < mEntries.size(); ++i )
  {
    if ( mEntries.at( i ).id == id )
      return i;
  }
  return -1;
}

void RsGeorefTaskList::refreshRow( int id )
{
  const int row = rowForId( id );
  if ( row < 0 || !mTable || row >= mTable->rowCount() )
    return;
  const Entry &e = mEntries.at( row );
  if ( auto *item = mTable->item( row, 4 ) )
  {
    if ( e.status == Status::Running )
      item->setText( QString::number( int( e.progress ) ) + QLatin1Char( '%' ) );
  }
  updateSummary();
}

QString RsGeorefTaskList::kindLabel( Kind k )
{
  switch ( k )
  {
    case Kind::WarpI2I:
      return QObject::tr( "I2I Correction" );
    case Kind::WarpI2M:
      return QObject::tr( "I2M Correction" );
  }
  return QStringLiteral( "?" );
}

QString RsGeorefTaskList::statusLabel( Status s )
{
  switch ( s )
  {
    case Status::Running:
      return QObject::tr( "Running" );
    case Status::Success:
      return QObject::tr( "Done" );
    case Status::Failed:
      return QObject::tr( "Failed" );
    case Status::Cancelled:
      return QObject::tr( "Cancel" );
  }
  return QStringLiteral( "?" );
}

QColor RsGeorefTaskList::statusColor( Status s )
{
  // Shared task-state palette (Milestone F): the token layer owns these
  // values; RsJobPanel renders the same roles from the same source.
  // Static method: probe the application palette (applyDarkPalette keeps it
  // in sync with the active theme).
  const bool dark = SicnuUi::Tokens::themeIsDark( nullptr );
  switch ( s )
  {
    case Status::Running:
      return SicnuUi::Tokens::statusRunning( dark );
    case Status::Success:
      return SicnuUi::Tokens::statusOk( dark );
    case Status::Failed:
      return SicnuUi::Tokens::statusError( dark );
    case Status::Cancelled:
      return SicnuUi::Tokens::statusIdle( dark );
  }
  return SicnuUi::Tokens::statusIdle( dark );
}

void RsGeorefTaskList::rebuildTable()
{
  mTable->setRowCount( 0 );
  for ( int i = 0; i < mEntries.size(); ++i )
  {
    const Entry &e = mEntries.at( i );
    const int row = mTable->rowCount();
    mTable->insertRow( row );

    auto *idItem = new QTableWidgetItem( QString::number( e.id ) );
    idItem->setTextAlignment( Qt::AlignCenter );
    idItem->setData( Qt::UserRole, e.id );
    mTable->setItem( row, 0, idItem );

    mTable->setItem( row, 1, new QTableWidgetItem( kindLabel( e.kind ) ) );

    auto *methodItem = new QTableWidgetItem(
      e.methodLabel.isEmpty() ? e.title : e.methodLabel );
    if ( !e.sourcePath.isEmpty() )
      methodItem->setToolTip( tr( "Source: %1" ).arg( e.sourcePath ) );
    mTable->setItem( row, 2, methodItem );

    auto *st = new QTableWidgetItem( statusLabel( e.status ) );
    st->setForeground( statusColor( e.status ) );
    st->setTextAlignment( Qt::AlignCenter );
    if ( !e.detail.isEmpty() )
      st->setToolTip( e.detail );
    mTable->setItem( row, 3, st );

    QString progText = QStringLiteral( "—" );
    if ( e.status == Status::Running )
      progText = QString::number( int( e.progress ) ) + QLatin1Char( '%' );
    else if ( e.status == Status::Success )
      progText = QStringLiteral( "100%" );
    auto *progItem = new QTableWidgetItem( progText );
    progItem->setTextAlignment( Qt::AlignCenter );
    mTable->setItem( row, 4, progItem );

    auto *gcpItem = new QTableWidgetItem( e.gcpCount > 0
                                            ? QString::number( e.gcpCount )
                                            : QStringLiteral( "—" ) );
    gcpItem->setTextAlignment( Qt::AlignCenter );
    mTable->setItem( row, 5, gcpItem );

    QString rmsText = QStringLiteral( "—" );
    if ( e.rmsPx >= 0.0 )
      rmsText = QString::number( e.rmsPx, 'f', 3 );
    auto *rmsItem = new QTableWidgetItem( rmsText );
    rmsItem->setTextAlignment( Qt::AlignRight | Qt::AlignVCenter );
    mTable->setItem( row, 6, rmsItem );

    QString durText = QStringLiteral( "—" );
    if ( e.status != Status::Running && e.durationMs > 0 )
    {
      if ( e.durationMs < 1000 )
        durText = tr( "%1 ms" ).arg( e.durationMs );
      else
        durText = tr( "%1 s" ).arg( e.durationMs / 1000.0, 0, 'f', 1 );
    }
    else if ( e.status == Status::Running )
    {
      durText = QStringLiteral( "…" );
    }
    auto *durItem = new QTableWidgetItem( durText );
    durItem->setTextAlignment( Qt::AlignRight | Qt::AlignVCenter );
    mTable->setItem( row, 7, durItem );

    const QString outName = e.outputPath.isEmpty()
                              ? QStringLiteral( "—" )
                              : QFileInfo( e.outputPath ).fileName();
    auto *outItem = new QTableWidgetItem( outName );
    if ( !e.outputPath.isEmpty() )
    {
      QString tip = e.outputPath;
      if ( e.outputBytes > 0 )
        tip += tr( "\n%1 bytes" ).arg( e.outputBytes );
      outItem->setToolTip( tip );
    }
    mTable->setItem( row, 8, outItem );
  }
  updateSummary();
  mCancelBtn->setEnabled( false );
}

void RsGeorefTaskList::updateSummary()
{
  const int run = runningCount();
  int ok = 0, fail = 0;
  for ( const Entry &e : mEntries )
  {
    if ( e.status == Status::Success )
      ++ok;
    else if ( e.status == Status::Failed )
      ++fail;
  }
  mSummary->setText( tr( "Tasks: %1  |  Running %2  ·  Done %3  ·  Failed %4" )
                       .arg( mEntries.size() )
                       .arg( run )
                       .arg( ok )
                       .arg( fail ) );
  mClearBtn->setEnabled( mEntries.size() > run );
}
