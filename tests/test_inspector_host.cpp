// Workbench 5.0 — InspectorHost section lifecycle contract (Milestone F)
#include <catch2/catch_test_macros.hpp>

#include "app/workbench/inspector_host.h"

#include <QStackedWidget>
#include <QApplication>
#include <QLabel>
#include <QTabWidget>

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

// ── Workbench 6.0 Milestone A: lifecycle hazards (#777 / #780 / #812) ──────

TEST_CASE( "InspectorHost: sections survive the unsupported→supported re-selection cycle",
           "[inspector_host][lifecycle][ux6]" )
{
  ensureApp();
  sicnu::app::InspectorHost host;
  FakeRasterSection general( QStringLiteral( "general" ) );
  sicnu::app::InspectorSection *section = &general;
  host.registerSection( section );

  host.setSnapshot( snapRaster() );
  REQUIRE( general.populates == 1 );

  // Selection moves away from raster: the tab widget is torn down and the
  // placeholder shown. The registered section object MUST survive this
  // rebuild (#777: delete oldTabs used to destroy it inside the QTabWidget).
  host.setSnapshot( snapRaster( false ) );
  REQUIRE( general.cancels == 1 );
  REQUIRE( host.sections().size() == 1 );
  REQUIRE( host.sections().first() == section );

  // Re-selecting a supported object must not use-after-free and must repopulate
  // the SAME section instance (#780 masks this whole cycle if it is untested).
  host.setSnapshot( snapRaster() );
  REQUIRE( host.sections().first() == section );
  REQUIRE( general.populates == 2 );

  // And a second full cycle keeps the pointer stable.
  host.setSnapshot( snapRaster( false ) );
  host.setSnapshot( snapRaster() );
  REQUIRE( host.sections().first() == section );
  REQUIRE( general.populates == 3 );
}

TEST_CASE( "InspectorHost: switching tabs populates the newly shown section",
           "[inspector_host][behavior][ux6]" )
{
  ensureApp();
  sicnu::app::InspectorHost host;
  FakeRasterSection general( QStringLiteral( "general" ), 0 );
  FakeRasterSection metadata( QStringLiteral( "metadata" ), 10 );
  host.registerSection( &general );
  host.registerSection( &metadata );

  host.setSnapshot( snapRaster() );
  REQUIRE( general.populates == 1 );
  REQUIRE( metadata.populates == 0 ); // lazy: only the shown tab populates

  auto *tabs = host.findChild<QTabWidget *>( QStringLiteral( "rsInspectorTabs" ) );
  REQUIRE( tabs );
  REQUIRE( tabs->count() == 2 );

  // #812: currentChanged was never connected — the secondary tab stayed
  // permanently blank. A user tab switch must populate the shown section.
  tabs->setCurrentIndex( 1 );
  REQUIRE( metadata.populates == 1 );
  tabs->setCurrentIndex( 0 );
  REQUIRE( general.populates == 2 );
}
