// test_edit_annotation.cpp — F11 Package B: app-level annotation surface.
//
// Oracles: item ids/counts read back from the QgsAnnotationLayer itself;
// project-reopen path exercises the reuse of an existing annotation layer;
// headless (no cad dock) tool creation must refuse.
#include <catch2/catch_test_macros.hpp>

#include "editing/rs_annotation_controller.h"

#include <QApplication>

#include <qgsannotationitem.h>
#include <qgsannotationlayer.h>
#include <qgsannotationpointtextitem.h>
#include <qgsapplication.h>
#include <qgsproject.h>

namespace
{

QApplication *ensureApp()
{
    static int argc = 1;
    static char arg0[] = "test_edit_annotation";
    static char arg1[] = "--quiet";
    static char *argv[] = { arg0, arg1, nullptr };
    return qApp ? nullptr : new QApplication( argc, argv );
}

struct AnnFixture
{
    AnnFixture()
    {
        if ( !QgsApplication::instance() )
        {
            static int argc = 1;
            static char arg0[] = "test_edit_annotation_app";
            static char arg1[] = "--quiet";
            static char *argv[] = { arg0, arg1, nullptr };
            new QgsApplication( argc, argv, false );
        }
        QgsApplication::initQgis();
        project = QgsProject::instance();
        project->clear();
    }
    ~AnnFixture() { project->clear(); }
    QgsProject *project = nullptr;
};

} // namespace

TEST_CASE( "controller creates the project annotation layer once",
           "[editing][annotation][f11]" )
{
    AnnFixture fx;
    RsAnnotationController controller( fx.project );

    QgsAnnotationLayer *layer = controller.annotationLayer();
    REQUIRE( layer );
    CHECK( layer->crs() == fx.project->crs() );
    CHECK( fx.project->mapLayer( layer->id() ) == layer );

    // Idempotent: same instance back.
    CHECK( controller.annotationLayer() == layer );
    CHECK( controller.itemCount() == 0 );
}

TEST_CASE( "addPointText and addMarker produce findable items with ids",
           "[editing][annotation][f11]" )
{
    AnnFixture fx;
    RsAnnotationController controller( fx.project );

    const QString textId = controller.addPointText( QStringLiteral( "核对点 A" ), QgsPointXY( 1.0, 2.0 ) );
    REQUIRE_FALSE( textId.isEmpty() );
    const QString markerId = controller.addMarker( QgsPointXY( 3.0, 4.0 ) );
    REQUIRE_FALSE( markerId.isEmpty() );
    CHECK( textId != markerId );
    CHECK( controller.itemCount() == 2 );

    QgsAnnotationLayer *layer = controller.annotationLayer();
    const QMap<QString, QgsAnnotationItem *> items = layer->items();
    // Annotation items are not QObjects in this core — use dynamic_cast.
    auto *textItem = dynamic_cast<QgsAnnotationPointTextItem *>( items.value( textId ) );
    REQUIRE( textItem );
    CHECK( textItem->text() == QStringLiteral( "核对点 A" ) );

    CHECK( controller.removeItem( textId ) );
    CHECK( controller.itemCount() == 1 );
    CHECK_FALSE( controller.removeItem( QStringLiteral( "no-such-id" ) ) );
}

TEST_CASE( "annotation layer survives project layer removal and is recreated cleanly",
           "[editing][annotation][f11][lifecycle]" )
{
    AnnFixture fx;
    RsAnnotationController controller( fx.project );
    QgsAnnotationLayer *layer = controller.annotationLayer();
    REQUIRE( layer );
    const QString oldId = layer->id();
    REQUIRE( controller.addMarker( QgsPointXY( 0, 0 ) ).isEmpty() == false );

    fx.project->removeMapLayer( layer );
    // QPointer cleared by the deletion — the controller reports no layer.
    CHECK_FALSE( controller.hasAnnotationLayer() );
    CHECK( controller.itemCount() == 0 );
    CHECK( fx.project->mapLayer( oldId ) == nullptr );

    // Next call creates a fresh layer in the project under a new id.
    QgsAnnotationLayer *fresh = controller.annotationLayer();
    REQUIRE( fresh );
    CHECK( fresh->id() != oldId );
    CHECK( fx.project->mapLayer( fresh->id() ) == fresh );
    CHECK( controller.itemCount() == 0 );
}

TEST_CASE( "existing annotation layer in a reopened project is reused, not duplicated",
           "[editing][annotation][f11]" )
{
    AnnFixture fx;
    // Simulate a project that already carries an annotation layer.
    auto *existing = new QgsAnnotationLayer(
      QStringLiteral( "Restored" ), QgsAnnotationLayer::LayerOptions( fx.project->transformContext() ) );
    fx.project->addMapLayer( existing );

    RsAnnotationController controller( fx.project );
    CHECK( controller.annotationLayer() == existing );
    controller.addMarker( QgsPointXY( 5, 5 ) );

    // Exactly one annotation layer in the project.
    int count = 0;
    const QMap<QString, QgsMapLayer *> layers = fx.project->mapLayers( true );
    for ( QgsMapLayer *l : layers )
        count += qobject_cast<QgsAnnotationLayer *>( l ) ? 1 : 0;
    CHECK( count == 1 );
    CHECK( controller.itemCount() == 1 );
}

TEST_CASE( "interactive tools refuse to be created headless (negative)",
           "[editing][annotation][f11][negative]" )
{
    AnnFixture fx;
    RsAnnotationController controller( fx.project );
    CHECK( controller.createPointTextTool( nullptr, nullptr ) == nullptr );
    CHECK( controller.modifyTool( nullptr, nullptr ) == nullptr );
    CHECK( controller.selectTool( nullptr, nullptr ) == nullptr );
}
