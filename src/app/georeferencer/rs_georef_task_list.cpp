#include "rs_georef_task_list.h"

#include <QAbstractItemView>
#include <QColor>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
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
  mSummary = new QLabel( tr( "任务: 0" ), this );
  mSummary->setObjectName( QStringLiteral( "rsGeorefTaskSummary" ) );
  mClearBtn = new QPushButton( tr( "清空已完成" ), this );
  mClearBtn->setObjectName( QStringLiteral( "rsGeorefTaskClearBtn" ) );
  top->addWidget( mSummary, 1 );
  top->addWidget( mClearBtn );
  root->addLayout( top );

  mTable = new QTableWidget( 0, 8, this );
  mTable->setObjectName( QStringLiteral( "rsGeorefTaskTable" ) );
  mTable->setHorizontalHeaderLabels( {
    tr( "#" ),
    tr( "类型" ),
    tr( "方法" ),
    tr( "状态" ),
    tr( "GCP" ),
    tr( "RMS" ),
    tr( "耗时" ),
    tr( "输出" ),
  } );
  mTable->verticalHeader()->setVisible( false );
  mTable->verticalHeader()->setDefaultSectionSize( 24 );
  mTable->setSelectionBehavior( QAbstractItemView::SelectRows );
  mTable->setSelectionMode( QAbstractItemView::SingleSelection );
  mTable->setEditTriggers( QAbstractItemView::NoEditTriggers );
  mTable->setAlternatingRowColors( true );
  mTable->setSortingEnabled( false );
  mTable->setColumnWidth( 0, 36 );
  mTable->setColumnWidth( 1, 72 );
  mTable->setColumnWidth( 3, 64 );
  mTable->setColumnWidth( 4, 44 );
  mTable->setColumnWidth( 5, 64 );
  mTable->setColumnWidth( 6, 64 );
  mTable->horizontalHeader()->setSectionResizeMode( 2, QHeaderView::Stretch );
  mTable->horizontalHeader()->setSectionResizeMode( 7, QHeaderView::Stretch );
  mTable->setToolTip( tr( "双击成功任务可请求打开输出路径" ) );
  root->addWidget( mTable, 1 );

  connect( mClearBtn, &QPushButton::clicked, this, &RsGeorefTaskList::onClearClicked );
  connect( mTable, &QTableWidget::cellDoubleClicked,
           this, &RsGeorefTaskList::onDoubleClick );

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
  e.startedAt = QDateTime::currentDateTime();
  mEntries.prepend( e );
  rebuildTable();
  return e.id;
}

void RsGeorefTaskList::finishSuccess( int id, int durationMs, qint64 outputBytes,
                                      const QString &detail )
{
  for ( Entry &e : mEntries )
  {
    if ( e.id != id )
      continue;
    e.status = Status::Success;
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
  keep.reserve( mEntries.size() );
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

void RsGeorefTaskList::onClearClicked()
{
  clearFinished();
}

void RsGeorefTaskList::onDoubleClick( int row, int /*column*/ )
{
  if ( row < 0 || row >= mEntries.size() )
    return;
  const Entry &e = mEntries.at( row );
  if ( e.status == Status::Success && !e.outputPath.isEmpty() )
    emit openOutputRequested( e.outputPath );
}

QString RsGeorefTaskList::kindLabel( Kind k )
{
  switch ( k )
  {
    case Kind::WarpI2I:
      return QObject::tr( "I2I 校正" );
    case Kind::WarpI2M:
      return QObject::tr( "I2M 校正" );
  }
  return QStringLiteral( "?" );
}

QString RsGeorefTaskList::statusLabel( Status s )
{
  switch ( s )
  {
    case Status::Running:
      return QObject::tr( "运行中" );
    case Status::Success:
      return QObject::tr( "完成" );
    case Status::Failed:
      return QObject::tr( "失败" );
    case Status::Cancelled:
      return QObject::tr( "取消" );
  }
  return QStringLiteral( "?" );
}

QColor RsGeorefTaskList::statusColor( Status s )
{
  switch ( s )
  {
    case Status::Running:
      return QColor( QStringLiteral( "#0969da" ) );
    case Status::Success:
      return QColor( QStringLiteral( "#1a7f37" ) );
    case Status::Failed:
      return QColor( QStringLiteral( "#cf222e" ) );
    case Status::Cancelled:
      return QColor( QStringLiteral( "#6e7781" ) );
  }
  return Qt::black;
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
      methodItem->setToolTip( tr( "源: %1" ).arg( e.sourcePath ) );
    mTable->setItem( row, 2, methodItem );

    auto *st = new QTableWidgetItem( statusLabel( e.status ) );
    st->setForeground( statusColor( e.status ) );
    st->setTextAlignment( Qt::AlignCenter );
    if ( !e.detail.isEmpty() )
      st->setToolTip( e.detail );
    mTable->setItem( row, 3, st );

    auto *gcpItem = new QTableWidgetItem( e.gcpCount > 0
                                            ? QString::number( e.gcpCount )
                                            : QStringLiteral( "—" ) );
    gcpItem->setTextAlignment( Qt::AlignCenter );
    mTable->setItem( row, 4, gcpItem );

    QString rmsText = QStringLiteral( "—" );
    if ( e.rmsPx >= 0.0 )
      rmsText = QString::number( e.rmsPx, 'f', 3 );
    auto *rmsItem = new QTableWidgetItem( rmsText );
    rmsItem->setTextAlignment( Qt::AlignRight | Qt::AlignVCenter );
    mTable->setItem( row, 5, rmsItem );

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
    mTable->setItem( row, 6, durItem );

    const QString outName = e.outputPath.isEmpty()
                              ? QStringLiteral( "—" )
                              : QFileInfo( e.outputPath ).fileName();
    auto *outItem = new QTableWidgetItem( outName );
    if ( !e.outputPath.isEmpty() )
    {
      QString tip = e.outputPath;
      if ( e.outputBytes > 0 )
        tip += tr( "\n%1 字节" ).arg( e.outputBytes );
      outItem->setToolTip( tip );
    }
    mTable->setItem( row, 7, outItem );
  }
  updateSummary();
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
  mSummary->setText( tr( "任务: %1  |  运行中 %2  ·  完成 %3  ·  失败 %4" )
                       .arg( mEntries.size() )
                       .arg( run )
                       .arg( ok )
                       .arg( fail ) );
  mClearBtn->setEnabled( mEntries.size() > run );
}
