// test_edit_session.cpp — F11 Package A: RsEditSession authority contract.
//
// Oracles are independent of the session: feature counts are read from the
// layer directly (getFeatures), commit failure is forced through
// QgsVectorLayer::setAllowCommit (a core-layer switch, not session code),
// and removal semantics go through the real QgsProject signals.
#include <catch2/catch_test_macros.hpp>

#include "editing/rs_edit_command_guard.h"
#include "editing/rs_edit_session.h"

#include <QApplication>
#include <QTemporaryDir>

#include <qgsapplication.h>
#include <qgsfeature.h>
#include <qgsgeometry.h>
#include <qgsproject.h>
#include <qgsvectordataprovider.h>
#include <qgsvectorlayer.h>

namespace
{

struct EditFixture
{
    EditFixture()
    {
        if ( !QgsApplication::instance() )
        {
            static int argc = 1;
            static char arg0[] = "test_edit_session";
            static char arg1[] = "--quiet";
            static char *argv[] = { arg0, arg1, nullptr };
            new QgsApplication( argc, argv, false );
        }
        QgsApplication::initQgis();
        project = QgsProject::instance();
        project->clear();
    }
    ~EditFixture() { project->clear(); }

    QgsProject *project = nullptr;
};

QgsVectorLayer *makePointLayer( const QString &name )
{
    auto *layer = new QgsVectorLayer( QStringLiteral( "Point?crs=EPSG:4326&field=label:string" ),
                                      name, QStringLiteral( "memory" ) );
    REQUIRE( layer->isValid() );
    return layer;
}

QgsFeature makePointFeature( QgsVectorLayer *layer, double x, double y, const QString &label )
{
    QgsFeature f( layer->fields() );
    f.setGeometry( QgsGeometry::fromPointXY( QgsPointXY( x, y ) ) );
    f.setAttribute( QStringLiteral( "label" ), label );
    return f;
}

qlonglong liveFeatureCount( QgsVectorLayer *layer )
{
    qlonglong n = 0;
    QgsFeatureIterator it = layer->getFeatures();
    QgsFeature f;
    while ( it.nextFeature( f ) )
        ++n;
    return n;
}

} // namespace

TEST_CASE( "attach starts editing and reports layer state", "[edit_session][f11]" )
{
    EditFixture fx;
    RsEditSession session;
    QgsVectorLayer layer( QStringLiteral( "Point?crs=EPSG:4326&field=label:string" ),
                          QStringLiteral( "pts" ), QStringLiteral( "memory" ) );
    REQUIRE( layer.isValid() );

    const QString err = session.attachLayer( &layer );
    REQUIRE( err.isEmpty() );
    CHECK( session.isAttached( layer.id() ) );
    CHECK( layer.isEditable() );

    const RsLayerEditState s = session.state( layer.id() );
    CHECK( s.layerId == layer.id() );
    CHECK( s.editing );
    CHECK_FALSE( s.modified );
    CHECK( s.featureCount == 0 );
    CHECK( s.undoDepth == 0 );
    CHECK( s.redoDepth == 0 );
    CHECK_FALSE( session.isDirty() );
}

TEST_CASE( "attach fails closed for a layer that cannot edit", "[edit_session][f11][negative]" )
{
    EditFixture fx;
    RsEditSession session;
    QgsVectorLayer bad( QStringLiteral( "definitely not a provider uri" ),
                        QStringLiteral( "broken" ), QStringLiteral( "memory" ) );
    REQUIRE_FALSE( bad.isValid() );
    const QString err = session.attachLayer( &bad );
    REQUIRE_FALSE( err.isEmpty() );
    CHECK( err.contains( QStringLiteral( "cannot start editing" ) ) );
    CHECK_FALSE( session.isAttached( bad.id() ) );
}

TEST_CASE( "attaching an already-editing layer adopts it", "[edit_session][f11]" )
{
    EditFixture fx;
    RsEditSession session;
    QgsVectorLayer layer( QStringLiteral( "Point?crs=EPSG:4326" ),
                          QStringLiteral( "pts" ), QStringLiteral( "memory" ) );
    REQUIRE( layer.startEditing() );
    const QString err = session.attachLayer( &layer );
    REQUIRE( err.isEmpty() );
    CHECK( layer.isEditable() );
    CHECK( session.state( layer.id() ).editing );
}

TEST_CASE( "attach refuses duplicate tracking", "[edit_session][f11][negative]" )
{
    EditFixture fx;
    RsEditSession session;
    QgsVectorLayer layer( QStringLiteral( "Point?crs=EPSG:4326" ),
                          QStringLiteral( "pts" ), QStringLiteral( "memory" ) );
    REQUIRE( session.attachLayer( &layer ).isEmpty() );
    const QString err = session.attachLayer( &layer );
    CHECK_FALSE( err.isEmpty() );
}

