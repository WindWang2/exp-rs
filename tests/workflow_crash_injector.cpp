// tests/workflow_crash_injector.cpp — see header. QProcess + barrier-file
// protocol toward workflow_crash_helper; jsoncpp read-back of checkpoints.
#include "workflow_crash_injector.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QProcess>

#include <json/json.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <sstream>
#include <thread>

#include "workflow/workflow_checkpoint.h"

#ifdef SICNU_TEST_CRASH_HELPER
#define CRASH_HELPER_STRING SICNU_TEST_CRASH_HELPER
#else
#error "SICNU_TEST_CRASH_HELPER must be defined by the test target"
#endif

namespace sicnu::workflow {
namespace {

/// Extracts "RUN <runId> <pipelineId>" from accumulated stdout into @p out.
bool extractRunLine( const QString &buffer, QString *out )
{
    for ( const QString &line : buffer.split( QLatin1Char( '\n' ), Qt::SkipEmptyParts ) )
    {
        if ( line.startsWith( QStringLiteral( "RUN " ) ) )
        {
            *out = line.section( QLatin1Char( ' ' ), 1, 1 );
            return true;
        }
    }
    return false;
}


bool waitFor( const std::function<bool()> &predicate, int timeoutMs )
{
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
    while ( QDateTime::currentMSecsSinceEpoch() < deadline )
    {
        if ( predicate() )
            return true;
        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    }
    return predicate();
}

} // namespace

WorkflowCrashInjector::WorkflowCrashInjector( const QString &scratchDir )
    : m_dir( scratchDir )
{
    QDir().mkpath( m_dir );
}

WorkflowCrashInjector::~WorkflowCrashInjector()
{
    killChild();
}

bool WorkflowCrashInjector::parseRunLine( const QString &buffer )
{
    QString id;
    if ( extractRunLine( buffer, &id ) && !id.isEmpty() )
    {
        m_runIdLine = id;
        return true;
    }
    return false;
}

QString WorkflowCrashInjector::resolveRunId() const
{
    if ( !m_runIdLine.isEmpty() )
        return m_runIdLine;
    // Disk fallback: the scratch dir is per-test fresh, so at most one run's
    // checkpoint can exist.
    const QStringList checkpoints = QDir( m_dir ).entryList(
        QStringList{ QStringLiteral( "checkpoint_*.json" ) }, QDir::Files );
    if ( checkpoints.size() == 1 )
    {
        const QString stem = checkpoints.front().chopped( strlen( ".json" ) );
        return stem.mid( strlen( "checkpoint_" ) );
    }
    return QString();
}

QString WorkflowCrashInjector::helperPath()
{
    return QStringLiteral( CRASH_HELPER_STRING );
}

QString WorkflowCrashInjector::barrierPath( const std::string &barrierName ) const
{
    return QDir( m_dir ).filePath( QStringLiteral( "barrier_%1" ).arg( barrierName.c_str() ) );
}

bool WorkflowCrashInjector::barrierExists( const std::string &barrierName ) const
{
    return QFile::exists( barrierPath( barrierName ) );
}

bool WorkflowCrashInjector::spawnUntilBarrier( const QStringList &args,
                                               const std::string &barrierName, int timeoutMs )
{
    killChild();
    m_process = std::make_unique<QProcess>();
    m_process->setWorkingDirectory( m_dir );
    m_process->start( helperPath(), args );
    if ( !m_process->waitForStarted( 15000 ) )
    {
        std::fprintf( stderr, "[crash-injector] %s failed to start: %s\n",
                      helperPath().toUtf8().constData(),
                      m_process->errorString().toUtf8().constData() );
        return false;
    }
    // Accumulate the child's stdout while polling: this thread runs no event
    // loop, so each pump must drain the pipe into our own buffer or the RUN
    // line would be lost. The barrier can beat the child's main thread to the
    // print (executors run on JobEngine workers), so keep draining until the
    // line shows up or a short bounded window closes.
    const qint64 runLineDeadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
    bool barrierSeen = false;
    while ( QDateTime::currentMSecsSinceEpoch() < runLineDeadline )
    {
        m_process->waitForReadyRead( 5 );
        m_stdoutBuffer += QString::fromUtf8( m_process->readAllStandardOutput() );
        if ( m_stdoutBuffer.contains( QLatin1Char( '\n' ) ) )
            barrierSeen = barrierExists( barrierName );
        if ( barrierSeen && m_runIdLine.isEmpty() && parseRunLine( m_stdoutBuffer ) )
            break;
        if ( barrierSeen && !m_runIdLine.isEmpty() )
            break;
        if ( !barrierSeen && barrierExists( barrierName ) )
        {
            // Give the child's main thread a bounded grace period to flush the
            // RUN line that precedes the barrier in program order.
            barrierSeen = true;
            const qint64 grace = QDateTime::currentMSecsSinceEpoch() + 5000;
            while ( QDateTime::currentMSecsSinceEpoch() < grace )
            {
                m_process->waitForReadyRead( 5 );
                m_stdoutBuffer += QString::fromUtf8( m_process->readAllStandardOutput() );
                if ( parseRunLine( m_stdoutBuffer ) )
                    break;
                std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
            }
            break;
        }
        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    }
    if ( barrierSeen && m_runIdLine.isEmpty() )
        parseRunLine( m_stdoutBuffer );
    if ( !barrierSeen )
    {
        const int exitCode = m_process->exitCode();
        const bool crashed = m_process->exitStatus() == QProcess::CrashExit;
        const QString errOut = QString::fromUtf8( m_process->readAllStandardError() ).trimmed();
        killChild();
        std::fprintf( stderr,
                      "[crash-injector] barrier '%s' never appeared; child exit=%d crash=%d\n"
                      "  stderr: %s\n",
                      barrierName.c_str(), exitCode, crashed ? 1 : 0,
                      errOut.toUtf8().constData() );
        return false;
    }
    return true;
}

void WorkflowCrashInjector::killChild()
{
    if ( !m_process )
        return;
    if ( m_process->state() != QProcess::NotRunning )
    {
        m_process->kill(); // SIGKILL on POSIX: the injection point itself
        m_process->waitForFinished( 10000 );
    }
    m_process.reset();
}

bool WorkflowCrashInjector::childRunning()
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

QString WorkflowCrashInjector::runIdLine() const
{
    return m_runIdLine;
}

QString WorkflowCrashInjector::checkpointPath( const std::string &runId ) const
{
    return QDir( m_dir ).filePath(
        QStringLiteral( "checkpoint_%1.json" ).arg( QString::fromStdString( runId ) ) );
}

QString WorkflowCrashInjector::archivedCheckpointPath( const std::string &runId ) const
{
    return QDir( QDir( m_dir ).filePath( QStringLiteral( "history" ) ) )
        .filePath( QStringLiteral( "checkpoint_%1.json" ).arg( QString::fromStdString( runId ) ) );
}

Json::Value WorkflowCrashInjector::readCheckpointJson( const std::string &runId ) const
{
    QString path = checkpointPath( runId );
    if ( !QFile::exists( path ) )
        path = archivedCheckpointPath( runId ); // completed runs archive to history/
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly | QIODevice::Text ) )
        return Json::Value( Json::nullValue );
    const QByteArray data = file.readAll();
    file.close();
    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errs;
    std::istringstream stream( data.toStdString() );
    if ( !Json::parseFromStream( builder, stream, &root, &errs ) )
        return Json::Value( Json::nullValue );
    return root;
}

