// test_ui_callback.cpp — worker→UI completion delivery helper (Workbench 9.0 M0)
#include <catch2/catch_test_macros.hpp>

#include "app/workbench/marshal_ui.h"

#include <QCoreApplication>
#include <QThread>

#include <atomic>
#include <functional>
#include <thread>

namespace
{

int fake_argc = 1;
char fake_argv0[] = "test_ui_callback";
char *fake_argv[] = { fake_argv0, nullptr };

QCoreApplication *ensureApp()
{
  static QCoreApplication *app = nullptr;
  if ( !app && !QCoreApplication::instance() )
    app = new QCoreApplication( fake_argc, fake_argv );
  return app;
}

/// Runs `job` on a scratch std::thread and joins before returning, so every
/// test leaves the process single-threaded again. (A QThread would need a
/// running event loop to deliver started(); marshalTo only requires some
/// foreign poster thread.)
void runOnWorkerThread( const std::function<void()> &job )
{
  std::thread worker( job );
  worker.join();
}

} // namespace

TEST_CASE( "marshalTo runs the callback on the receiver's thread", "[ui_callback][m0]" )
{
  ensureApp();
  QObject receiver;

  Qt::HANDLE seenThread = nullptr;
  std::atomic<bool> called{ false };
  // Callback posted from a scratch worker thread must execute on the thread
  // that owns the receiver (here: the test main thread) — never on the poster.
  runOnWorkerThread( [&] {
    sicnu::app::ui_callback::marshalTo( &receiver, [&] {
      seenThread = QThread::currentThreadId();
      called.store( true );
    } );
  } );

  QCoreApplication::processEvents();
  QCoreApplication::sendPostedEvents( &receiver );
  REQUIRE( called.load() );
  REQUIRE( seenThread == QThread::currentThreadId() );
}

TEST_CASE( "marshalTo discards the callback when the receiver dies first",
           "[ui_callback][m0]" )
{
  ensureApp();

  std::atomic<bool> called{ false };
  {
    QObject receiver;
    sicnu::app::ui_callback::marshalTo( &receiver, [&called] { called.store( true ); } );
    // receiver destroyed here, before the queued call is delivered
  }

  QCoreApplication::processEvents();
  QCoreApplication::sendPostedEvents( nullptr );
  REQUIRE_FALSE( called.load() );
}

TEST_CASE( "marshalTo with a null receiver is a no-op", "[ui_callback][m0]" )
{
  ensureApp();
  std::atomic<bool> called{ false };
  sicnu::app::ui_callback::marshalTo( nullptr, [&called] { called.store( true ); } );
  QCoreApplication::processEvents();
  REQUIRE_FALSE( called.load() );
}