TEST_CASE( "one guard is one undoable command; undo/redo delegate to the layer stack",
           "[edit_session][f11][oracle1]" )
{
    EditFixture fx;
    RsEditSession session;
    QgsVectorLayer *layer = makePointLayer( QStringLiteral( "pts" ) );
    REQUIRE( session.attachLayer( layer ).isEmpty() );
    const QString id = layer->id();

    {
        RsEditCommandGuard guard( &session, layer, QStringLiteral( "add point" ) );
        REQUIRE( guard.isValid() );
        QgsFeature featureToAdd = makePointFeature( layer, 1.0, 2.0, QStringLiteral( "a" ) );
        layer->addFeature( featureToAdd );
    }

    RsLayerEditState s = session.state( id );
    CHECK( s.modified );
    CHECK( s.undoDepth == 1 );
    CHECK( s.featureCount == 1 );
    CHECK( liveFeatureCount( layer ) == 1 );
    CHECK( session.isDirty() );

    // Whole-stroke undo through the session.
    REQUIRE( session.undo( id ) );
    CHECK( liveFeatureCount( layer ) == 0 );
    CHECK_FALSE( session.undo( id ) ); // nothing left to undo
    s = session.state( id );
    CHECK( s.featureCount == 0 );
    CHECK( s.undoDepth == 0 );
    CHECK( s.redoDepth == 1 );

    REQUIRE( session.redo( id ) );
    CHECK( liveFeatureCount( layer ) == 1 );
    CHECK_FALSE( session.redo( id ) ); // nothing left to redo

    // Locked layers refuse undo/redo through the session.
    REQUIRE( session.setLocked( id, true ) );
    CHECK( session.isLocked( id ) );
    const bool beforeUndoOk = session.undo( id ); // there is something to undo
    CHECK_FALSE( beforeUndoOk );
    session.setLocked( id, false );
    REQUIRE( session.undo( id ) );
    CHECK( liveFeatureCount( layer ) == 0 );

    delete layer;
}

TEST_CASE( "locked layer refuses edit command guards", "[edit_session][f11][negative]" )
{
    EditFixture fx;
    RsEditSession session;
    QgsVectorLayer *layer = makePointLayer( QStringLiteral( "pts" ) );
    REQUIRE( session.attachLayer( layer ).isEmpty() );
    REQUIRE( session.setLocked( layer->id(), true ) );

    RsEditCommandGuard guard( &session, layer, QStringLiteral( "must not begin" ) );
    CHECK_FALSE( guard.isValid() );
    // The guard must not have opened a command: layer stack stays empty.
    CHECK( session.state( layer->id() ).undoDepth == 0 );

    // An unattached layer also refuses guards.
    QgsVectorLayer stray( QStringLiteral( "Point?crs=EPSG:4326" ),
                          QStringLiteral( "stray" ), QStringLiteral( "memory" ) );
    RsEditCommandGuard guard2( &session, &stray, QStringLiteral( "must not begin" ) );
    CHECK_FALSE( guard2.isValid() );

    delete layer;
}

TEST_CASE( "commit success clears modified state and keeps persisted features",
           "[edit_session][f11]" )
{
    EditFixture fx;
    RsEditSession session;
    QgsVectorLayer *layer = makePointLayer( QStringLiteral( "pts" ) );
    REQUIRE( session.attachLayer( layer ).isEmpty() );
    const QString id = layer->id();
    {
        RsEditCommandGuard guard( &session, layer, QStringLiteral( "add" ) );
        QgsFeature featureToAdd = makePointFeature( layer, 5.0, 6.0, QStringLiteral( "a" ) );
        layer->addFeature( featureToAdd );
    }
    REQUIRE( session.isDirty() );

    QString err;
    REQUIRE( session.commit( id, &err ) );
    CHECK( err.isEmpty() );

    // Memory provider: committed features stay live in the layer.
    CHECK( liveFeatureCount( layer ) == 1 );
    const RsLayerEditState s = session.state( id );
    CHECK_FALSE( s.modified );
    CHECK_FALSE( s.editing ); // commitChanges() default stops editing
    CHECK_FALSE( session.isDirty() );

    delete layer;
}

