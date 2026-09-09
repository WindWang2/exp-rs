// Workbench 5.0 — WorkbenchHost registry/switcher contract (Milestone A)
#include <catch2/catch_test_macros.hpp>

#include "app/workbench/workbench_host.h"
#include "app/workbench/adapters.h"

#include <QApplication>
#include <QCoreApplication>
#include <QVariantMap>
#include <QWidget>

namespace
{

int fake_argc = 1;
char fake_argv0[] = "test_workbench_host";
char *fake_argv[] = { fake_argv0, nullptr };

QCoreApplication *ensureApp()
{
  static QApplication *app = nullptr;
  // QApplication (not the core variant): the lifecycle tests construct real
  // QWidget surfaces for the window-getter hook.
  if ( !app && !QCoreApplication::instance() )
    app = new QApplication( fake_argc, fake_argv );
  return QCoreApplication::instance();
}

/// Minimal fake bench for registry semantics (no widgets).
class FakeBench : public sicnu::app::IWorkbench
{
  public:
    FakeBench( QString id, int *activated = nullptr )
        : m_id( std::move( id ) ), m_activated( activated ) {}

    QString id() const override { return m_id; }
    QString title() const override { return m_id; }
    QIcon icon() const override { return QIcon(); }
    QWidget *primaryWidget() override { return nullptr; }
    void activate() override
    {
      m_active = true;
      if ( m_activated )
        ++*m_activated;
    }
    void deactivate() override { m_active = false; }
    bool isActive() const override { return m_active; }
    bool isDirty() const override { return m_dirty; }
    bool requestClose() override { return m_allowClose; }
    QVariantMap saveState() const override { return { { "k", m_stateValue } }; }
    void restoreState( const QVariantMap &state ) override { m_stateValue = state.value( "k" ).toInt(); }
    sicnu::app::WorkbenchFeatures features() const override
    {
      return sicnu::app::WorkbenchFeature::ExternalWindow;
    }

    bool m_dirty = false;
    bool m_allowClose = true;
    int m_stateValue = 0;

  private:
    QString m_id;
    int *m_activated = nullptr;
    bool m_active = false;
};

} // namespace

TEST_CASE( "WorkbenchHost: registration rejects duplicates and empty ids", "[workbench][contract]" )
{
  ensureApp();
  sicnu::app::WorkbenchHost host;

  FakeBench a{ "map" };
  REQUIRE( host.registerWorkbench( &a ) );
  REQUIRE_FALSE( host.registerWorkbench( &a ) );          // duplicate id

  FakeBench empty{ QString() };
  REQUIRE_FALSE( host.registerWorkbench( &empty ) );      // empty id

  REQUIRE( host.workbenchIds() == QStringList{ QStringLiteral( "map" ) } );
}

TEST_CASE( "WorkbenchHost: activation switches, deactivates and signals", "[workbench]" )
{
  ensureApp();
  sicnu::app::WorkbenchHost host;
  int mapActivations = 0;
  FakeBench map( "map", &mapActivations );
  FakeBench classify{ "classify" };
  REQUIRE( host.registerWorkbench( &map ) );
  REQUIRE( host.registerWorkbench( &classify ) );

  QString changedTo;
  QString changedFrom;
  QObject::connect( &host, &sicnu::app::WorkbenchHost::activeWorkbenchChanged,
                    [&]( const QString &to, const QString &from ) {
                      changedTo = to;
                      changedFrom = from;
                    } );

  REQUIRE( host.activate( "map" ) );
  REQUIRE( map.isActive() );
  REQUIRE( host.activeWorkbenchId() == "map" );
  REQUIRE( mapActivations == 1 );

  REQUIRE( host.activate( "classify" ) );
  REQUIRE( classify.isActive() );
  REQUIRE_FALSE( map.isActive() );   // previous bench deactivated
  REQUIRE( changedTo == "classify" );
  REQUIRE( changedFrom == "map" );

  REQUIRE_FALSE( host.activate( "nope" ) ); // unknown id ignored
  REQUIRE( host.activeWorkbenchId() == "classify" );
}

TEST_CASE( "WorkbenchHost: dirty query and close semantics", "[workbench][contract]" )
{
  ensureApp();
  sicnu::app::WorkbenchHost host;
  FakeBench bench{ "classify" };
  REQUIRE( host.registerWorkbench( &bench ) );

  bench.m_dirty = false;
  REQUIRE_FALSE( host.activeWorkbench() ); // nothing active yet
  REQUIRE( host.activate( "classify" ) );
  REQUIRE( host.activeWorkbench() );
  REQUIRE_FALSE( host.activeWorkbench()->isDirty() );

  bench.m_dirty = true;
  REQUIRE( host.activeWorkbench()->isDirty() );

  bench.m_allowClose = false;
  REQUIRE_FALSE( host.activeWorkbench()->requestClose() ); // dirty bench may refuse
}

