// test_r22_workbench_async_runner.cpp — R22-05 W1 (#1695 item 1)
//
// Lifecycle contract for the legacy async runner pair:
//   1. A runner destroyed while its task is still running must restore the
//      override cursor it pushed (setOverrideCursor/restoreOverrideCursor
//      are an application-global stack — a stranded WaitCursor freezes the
//      whole UI's cursor shape until quit).
//   2. The parent dialog dying mid-task must not leave the cursor stranded
//      either, and the completion path must never build a QPointer from a
//      freed widget (dangling bare-pointer read).
//   3. The null-parent contract (no cursor management at all) and the
//      normal completion path must keep working.
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QPointer>
#include <QSignalSpy>
#include <QThread>
#include <QWidget>

#include <functional>

#include "app/dialogs/async_algorithm_runner.h"
#include "app/dialogs/async_gdal_runner.h"
#include "processing/framework/task_center.h"
#include "qgstaskmanager.h"

namespace
{

int fake_argc = 1;
char fake_argv0[] = "test_r22_workbench_async_runner";
char *fake_argv[] = { fake_argv0, nullptr };

QApplication *ensureApp()
{
    if ( !QApplication::instance() )
        return new QApplication( fake_argc, fake_argv );
    return static_cast<QApplication *>( QApplication::instance() );
}

// The override-cursor stack is process-global. Rebalance it around every
// case so one red assertion cannot poison the binary's other cases.
void resetOverrideCursorStack()
{
    while ( QApplication::overrideCursor() != nullptr )
        QApplication::restoreOverrideCursor();
}

void drainEvents( int ms )
{
    QElapsedTimer t;
    t.start();
    while ( t.elapsed() < ms )
        QCoreApplication::processEvents( QEventLoop::AllEvents, 20 );
}

// Spin the event loop until @p predicate holds or @p timeoutMs elapses.
bool waitFor( const std::function<bool()> &predicate, int timeoutMs )
{
    QElapsedTimer t;
    t.start();
    while ( !predicate() && t.elapsed() < timeoutMs )
        QCoreApplication::processEvents( QEventLoop::AllEvents, 20 );
    return predicate();
}

class MinimalTask : public QgsTask
{
  public:
    MinimalTask()
      : QgsTask( QStringLiteral( "test:r22_workbench_async_teardown" ) )
    {}
  protected:
    bool run() override { return true; }
};

class TestAlgorithmRunner : public AsyncAlgorithmRunner
{
  public:
    TestAlgorithmRunner( QWidget *host, QgsTask *task, long centerId )
      : AsyncAlgorithmRunner( host )
    {
        setTaskForTesting( task, centerId );
    }
};

} // namespace

TEST_CASE( "r22/#1695-1: runner destroyed mid-run restores the override cursor",
           "[r22][workbench][async_runner]" )
{
    ensureApp();
    resetOverrideCursorStack();
    QWidget host;

    SECTION( "AsyncGdalRunner destructor restores the wait cursor" )
    {
        {
            AsyncGdalRunner runner( &host );
            runner.run( [] { QThread::msleep( 3000 ); return QStringLiteral( "late" ); } );
            REQUIRE( runner.isRunning() );
            REQUIRE( QApplication::overrideCursor() != nullptr ); // WaitCursor pushed
        } // runner destroyed while the task is still running
        REQUIRE( QApplication::overrideCursor() == nullptr );
    }

    SECTION( "AsyncAlgorithmRunner destructor restores the wait cursor" )
    {
        const long taskId = sicnu::TaskCenter::instance().enqueueTask(
            QStringLiteral( "test:r22_workbench_async_teardown" ), QVariantMap(), true );
        REQUIRE( taskId > 0 );
        sicnu::TaskCenter::instance().markTaskRunning( taskId );
        MinimalTask *task = new MinimalTask();
        {
            TestAlgorithmRunner runner( &host, task, taskId );
            REQUIRE( runner.isRunning() );
            REQUIRE( QApplication::overrideCursor() != nullptr );
        } // runner destroyed while the task is still "running"
        REQUIRE( QApplication::overrideCursor() == nullptr );
        // Pre-existing teardown contract (test_ui_task_center_contract.cpp)
        // must survive the lifecycle fix.
        const auto info = sicnu::TaskCenter::instance().getTaskInfo( taskId );
        REQUIRE( info.taskId == taskId );
        REQUIRE( info.status == sicnu::TaskStatus::Canceled );
        delete task;
    }

    resetOverrideCursorStack();
}

TEST_CASE( "r22/#1695-1: parent dialog destroyed mid-run does not strand the cursor",
           "[r22][workbench][async_runner]" )
{
    ensureApp();
    resetOverrideCursorStack();

    auto *parent = new QWidget;
    auto *runner = new AsyncGdalRunner( parent ); // runner deliberately outlives parent
    runner->run( [] { QThread::msleep( 300 ); return QStringLiteral( "/tmp/r22/ok.tif" ); } );
    REQUIRE( runner->isRunning() );
    REQUIRE( QApplication::overrideCursor() != nullptr );

    delete parent; // dialog closes while the task is in flight

    // Let the task finish and the watcher's finished handler run. The guard
    // in the handler must observe a dead parent without touching freed
    // memory, and endRun() must still pop the cursor this runner pushed.
    QElapsedTimer t;
    t.start();
    while ( runner->isRunning() && t.elapsed() < 10000 )
        QCoreApplication::processEvents( QEventLoop::AllEvents, 20 );
    REQUIRE_FALSE( runner->isRunning() );
    REQUIRE( QApplication::overrideCursor() == nullptr );
    // With a dead parent the completion signal is suppressed by design
    // (nobody is left to receive it); the cursor pop is the observable part.
    delete runner;

    resetOverrideCursorStack();
}

TEST_CASE( "r22/#1695-1: null parent keeps the no-cursor contract",
           "[r22][workbench][async_runner]" )
{
    ensureApp();
    resetOverrideCursorStack();

    {
        AsyncGdalRunner runner( nullptr );
        QSignalSpy completed( &runner, &AsyncGdalRunner::completed );
        runner.run( [] { return QStringLiteral( "/tmp/r22/done.tif" ); } );
        REQUIRE( waitFor( [&completed] { return completed.count() == 1; }, 10000 ) );
        REQUIRE_FALSE( runner.isRunning() );
        REQUIRE( QApplication::overrideCursor() == nullptr ); // never pushed
    } // destructor after normal completion: nothing left to restore

    resetOverrideCursorStack();
}

TEST_CASE( "r22/#1695-1: repeated runs keep the cursor stack balanced",
           "[r22][workbench][async_runner]" )
{
    ensureApp();
    resetOverrideCursorStack();
    QWidget host;

    AsyncGdalRunner runner( &host );
    QSignalSpy completed( &runner, &AsyncGdalRunner::completed );
    for ( int i = 0; i < 2; ++i )
    {
        runner.run( [] { return QStringLiteral( "/tmp/r22/loop.tif" ); } );
        REQUIRE( QApplication::overrideCursor() != nullptr );
        drainEvents( 500 );
        REQUIRE( waitFor( [&completed, i] { return completed.count() == i + 1; }, 10000 ) );
        REQUIRE_FALSE( runner.isRunning() );
        REQUIRE( QApplication::overrideCursor() == nullptr );
    }

    resetOverrideCursorStack();
}