TEST_CASE( "commit failure is reported, never swallowed", "[edit_session][f11][negative][oracle]" )
{
    EditFixture fx;
    RsEditSession session;
    QgsVectorLayer *layer = makePointLayer( QStringLiteral( "pts" ) );
    REQUIRE( session.attachLayer( layer ).isEmpty() );
    const QString id = layer->id();
    {
        RsEditCommandGuard guard( &session, layer, QStringLiteral( "add" ) );
        QgsFeature featureToAdd = makePointFeature( layer, 1.0, 1.0, QStringLiteral( "x" ) );
        layer->addFeature( featureToAdd );
    }

    // Force a genuine commitChanges()==false through the core switch.
    layer->setAllowCommit( false );
    QString err;
    bool commitFinishedOk = true;
    QObject::connect( &session, &RsEditSession::commitFinished,
                      [&commitFinishedOk]( const QString &, bool ok, const QString & )
    {
        commitFinishedOk = ok;
    } );
    CHECK_FALSE( session.commit( id, &err ) );
    CHECK_FALSE( commitFinishedOk );
    CHECK_FALSE( err.isEmpty() ); // fallback message when the layer names no reason
    // The layer keeps its buffer: data was not silently lost.
    CHECK( layer->isEditable() );
    CHECK( session.isDirty() );

    layer->setAllowCommit( true );
    CHECK( session.commit( id ) );
    CHECK( liveFeatureCount( layer ) == 1 );

    delete layer;
}

TEST_CASE( "commitAll commits every dirty layer and reports failures per layer",
           "[edit_session][f11]" )
{
    EditFixture fx;
    RsEditSession session;
    QgsVectorLayer *a = makePointLayer( QStringLiteral( "a" ) );
    QgsVectorLayer *b = makePointLayer( QStringLiteral( "b" ) );
    REQUIRE( session.attachLayer( a ).isEmpty() );
    REQUIRE( session.attachLayer( b ).isEmpty() );
    {
        RsEditCommandGuard ga( &session, a, QStringLiteral( "add" ) );
        QgsFeature featureToAdd = makePointFeature( a, 0.0, 0.0, QStringLiteral( "a1" ) );
        a->addFeature( featureToAdd );
        RsEditCommandGuard gb( &session, b, QStringLiteral( "add" ) );
        QgsFeature featureToAddB = makePointFeature( b, 1.0, 1.0, QStringLiteral( "b1" ) );
        b->addFeature( featureToAddB );
    }
    b->setAllowCommit( false );

    QStringList errors;
    const int committed = session.commitAll( &errors );
    CHECK( committed == 1 );
    REQUIRE( errors.size() == 1 );
    CHECK( errors.first().startsWith( b->id() ) );
    CHECK( liveFeatureCount( a ) == 1 );
    CHECK( liveFeatureCount( b ) == 1 ); // still buffered, reported not lost

    b->setAllowCommit( true );
    CHECK( session.commit( b->id() ) );

    delete a;
    delete b;
}

TEST_CASE( "rollback discards uncommitted changes and stops editing", "[edit_session][f11]" )
{
    EditFixture fx;
    RsEditSession session;
    QgsVectorLayer *layer = makePointLayer( QStringLiteral( "pts" ) );
    REQUIRE( session.attachLayer( layer ).isEmpty() );
    const QString id = layer->id();
    {
        RsEditCommandGuard guard( &session, layer, QStringLiteral( "add" ) );
        QgsFeature featureToAdd = makePointFeature( layer, 3.0, 4.0, QStringLiteral( "r" ) );
        layer->addFeature( featureToAdd );
    }
    REQUIRE( session.isDirty() );

    QString err;
    REQUIRE( session.rollback( id, &err ) );
    CHECK( err.isEmpty() );
    CHECK( liveFeatureCount( layer ) == 0 );
    CHECK_FALSE( layer->isEditable() );
    CHECK_FALSE( session.isDirty() );
    CHECK( session.state( id ).featureCount == 0 );

    delete layer;
}

TEST_CASE( "detach rolls back dirty layers by default", "[edit_session][f11]" )
{
    EditFixture fx;
    RsEditSession session;
    QgsVectorLayer *layer = makePointLayer( QStringLiteral( "pts" ) );
    REQUIRE( session.attachLayer( layer ).isEmpty() );
    const QString id = layer->id();
    {
        RsEditCommandGuard guard( &session, layer, QStringLiteral( "add" ) );
        QgsFeature featureToAdd = makePointFeature( layer, 1.0, 1.0, QStringLiteral( "d" ) );
        layer->addFeature( featureToAdd );
    }

    CHECK( session.detachLayer( id ).isEmpty() );
    CHECK_FALSE( session.isAttached( id ) );
    CHECK( liveFeatureCount( layer ) == 0 );
    CHECK_FALSE( layer->isEditable() );

    delete layer;
}