QStringList WorkflowCrashInjector::committedStepsOnDisk( const std::string &runId ) const
{
    const Json::Value root = readCheckpointJson( runId );
    QStringList committed;
    if ( !root.isMember( "stepPlans" ) || !root["stepPlans"].isArray() )
        return committed;
    for ( const Json::Value &plan : root["stepPlans"] )
    {
        if ( plan.isMember( "stepId" ) && plan.isMember( "status" )
             && plan["status"].isString() && plan["status"].asString() == "Completed" )
            committed << QString::fromStdString( plan["stepId"].asString() );
    }
    return committed;
}

QString WorkflowCrashInjector::checkpointStateOnDisk( const std::string &runId ) const
{
    const Json::Value root = readCheckpointJson( runId );
    if ( root.isMember( "state" ) && root["state"].isString() )
        return QString::fromStdString( root["state"].asString() );
    return QString();
}

int WorkflowCrashInjector::tmpResidueCount() const
{
    return QDir( m_dir )
        .entryList( QStringList{ QStringLiteral( "checkpoint_*.json.tmp.*" ) }, QDir::Files )
        .size();
}

bool WorkflowCrashInjector::checkpointLoads( const std::string &runId ) const
{
    QString path = checkpointPath( runId );
    if ( !QFile::exists( path ) )
        path = archivedCheckpointPath( runId );
    QString err;
    return WorkflowCheckpointManager().loadCheckpoint( path, &err ) != nullptr;
}

} // namespace sicnu::workflow
