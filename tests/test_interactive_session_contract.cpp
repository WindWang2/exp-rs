// Workbench 5.0 — InteractiveSession contract (Milestone H, batch 1)
#include <catch2/catch_test_macros.hpp>

#include "app/workbench/interactive_session.h"
#include "app/workbench/session_adapters.h"

#include <QApplication>
#include <QFile>
#include <QString>

namespace
{

int fake_argc = 1;
char fake_argv0[] = "test_interactive_session_contract";
char *fake_argv[] = { fake_argv0, nullptr };

QCoreApplication *ensureApp()
{
  static QCoreApplication *app = nullptr;
  if ( !app && !QCoreApplication::instance() )
    app = new QCoreApplication( fake_argc, fake_argv );
  return QCoreApplication::instance();
}

/// Fake session: pins the shared lifecycle surface the shell relies on.
class FakeSession : public sicnu::app::InteractiveSession
{
  public:
    QString sessionId() const override { return QStringLiteral( "fake" ); }
    bool isDirty() const override { return m_dirty; }
    void clearDirty() override { m_dirty = false; }
    bool hasInFlightCompute() const override { return m_running; }
    bool requestCancel() override
    {
      if ( !m_running )
        return false;
      m_running = false;
      emit computeFinished( false );
      return true;
    }
    bool requestClose() override { return !m_dirty; }
    QVariantMap saveSessionState() const override { return { { "step", 3 } }; }

    bool m_dirty = false;
    bool m_running = false;
};

QString readSource( const QString &relativePath )
{
  QFile f( QStringLiteral( CMAKE_SOURCE_DIR ) + QStringLiteral( "/" ) + relativePath );
  if ( f.open( QIODevice::ReadOnly | QIODevice::Text ) )
    return QString::fromUtf8( f.readAll() );
  return {};
}

} // namespace

TEST_CASE( "InteractiveSession: dirty state gates close, cancel only when running",
           "[interactive_session][contract]" )
{
  ensureApp();
  FakeSession session;

  session.m_dirty = true;
  REQUIRE( session.isDirty() );
  REQUIRE_FALSE( session.requestClose() ); // dirty refuses close
  session.clearDirty();
  REQUIRE( session.requestClose() );

  REQUIRE_FALSE( session.requestCancel() ); // nothing running
  session.m_running = true;
  REQUIRE( session.hasInFlightCompute() );
  REQUIRE( session.requestCancel() );
  REQUIRE_FALSE( session.hasInFlightCompute() );
}

TEST_CASE( "InteractiveSession: state persistence round-trips", "[interactive_session]" )
{
  ensureApp();
  FakeSession session;
  REQUIRE( session.saveSessionState().value( "step" ).toInt() == 3 );
  session.restoreSessionState( session.saveSessionState() ); // must be safe
}

TEST_CASE( "ClassifySessionAdapter: headless opener degrades gracefully",
           "[interactive_session][classify]" )
{
  ensureApp();
  sicnu::app::ClassifySessionAdapter adapter( []() -> QgsClassificationMainWindow * {
    return nullptr; // headless test environment cannot construct the lab
  } );

  REQUIRE( adapter.sessionId() == QLatin1String( "classify" ) );
  REQUIRE_FALSE( adapter.isDirty() );
  REQUIRE_FALSE( adapter.hasInFlightCompute() );
  REQUIRE_FALSE( adapter.requestCancel() );
  REQUIRE( adapter.requestClose() ); // nothing to close → success
}

TEST_CASE( "InteractiveSession: classification lab exposes the TaskCenter seam",
           "[interactive_session][contract][source_scan]" )
{
  ensureApp();
  const QString header =
      readSource( QStringLiteral( "src/app/classification/qgsclassificationmainwindow.h" ) );
  REQUIRE_FALSE( header.isEmpty() );
  // Lifecycle accessors required by the shared contract:
  REQUIRE( header.contains( QStringLiteral( "isSessionDirty" ) ) );
  REQUIRE( header.contains( QStringLiteral( "hasInFlightCompute" ) ) );
  REQUIRE( header.contains( QStringLiteral( "cancelInFlightCompute" ) ) );

  // The window owns a GuiJobHandle - TaskCenter thin-client law.
  REQUIRE( header.contains( QStringLiteral( "GuiJobHandle" ) ) );
}