TEST_CASE( "project layer removal drops session tracking without dangling state",
           "[edit_session][f11][oracle1][lifecycle]" )
{
    EditFixture fx;
    RsEditSession session;
    QgsVectorLayer *layer = makePointLayer( QStringLiteral( "doomed" ) );
    fx.project->addMapLayer( layer, /*addToLegend=*/false );
    REQUIRE( session.attachLayer( layer ).isEmpty() );
    const QString id = layer->id();
    {
        RsEditCommandGuard guard( &session, layer, QStringLiteral( "add" ) );
        QgsFeature featureToAdd = makePointFeature( layer, 2.0, 2.0, QStringLiteral( "x" ) );
        layer->addFeature( featureToAdd );
    }
    REQUIRE( session.isAttached( id ) );

    // Removing the layer from the project deletes it; the session must drop
    // its entry inside layerWillBeRemoved — before the object dies.
    fx.project->removeMapLayer( layer );
    layer = nullptr;
    CHECK_FALSE( session.isAttached( id ) );
    CHECK( session.attachedLayerIds().isEmpty() );
    CHECK_FALSE( session.isDirty() );

    // The session stays fully usable afterwards.
    QgsVectorLayer *fresh = makePointLayer( QStringLiteral( "fresh" ) );
    REQUIRE( session.attachLayer( fresh ).isEmpty() );
    CHECK( session.attachedLayerIds().size() == 1 );
    delete fresh;
}

TEST_CASE( "project clear with dirty session leaves the session consistent",
           "[edit_session][f11][oracle1][lifecycle]" )
{
    EditFixture fx;
    RsEditSession session;
    QgsVectorLayer *layer = makePointLayer( QStringLiteral( "pts" ) );
    fx.project->addMapLayer( layer, false );
    REQUIRE( session.attachLayer( layer ).isEmpty() );
    {
        RsEditCommandGuard guard( &session, layer, QStringLiteral( "add" ) );
        QgsFeature featureToAdd = makePointFeature( layer, 1.0, 2.0, QStringLiteral( "c" ) );
        layer->addFeature( featureToAdd );
    }
    REQUIRE( session.isDirty() );

    fx.project->clear();
    CHECK( session.attachedLayerIds().isEmpty() );
    CHECK_FALSE( session.isDirty() );
    // And a new attach works on the cleared project.
    QgsVectorLayer *fresh = makePointLayer( QStringLiteral( "pts2" ) );
    REQUIRE( session.attachLayer( fresh ).isEmpty() );
    CHECK( session.attachedLayerIds().size() == 1 );
    delete fresh;
}

TEST_CASE( "selection and aggregate dirty facts track live layers", "[edit_session][f11]" )
{
    EditFixture fx;
    RsEditSession session;
    QgsVectorLayer *layer = makePointLayer( QStringLiteral( "pts" ) );
    REQUIRE( session.attachLayer( layer ).isEmpty() );
    const QString id = layer->id();
    {
        RsEditCommandGuard guard( &session, layer, QStringLiteral( "add" ) );
        QgsFeature featureToAdd1 = makePointFeature( layer, 0.0, 0.0, QStringLiteral( "s1" ) );
        layer->addFeature( featureToAdd1 );
        QgsFeature featureToAdd2 = makePointFeature( layer, 1.0, 0.0, QStringLiteral( "s2" ) );
        layer->addFeature( featureToAdd2 );
    }
    layer->selectByIds( QgsFeatureIds() << layer->getFeature( 1 ).id() );
    CHECK( session.state( id ).selectedCount == 1 );
    layer->removeSelection();
    CHECK( session.state( id ).selectedCount == 0 );

    delete layer;
}

TEST_CASE( "guard cancel discards the whole command", "[edit_session][f11][oracle1]" )
{
    EditFixture fx;
    RsEditSession session;
    QgsVectorLayer *layer = makePointLayer( QStringLiteral( "pts" ) );
    REQUIRE( session.attachLayer( layer ).isEmpty() );
    const QString id = layer->id();
    {
        RsEditCommandGuard guard( &session, layer, QStringLiteral( "doomed stroke" ) );
        REQUIRE( guard.isValid() );
        QgsFeature featureToAdd = makePointFeature( layer, 9.0, 9.0, QStringLiteral( "gone" ) );
        layer->addFeature( featureToAdd );
        guard.cancel();
        CHECK_FALSE( guard.isValid() );
    }
    CHECK( liveFeatureCount( layer ) == 0 );
    CHECK( session.state( id ).undoDepth == 0 );
    CHECK_FALSE( session.isDirty() );

    delete layer;
}
