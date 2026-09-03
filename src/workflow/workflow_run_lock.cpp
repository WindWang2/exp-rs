// src/workflow/workflow_run_lock.cpp — see header for the ownership contract.
#include "workflow_run_lock.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostInfo>
#include <QLockFile>
#include <QDateTime>
#include <QRegularExpression>
#include <QTextStream>

#include <cerrno>
#include <random>

#if defined( Q_OS_UNIX )
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace sicnu::workflow {

namespace {

QString generateOwnerToken()
{
  static std::random_device rd;
  return QStringLiteral( "%1-%2" ).arg( QCoreApplication::applicationPid() )
    .arg( rd(), 8, 16, QLatin1Char( '0' ) );
}

QString ownerMetadataJson( qint64 pid )
{
  return QStringLiteral(
           R"({"pid":%1,"hostname":"%2","token":"%3","acquiredAt":"%4"})" )
    .arg( pid )
    .arg( QHostInfo::localHostName() )
    .arg( generateOwnerToken() )
    .arg( QDateTime::currentDateTimeUtc().toString( Qt::ISODateWithMs ) );
}

/// Extracts "pid":N from the owner metadata (diagnostics only).
qint64 pidFromOwnerFile( const QString &lockFilePath )
{
  QFile f( lockFilePath );
  if ( !f.open( QIODevice::ReadOnly ) )
    return 0;
  const QRegularExpression re( QStringLiteral( "\"pid\"\\s*:\\s*(\\d+)" ) );
  const QRegularExpressionMatch m = re.match( QString::fromUtf8( f.readAll() ) );
  return m.hasMatch() ? m.captured( 1 ).toLongLong() : 0;
}

} // namespace

QString WorkflowRunLock::lockPathForRun( const QString &checkpointDirectory,
                                         const std::string &runId )
{
  return QDir( checkpointDirectory ).filePath(
    QStringLiteral( "checkpoint_%1.lock" ).arg( QString::fromStdString( runId ) ) );
}

WorkflowRunLock::WorkflowRunLock( const QString &lockFilePath )
  : m_path( lockFilePath )
{
}

WorkflowRunLock::~WorkflowRunLock()
{
  release();
}

bool WorkflowRunLock::isHeld() const
{
#if defined( Q_OS_UNIX )
  return m_fd >= 0;
#else
  return m_qtLock && m_qtLock->isLocked();
#endif
}

WorkflowRunLock::TryResult WorkflowRunLock::tryAcquire( QString *heldByPid )
{
  if ( isHeld() )
    return TryResult::Acquired; // already ours

  if ( heldByPid )
    heldByPid->clear();

  // The lock can be acquired before the run's FIRST checkpoint exists, so
  // the checkpoint directory may not exist yet — create it (same directory
  // saveCheckpoint would create).
  if ( !QDir().mkpath( QFileInfo( m_path ).absolutePath() ) )
    return TryResult::Error;

#if defined( Q_OS_UNIX )
  const int fd = ::open( m_path.toUtf8().constData(), O_CREAT | O_RDWR | O_CLOEXEC, 0666 );
  if ( fd < 0 )
    return TryResult::Error;
  if ( ::flock( fd, LOCK_EX | LOCK_NB ) != 0 )
  {
    const int err = errno;
    ::close( fd );
    if ( err == EWOULDBLOCK )
    {
      if ( heldByPid )
        *heldByPid = QString::number( pidFromOwnerFile( m_path ) );
      return TryResult::HeldByLiveOwner;
    }
    return TryResult::Error;
  }
  // We own the run. Record owner metadata (diagnostics only: liveness is the
  // flock itself, so a truncated/missing metadata file can never widen the
  // ownership).
  const QByteArray meta = ownerMetadataJson( QCoreApplication::applicationPid() ).toUtf8();
  ( void )::ftruncate( fd, 0 );
  ( void )::write( fd, meta.constData(), static_cast<size_t>( meta.size() ) );
  m_fd = fd;
  return TryResult::Acquired;
#else
  QDir().mkpath( QFileInfo( m_path ).absolutePath() );
  auto qtLock = std::make_unique<QLockFile>( m_path );
  qtLock->setStaleLockTime( 0 ); // liveness comes from the holder check, never from age
  if ( !qtLock->tryLock( 0 ) )
  {
    qint64 pid = 0;
    QString hostname, app;
    if ( qtLock->getLockInfo( &pid, &hostname, &app ) && heldByPid )
      *heldByPid = QString::number( pid );
    return TryResult::HeldByLiveOwner;
  }
  QFile f( m_path );
  if ( f.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
  {
    f.write( ownerMetadataJson( QCoreApplication::applicationPid() ).toUtf8() );
    f.close();
  }
  m_qtLock = std::move( qtLock );
  return TryResult::Acquired;
#endif
}

void WorkflowRunLock::release()
{
#if defined( Q_OS_UNIX )
  if ( m_fd >= 0 )
  {
    ::flock( m_fd, LOCK_UN );
    ::close( m_fd );
    m_fd = -1;
  }
#else
  if ( m_qtLock )
  {
    m_qtLock->unlock();
    m_qtLock.reset();
  }
#endif
}

QString WorkflowRunLock::ownerInfoLine() const
{
  QFile f( m_path );
  if ( !f.open( QIODevice::ReadOnly ) )
    return QString();
  return QString::fromUtf8( f.readAll() ).trimmed();
}

WorkflowRunLock::OwnerProbe WorkflowRunLock::probeOwner( const QString &lockFilePath )
{
  OwnerProbe probe;
#if defined( Q_OS_UNIX )
  const int fd = ::open( lockFilePath.toUtf8().constData(), O_RDWR | O_CLOEXEC );
  if ( fd < 0 )
  {
    probe.state = OwnerProbe::State::NoHolder;
    return probe;
  }
  if ( ::flock( fd, LOCK_EX | LOCK_NB ) == 0 )
  {
    ::flock( fd, LOCK_UN );
    ::close( fd );
    probe.state = OwnerProbe::State::NoHolder;
    return probe;
  }
  ::close( fd );
  probe.state = OwnerProbe::State::LiveOwner;
  probe.pid = pidFromOwnerFile( lockFilePath );
  return probe;
#else
  QLockFile probeLock( lockFilePath );
  probeLock.setStaleLockTime( 0 );
  if ( probeLock.tryLock( 0 ) )
  {
    probeLock.unlock();
    probe.state = OwnerProbe::State::NoHolder;
    return probe;
  }
  qint64 pid = 0;
  QString hostname, app;
  if ( probeLock.getLockInfo( &pid, &hostname, &app ) )
  {
    probe.state = OwnerProbe::State::LiveOwner;
    probe.pid = pid;
  }
  else
  {
    probe.state = OwnerProbe::State::Unknown;
  }
  return probe;
#endif
}

} // namespace sicnu::workflow
