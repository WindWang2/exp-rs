/***************************************************************************
 * tests/test_adversarial_m4.cpp
 * Adversarial Empirical Stress Suite for Milestone 4
 * Issues: #777, #778, #779, #780, #792, #793, #794, #795, #796, #812, #813
 ***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "app/workbench/inspector_host.h"
#include "app/workbench/selection_context.h"
#include "app/workbench/command_registry.h"
#include "app/workbench/adapters.h"
#include "app/workbench/workbench_host.h"
#include "app/display/qgis_display_manager.h"

#include <QApplication>
#include <QLabel>
#include <QTabWidget>
#include <QStackedWidget>
#include <QRegularExpression>
#include <qgsproject.h>
#include <qgsmapcanvas.h>
#include <qgsvectorlayer.h>
#include <qgsrasterlayer.h>
#include <qgslayertree.h>
#include <qgsmaplayerstore.h>

namespace
{

int fake_argc = 1;
char fake_argv0[] = "test_adversarial_m4";
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

class AdvTestSection : public InspectorSection
{
public:
    explicit AdvTestSection( QString id, int order = 100, bool onlyRaster = true )
        : m_id( std::move( id ) ), m_order( order ), m_onlyRaster( onlyRaster )
    {
        m_label = new QLabel( QStringLiteral( "Content for %1" ).arg( m_id ), this );
    }

    QString sectionId() const override { return m_id; }
    QString title() const override { return m_id.toUpper(); }
    int order() const override { return m_order; }

    bool supports( const SelectionContextSnapshot &s ) const override
    {
        return m_onlyRaster ? s.hasRaster : s.hasVector;
    }

    void populate( const SelectionContextSnapshot &s ) override
    {
        ++populates;
        lastLayerCount = s.layerCount;
    }

    void cancelPending() override
    {
        ++cancels;
    }

    int populates = 0;
    int cancels = 0;
    int lastLayerCount = -1;

private:
    QString m_id;
    int m_order;
    bool m_onlyRaster;
    QLabel *m_label = nullptr;
};

SelectionContextSnapshot makeSnap( bool raster, bool vector, int count )
{
    SelectionContextSnapshot s;
    s.hasRaster = raster;
    s.hasVector = vector;
    s.layerCount = count;
    return s;
}

} // namespace

// ============================================================================
// Case 1: #777, #780, #812 - InspectorHost Section Reparenting & Lazy Tabs Stress
// ============================================================================
TEST_CASE( "Adversarial M4 - #777 & #780 & #812: InspectorHost UAF prevention & multi-tab stress",
           "[adv][m4][issue-777][issue-780][issue-812]" )
{
    ensureApp();
    sicnu::app::InspectorHost host;

    // Create 4 sections with distinct order and criteria
    AdvTestSection sec1( QStringLiteral( "s1_raster" ), 10, true );
    AdvTestSection sec2( QStringLiteral( "s2_raster" ), 20, true );
    AdvTestSection sec3( QStringLiteral( "s3_vector" ), 30, false );
    AdvTestSection sec4( QStringLiteral( "s4_vector" ), 40, false );

    host.registerSection( &sec1 );
    host.registerSection( &sec2 );
    host.registerSection( &sec3 );
    host.registerSection( &sec4 );

    // 50 rapid cycles of alternating supported, unsupported, empty
    for ( int cycle = 0; cycle < 50; ++cycle )
    {
        // Raster selection: sec1 and sec2 supported, sec1 active
        host.setSnapshot( makeSnap( true, false, 1 ) );
        CHECK( sec1.populates == cycle + 1 );
        CHECK( sec1.parent() != nullptr );

        // Empty selection: all tabs removed, placeholder shown, sections reparented back to host
        host.setSnapshot( makeSnap( false, false, 0 ) );
        CHECK( sec1.parent() == &host );
        CHECK( sec2.parent() == &host );

        // Vector selection: sec3 and sec4 supported, sec3 active
        host.setSnapshot( makeSnap( false, true, 2 ) );
        CHECK( sec3.populates == cycle + 1 );
        CHECK( sec3.parent() != nullptr );

        // Re-empty
        host.setSnapshot( makeSnap( false, false, 0 ) );
        CHECK( sec3.parent() == &host );
        CHECK( sec4.parent() == &host );
    }
    CHECK( sec1.cancels >= 50 );
    CHECK( sec3.cancels >= 50 );

    // Now test tab-switching lazy population (#812)
    host.setSnapshot( makeSnap( true, false, 3 ) );
    auto *tabs = host.findChild<QTabWidget *>( QStringLiteral( "rsInspectorTabs" ) );
    REQUIRE( tabs != nullptr );
    REQUIRE( tabs->count() == 2 );

    const int initialSec2Populates = sec2.populates;
    // sec2 is the second tab, not active yet
    tabs->setCurrentIndex( 1 );
    CHECK( sec2.populates == initialSec2Populates + 1 );
    CHECK( sec2.lastLayerCount == 3 );
}

// ============================================================================
// Case 2: #778 - SelectionContext Instant Cache Invalidation & Pointer Safety
// ============================================================================
TEST_CASE( "Adversarial M4 - #778: SelectionContext rapid layer purge cache eviction",
           "[adv][m4][issue-778]" )
{
    ensureApp();
    QgsProject *project = QgsProject::instance();
    project->clear();

    QgsMapCanvas canvas;
    sicnu::app::SelectionContext ctx;
    ctx.attachCanvas( &canvas );

    for ( int i = 0; i < 20; ++i )
    {
        auto *layer = new QgsVectorLayer( QStringLiteral( "Point?crs=EPSG:4326" ),
                                          QStringLiteral( "layer_%1" ).arg( i ),
                                          QStringLiteral( "memory" ) );
        project->addMapLayer( layer );
        canvas.setCurrentLayer( layer );

        const auto snap = ctx.snapshot();
        CHECK( snap.activeLayer == layer );

        // Instantly remove layer from project before debounce (150ms) fires
        project->removeMapLayer( layer->id() );

        // Immediate snapshot must NOT return dangling layer
        const auto purgedSnap = ctx.snapshot();
        CHECK( purgedSnap.activeLayer == nullptr );
    }

    project->clear();
}

// ============================================================================
// Case 3: #792 & #794 - CommandRegistry Multi-Shortcut Mass Registration
// ============================================================================
TEST_CASE( "Adversarial M4 - #792 & #794: CommandRegistry 30 distinct canonical shortcuts",
           "[adv][m4][issue-792][issue-794]" )
{
    ensureApp();
    sicnu::app::CommandRegistry registry;

    for ( int i = 1; i <= 30; ++i )
    {
        sicnu::app::CommandDefinition d;
        d.id = QStringLiteral( "adv.cmd.%1" ).arg( i );
        d.title = QStringLiteral( "Command %1" ).arg( i );
        d.description = QStringLiteral( "Desc %1" ).arg( i );
        d.shortcut = QKeySequence( QStringLiteral( "Ctrl+F%1" ).arg( i ) );
        d.handler = [] {};
        REQUIRE( registry.registerCommand( d ) );

        // Each command can install its shortcut once without aborting
        QAction *act = registry.action( d.id, true );
        REQUIRE( act != nullptr );
        CHECK( act->shortcut() == QKeySequence( QStringLiteral( "Ctrl+F%1" ).arg( i ) ) );
    }

    CHECK( registry.count() == 30 );
}

// ============================================================================
// Case 4: #779, #793, #796 - Canvas Rendering Race & Multi-View Tree Isolation
// ============================================================================
TEST_CASE( "Adversarial M4 - #779, #793 & #796: Multi-view isolation and render race stop",
           "[adv][m4][issue-779][issue-793][issue-796]" )
{
    ensureApp();
    sicnu::display::QgisDisplayManager dm( nullptr );

    // Create 3 independent display views
    std::vector<std::unique_ptr<QgsMapCanvas>> canvases;
    std::vector<std::unique_ptr<QgsLayerTree>> trees;
    std::vector<std::unique_ptr<QgsMapLayerStore>> stores;
    std::vector<sicnu::display::DisplayViewId> viewIds;

    for ( int i = 0; i < 3; ++i )
    {
        canvases.push_back( std::make_unique<QgsMapCanvas>() );
        trees.push_back( std::make_unique<QgsLayerTree>() );
        stores.push_back( std::make_unique<QgsMapLayerStore>() );

        sicnu::display::DisplayViewSpec spec{ canvases.back().get(), trees.back().get(), stores.back().get() };
        auto res = dm.createView( spec );
        REQUIRE( res.has_value() );
        viewIds.push_back( *res );
    }

    // Verify all 3 views return their own distinct layer tree (Issue #793)
    for ( int i = 0; i < 3; ++i )
    {
        CHECK( dm.layerTree( viewIds[i] ) == trees[i].get() );
        for ( int j = i + 1; j < 3; ++j )
        {
            CHECK( dm.layerTree( viewIds[i] ) != dm.layerTree( viewIds[j] ) );
        }
    }

    // Render race stress: add layer, trigger refresh, stopRendering and remove (Issues #779, #796)
    for ( int i = 0; i < 3; ++i )
    {
        auto *layer = new QgsVectorLayer( QStringLiteral( "Point?crs=epsg:4326" ),
                                          QStringLiteral( "layer_adv_%1" ).arg( i ),
                                          QStringLiteral( "memory" ) );
        stores[i]->addMapLayer( layer );
        trees[i]->addLayer( layer );
        canvases[i]->setLayers( { layer } );
        canvases[i]->refresh();

        canvases[i]->stopRendering();
        canvases[i]->setLayers( {} );
        stores[i]->removeMapLayer( layer->id() );
        CHECK_FALSE( canvases[i]->isDrawing() );
    }
}

// ============================================================================
// Case 5: #813 - ExternalWindowWorkbench Lifecycle and Dirty Hooks
// ============================================================================
TEST_CASE( "Adversarial M4 - #813: ExternalWindowWorkbench dirty and close semantics",
           "[adv][m4][issue-813]" )
{
    ensureApp();
    sicnu::app::WorkbenchHost host;

    bool dirty = false;
    bool allowClose = false;
    bool windowClosed = false;
    QWidget dummyWindow;

    auto *wb = new sicnu::app::ExternalWindowWorkbench(
        QStringLiteral( "adv_wb" ), QStringLiteral( "高级工作区" ), QStringLiteral( "icon" ),
        [] {},
        [&dummyWindow]() -> QWidget * { return &dummyWindow; },
        [&dirty]() -> bool { return dirty; },
        [&allowClose, &windowClosed]() -> bool {
            if ( !allowClose )
                return false;
            windowClosed = true;
            return true;
        },
        &host );

    host.registerWorkbench( wb );
    host.activate( QStringLiteral( "adv_wb" ) );
    CHECK( wb->isActive() );

    // Dirty state follows hook
    CHECK_FALSE( wb->isDirty() );
    dirty = true;
    CHECK( wb->isDirty() );

    // Refused close does NOT deactivate workbench
    CHECK_FALSE( wb->requestClose() );
    CHECK( wb->isActive() );
    CHECK_FALSE( windowClosed );

    // Permitted close closes and deactivates
    allowClose = true;
    CHECK( wb->requestClose() );
    CHECK_FALSE( wb->isActive() );
    CHECK( windowClosed );
}