TEST_CASE( "WorkbenchHost: state save/restore round-trips through the bench", "[workbench]" )
{
  ensureApp();
  FakeBench bench{ "layout" };
  bench.m_stateValue = 42;
  const QVariantMap state = bench.saveState();
  bench.m_stateValue = 0;
  bench.restoreState( state );
  REQUIRE( bench.m_stateValue == 42 );
}

TEST_CASE( "ExternalWindowWorkbench: activate runs the lazy opener once and tracks lifetime",
           "[workbench][adapters]" )
{
  ensureApp();
  int opens = 0;
  sicnu::app::ExternalWindowWorkbench bench(
      QStringLiteral( "obia" ), QStringLiteral( "对象级分类" ), QStringLiteral( "seg_ent_tion" ),
      [&opens]() -> QWidget * {
        ++opens;
        return nullptr; // widget lifetime tracking tolerated null (headless)
      } );

  REQUIRE( bench.id() == "obia" );
  REQUIRE_FALSE( bench.isActive() );
  bench.activate();
  REQUIRE( opens == 1 );
  REQUIRE( bench.isActive() );
  bench.activate();
  REQUIRE( opens == 2 ); // opener is idempotent show/raise — may repeat safely
  bench.deactivate();
  REQUIRE_FALSE( bench.isActive() );
}

TEST_CASE( "MapWorkbench: embedded bench reports map features", "[workbench][adapters]" )
{
  ensureApp();
  sicnu::app::MapWorkbench map( nullptr );
  REQUIRE( map.id() == "map" );
  REQUIRE( map.features().testFlag( sicnu::app::WorkbenchFeature::MapCanvas ) );
  REQUIRE( map.features().testFlag( sicnu::app::WorkbenchFeature::LayerTree ) );
}

// ── Workbench 6.0 Milestone F: unified external-bench lifecycle (#813) ─────

TEST_CASE( "ExternalWindowWorkbench: lifecycle hooks expose dirty, in-flight and cancel (#813)",
           "[workbench][ux6]" )
{
  ensureApp();
  QWidget window;

  bool windowAlive = false;
  bool dirty = false;
  bool inFlight = false;
  int cancels = 0;
  int closes = 0;

  sicnu::app::ExternalWindowWorkbench bench(
      QStringLiteral( "classify" ), QStringLiteral( "分类工作区" ),
      QStringLiteral( "su_ervised" ), [] {} );
  bench.setWindowGetter( [&]() -> QWidget * { return windowAlive ? &window : nullptr; } );
  bench.setDirtyFn( [&] { return dirty; } );
  bench.setInFlightFn( [&] { return inFlight; } );
  bench.setCancelFn( [&]() -> bool {
    if ( !inFlight )
      return false;
    ++cancels;
    inFlight = false;
    return true;
  } );
  bench.setCloseFn( [&] {
    ++closes;
    return true;
  } );

  // No window yet: nothing dirty, nothing running, cancel refuses.
  CHECK_FALSE( bench.isDirty() );
  CHECK_FALSE( bench.hasInFlightCompute() );
  CHECK_FALSE( bench.requestCancel() );

  bench.activate(); // opener runs → window exists
  CHECK( bench.isActive() );

  dirty = true;
  CHECK( bench.isDirty() ); // #813: the shell close policy can see dirty state
  dirty = false;

  // In-flight compute is visible and cancel routes through the seam once.
  inFlight = true;
  CHECK( bench.hasInFlightCompute() );
  CHECK( bench.requestCancel() );
  CHECK( cancels == 1 );
  CHECK_FALSE( bench.hasInFlightCompute() );
  CHECK_FALSE( bench.requestCancel() ); // nothing running anymore

  // Close delegation runs the window's own confirmation path.
  CHECK( bench.requestClose() );
  CHECK( closes == 1 );
}

TEST_CASE( "ExternalWindowWorkbench: absent hooks degrade to a safe default (#813)",
           "[workbench][ux6]" )
{
  ensureApp();
  sicnu::app::ExternalWindowWorkbench bench(
      QStringLiteral( "obia" ), QStringLiteral( "对象级分类" ),
      QStringLiteral( "seg_ent_tion" ), [] {} );
  // No hooks installed at all: the lifecycle questions must still answer
  // safely (clean, idle, nothing to cancel, close allowed).
  CHECK_FALSE( bench.isDirty() );
  CHECK_FALSE( bench.hasInFlightCompute() );
  CHECK_FALSE( bench.requestCancel() );
  CHECK( bench.requestClose() );
}
