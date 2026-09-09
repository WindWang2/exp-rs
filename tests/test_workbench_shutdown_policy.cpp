// Workbench 7.0 — shutdown/switch policy (goal §A: quit, project switch and
// external-window close must consume the bench lifecycle hooks; no silent drop)
#include <catch2/catch_test_macros.hpp>

#include "app/workbench/shutdown_policy.h"
#include "app/workbench/workbench_host.h"

#include <QApplication>
#include <QCoreApplication>

namespace
{

int fake_argc = 1;
char fake_argv0[] = "test_workbench_shutdown_policy";
char *fake_argv[] = { fake_argv0, nullptr };

QCoreApplication *ensureApp()
{
  static QApplication *app = nullptr;
  if ( !app && !QCoreApplication::instance() )
    app = new QApplication( fake_argc, fake_argv );
  return QCoreApplication::instance();
}

/// Fake bench recording lifecycle-hook traffic.
class LifecycleBench : public sicnu::app::IWorkbench
{
  public:
    LifecycleBench( QString id, QString title )
        : m_id( std::move( id ) ), m_title( std::move( title ) ) {}

    QString id() const override { return m_id; }
    QString title() const override { return m_title; }
    QIcon icon() const override { return QIcon(); }
    QWidget *primaryWidget() override { return nullptr; }
    void activate() override { m_active = true; }
    void deactivate() override { m_active = false; }
    bool isActive() const override { return m_active; }
    bool isDirty() const override { return m_dirty; }
    bool hasInFlightCompute() const override { return m_inFlight; }
    bool requestCancel() override
    {
      ++m_cancelAsked;
      if ( !m_inFlight || !m_acceptCancel )
        return false;
      ++m_cancelAccepted;
      m_inFlight = false;
      return true;
    }
    bool requestClose() override
    {
      ++m_closeAsked;
      return m_allowClose;
    }
    sicnu::app::WorkbenchFeatures features() const override
    {
      return sicnu::app::WorkbenchFeature::ExternalWindow;
    }

    bool m_dirty = false;
    bool m_inFlight = false;
    bool m_acceptCancel = true;
    bool m_allowClose = true;
    int m_cancelAsked = 0;
    int m_cancelAccepted = 0;
    int m_closeAsked = 0;

  private:
    QString m_id;
    QString m_title;
    bool m_active = false;
};

} // namespace

TEST_CASE( "ShutdownPlan: pure projection", "[shutdown][policy]" )
{
  ensureApp();

  SECTION( "clean benches and no tasks → empty plan" )
  {
    QVector<sicnu::app::WorkbenchShutdownFacts> facts;
    sicnu::app::WorkbenchShutdownFacts clean;
    clean.benchId = "map";
    facts.append( clean );
    const sicnu::app::ShutdownPlan plan =
      sicnu::app::planWorkbenchShutdown( facts, 0 );
    REQUIRE( plan.isEmpty() );
    REQUIRE_FALSE( plan.needsConfirmation() );
  }

  SECTION( "dirty bench lands in dirtyBenches" )
  {
    QVector<sicnu::app::WorkbenchShutdownFacts> facts;
    sicnu::app::WorkbenchShutdownFacts dirty;
    dirty.benchId = "classify";
    dirty.title = QStringLiteral( "分类工作区" );
    dirty.dirty = true;
    facts.append( dirty );
    const sicnu::app::ShutdownPlan plan = sicnu::app::planWorkbenchShutdown( facts, 0 );
    REQUIRE( plan.dirtyBenches == QStringList{ QStringLiteral( "分类工作区" ) } );
    REQUIRE( plan.inFlightBenches.isEmpty() );
    REQUIRE( plan.needsConfirmation() );
  }

  SECTION( "in-flight bench and background tasks combine" )
  {
    QVector<sicnu::app::WorkbenchShutdownFacts> facts;
    sicnu::app::WorkbenchShutdownFacts busy;
    busy.benchId = "classify";
    busy.title = "classify";
    busy.inFlight = true;
    facts.append( busy );
    const sicnu::app::ShutdownPlan plan = sicnu::app::planWorkbenchShutdown( facts, 3 );
    REQUIRE( plan.inFlightBenches == QStringList{ "classify" } );
    REQUIRE( plan.runningTaskCount == 3 );
  }

  SECTION( "negative task count clamps to zero" )
  {
    const sicnu::app::ShutdownPlan plan =
      sicnu::app::planWorkbenchShutdown( {}, -5 );
    REQUIRE( plan.runningTaskCount == 0 );
    REQUIRE( plan.isEmpty() );
  }

  SECTION( "ordering follows registration order" )
  {
    QVector<sicnu::app::WorkbenchShutdownFacts> facts;
    sicnu::app::WorkbenchShutdownFacts a;
    a.benchId = "a";
    a.title = "a";
    a.dirty = true;
    sicnu::app::WorkbenchShutdownFacts b;
    b.benchId = "b";
    b.title = "b";
    b.dirty = true;
    b.inFlight = true;
    facts.append( a );
    facts.append( b );
    const sicnu::app::ShutdownPlan plan = sicnu::app::planWorkbenchShutdown( facts, 0 );
    REQUIRE( plan.dirtyBenches == QStringList{ "a", "b" } );
    REQUIRE( plan.inFlightBenches == QStringList{ "b" } );
  }
}

