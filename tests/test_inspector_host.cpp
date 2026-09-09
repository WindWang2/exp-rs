// Workbench 5.0 — InspectorHost section lifecycle contract (Milestone F)
#include <catch2/catch_test_macros.hpp>

#include "app/workbench/inspector_host.h"

#include <QStackedWidget>
#include <QTabWidget>
#include <QApplication>
#include <QLabel>

namespace
{

int fake_argc = 1;
char fake_argv0[] = "test_inspector_host";
char *fake_argv[] = { fake_argv0, nullptr };

QApplication *ensureApp()
{
  static QApplication *app = nullptr;
  if ( !app && !QCoreApplication::instance() )
    app = new QApplication( fake_argc, fake_argv );
  return app;
}

using sicnu::app::InspectorSection;
using sicnu::app::SelectionContextSnapshot;

/// Section that only supports raster selections; counts populate calls.
class FakeRasterSection : public InspectorSection
{
  public:
    explicit FakeRasterSection( QString id, int order = 100 )
        : m_id( std::move( id ) ), m_order( order )
    {
      m_body = new QLabel( this );
    }
    QString sectionId() const override { return m_id; }
    QString title() const override { return m_id.toUpper(); }
    int order() const override { return m_order; }
    bool supports( const SelectionContextSnapshot &s ) const override { return s.hasRaster; }
    void populate( const SelectionContextSnapshot &s ) override
    {
      ++populates;
      lastPopulateLayerCount = s.layerCount;
    }
    void cancelPending() override { ++cancels; }

    int populates = 0;
    int cancels = 0;
    int lastPopulateLayerCount = -1;

  private:
    QString m_id;
    int m_order = 100;
    QLabel *m_body = nullptr;
};

SelectionContextSnapshot snapRaster( bool raster = true )
{
  SelectionContextSnapshot s;
  s.hasRaster = raster;
  s.layerCount = raster ? 1 : 0;
  return s;
}

} // namespace

TEST_CASE( "InspectorHost: placeholder with nothing selected", "[inspector_host]" )
{
  ensureApp();
  sicnu::app::InspectorHost host;
  FakeRasterSection general( QStringLiteral( "general" ) );
  host.registerSection( &general );
  host.setSnapshot( snapRaster( false ) );

  auto *placeholder = host.findChild<QLabel *>( QStringLiteral( "rsInspectorPlaceholder" ) );
  REQUIRE( placeholder );
  auto *stack = host.findChild< QStackedWidget * >( QStringLiteral( "rsInspectorStack" ) );
  REQUIRE( stack );
  REQUIRE( stack->currentWidget() == placeholder );
  REQUIRE( general.populates == 0 ); // unsupported → never populated
}

TEST_CASE( "InspectorHost: supported selection populates the section",
           "[inspector_host][behavior]" )
{
  ensureApp();
  sicnu::app::InspectorHost host;
  FakeRasterSection general( QStringLiteral( "general" ) );
  host.registerSection( &general );

  host.setSnapshot( snapRaster() );
  REQUIRE( general.populates == 1 );

  // Same-snapshot refresh still repopulates (cheap sections stay live).
  host.setSnapshot( snapRaster() );
  REQUIRE( general.populates == 2 );
}

TEST_CASE( "InspectorHost: unsupported→cancel; empty→placeholder without stale tabs",
           "[inspector_host][contract]" )
{
  ensureApp();
  sicnu::app::InspectorHost host;
  FakeRasterSection general( QStringLiteral( "general" ) );
  host.registerSection( &general );

  host.setSnapshot( snapRaster() );
  REQUIRE( general.populates == 1 );

  host.setSnapshot( snapRaster( false ) ); // selection moved away from raster
  REQUIRE( general.cancels == 1 );

  auto *placeholder = host.findChild<QLabel *>( QStringLiteral( "rsInspectorPlaceholder" ) );
  REQUIRE( placeholder );

  // Issue #780 / #777: Re-selection after unsupported snapshot must not crash or UAF
  host.setSnapshot( snapRaster( true ) );
  REQUIRE( general.populates == 2 );
}

TEST_CASE( "InspectorHost: multi-section ordering by order()/id", "[inspector_host]" )
{
  ensureApp();
  sicnu::app::InspectorHost host;
  FakeRasterSection metadata( QStringLiteral( "metadata" ), 10 );
  FakeRasterSection general( QStringLiteral( "general" ), 0 );
  host.registerSection( &metadata );
  host.registerSection( &general );

  host.setSnapshot( snapRaster() );
  const QList<InspectorSection *> sections = host.sections();
  REQUIRE( sections.size() == 2 );
  REQUIRE( sections.front()->sectionId() == QLatin1String( "general" ) );
  REQUIRE( sections.back()->sectionId() == QLatin1String( "metadata" ) );
}

TEST_CASE( "InspectorHost: switching tabs lazily populates secondary sections (#812)",
           "[inspector_host][behavior]" )
{
  ensureApp();
  sicnu::app::InspectorHost host;
  FakeRasterSection sec1( QStringLiteral( "sec1" ), 1 );
  FakeRasterSection sec2( QStringLiteral( "sec2" ), 2 );
  host.registerSection( &sec1 );
  host.registerSection( &sec2 );

  host.setSnapshot( snapRaster() );
  REQUIRE( sec1.populates == 1 );
  REQUIRE( sec2.populates == 0 ); // secondary tab not yet shown, lazy

  auto *tabs = host.findChild<QTabWidget *>( QStringLiteral( "rsInspectorTabs" ) );
  REQUIRE( tabs );
  REQUIRE( tabs->count() == 2 );

  // Switch to secondary tab
  tabs->setCurrentIndex( 1 );
  REQUIRE( sec2.populates == 1 );
}