TEST_CASE( "collectWorkbenchShutdownFacts reads every registered bench",
           "[shutdown][policy][host]" )
{
  ensureApp();
  sicnu::app::WorkbenchHost host;

  REQUIRE( sicnu::app::collectWorkbenchShutdownFacts( &host ).isEmpty() );

  LifecycleBench map{ "map", "map" };
  LifecycleBench classify{ "classify", QStringLiteral( "分类工作区" ) };
  REQUIRE( host.registerWorkbench( &map ) );
  REQUIRE( host.registerWorkbench( &classify ) );
  classify.m_dirty = true;
  classify.m_inFlight = true;

  const QVector<sicnu::app::WorkbenchShutdownFacts> facts =
    sicnu::app::collectWorkbenchShutdownFacts( &host );
  REQUIRE( facts.size() == 2 );
  REQUIRE_FALSE( facts[0].dirty );
  REQUIRE_FALSE( facts[0].inFlight );
  REQUIRE( facts[1].benchId == "classify" );
  REQUIRE( facts[1].title == QStringLiteral( "分类工作区" ) );
  REQUIRE( facts[1].dirty );
  REQUIRE( facts[1].inFlight );

  REQUIRE( sicnu::app::collectWorkbenchShutdownFacts( nullptr ).isEmpty() );
}

TEST_CASE( "cancelInFlightBenches routes only through in-flight benches",
           "[shutdown][policy][host]" )
{
  ensureApp();
  sicnu::app::WorkbenchHost host;
  LifecycleBench map{ "map", "map" };
  LifecycleBench classify{ "classify", "classify" };
  LifecycleBench busy{ "georef-i2m", "georef-i2m" };
  REQUIRE( host.registerWorkbench( &map ) );
  REQUIRE( host.registerWorkbench( &classify ) );
  REQUIRE( host.registerWorkbench( &busy ) );
  classify.m_inFlight = true;
  busy.m_inFlight = true;
  busy.m_acceptCancel = false; // refuses (e.g. work already ended)

  REQUIRE( sicnu::app::cancelInFlightBenches( &host ) == 1 );
  REQUIRE( classify.m_cancelAsked == 1 );
  REQUIRE( classify.m_cancelAccepted == 1 );
  REQUIRE( busy.m_cancelAsked == 1 );
  REQUIRE( busy.m_cancelAccepted == 0 );

  // Nothing left in flight — a second pass asks nobody.
  REQUIRE( sicnu::app::cancelInFlightBenches( &host ) == 0 );
  REQUIRE( classify.m_cancelAsked == 1 );
  REQUIRE( sicnu::app::cancelInFlightBenches( nullptr ) == 0 );
}

TEST_CASE( "requestCloseDirtyBenches aborts on first refusal",
           "[shutdown][policy][host]" )
{
  ensureApp();
  sicnu::app::WorkbenchHost host;
  LifecycleBench map{ "map", "map" };
  LifecycleBench classify{ "classify", "classify" };
  LifecycleBench georef{ "georef-i2i", "georef-i2i" };
  REQUIRE( host.registerWorkbench( &map ) );
  REQUIRE( host.registerWorkbench( &classify ) );
  REQUIRE( host.registerWorkbench( &georef ) );

  SECTION( "no dirty benches → trivially confirmed, nobody asked" )
  {
    REQUIRE( sicnu::app::requestCloseDirtyBenches( &host ) );
    REQUIRE( classify.m_closeAsked == 0 );
  }

  SECTION( "dirty bench accepting close" )
  {
    classify.m_dirty = true;
    REQUIRE( sicnu::app::requestCloseDirtyBenches( &host ) );
    REQUIRE( classify.m_closeAsked == 1 );
    REQUIRE( map.m_closeAsked == 0 ); // clean benches are never asked
  }

  SECTION( "refusal stops before touching later benches" )
  {
    classify.m_dirty = true;
    classify.m_allowClose = false;
    georef.m_dirty = true;
    REQUIRE_FALSE( sicnu::app::requestCloseDirtyBenches( &host ) );
    REQUIRE( classify.m_closeAsked == 1 );
    REQUIRE( georef.m_closeAsked == 0 );
  }

  SECTION( "null host is a confirmed no-op" )
  {
    REQUIRE( sicnu::app::requestCloseDirtyBenches( nullptr ) );
  }
}
