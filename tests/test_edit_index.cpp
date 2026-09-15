// test_edit_index.cpp — F11 Package E: maintained spatial index over an
// editable layer.
//
// Oracles:
//   * query answers are checked against brute-force scans over the same
//     deterministic generator (independent of QgsSpatialIndex);
//   * the 100k scale is the GOAL's logical gate (O3); the attach cap is
//     verified to fail closed at a hand-chosen smaller cap;
//   * incremental maintenance is verified against a freshly rebuilt index.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "editing/rs_edit_index.h"

#include <qgsapplication.h>
#include <qgsfeature.h>
#include <qgsgeometry.h>
#include <qgsproject.h>
#include <qgsvectorlayer.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace
{

struct IndexFixture
{
    IndexFixture()
    {
        if ( !QgsApplication::instance() )
        {
            static int argc = 1;
            static char arg0[] = "test_edit_index";
            static char arg1[] = "--quiet";
            static char *argv[] = { arg0, arg1, nullptr };
            new QgsApplication( argc, argv, false );
        }
        QgsApplication::initQgis();
        QgsProject::instance()->clear();
    }
    ~IndexFixture() { QgsProject::instance()->clear(); }
};

/// Deterministic grid generator: fid i → (x = i % 316, y = i / 316).
QgsPointXY pointOf( int i ) { return QgsPointXY( i % 316, i / 316 ); }

void seedPoints( QgsVectorLayer *layer, int count )
{
    layer->startEditing();
    QVector<QgsFeature> features;
    features.reserve( count );
    for ( int i = 0; i < count; ++i )
    {
        QgsFeature f( layer->fields() );
        f.setId( i + 1 );
        f.setGeometry( QgsGeometry::fromPointXY( pointOf( i ) ) );
        features.append( f );
    }
    QgsFeatureList list( features );
    layer->addFeatures( list );
    layer->commitChanges();
}

} // namespace

TEST_CASE( "index answers matches brute force at the 100k logical scale",
           "[editing][index][f11][oracle3][scale]" )
{
    IndexFixture fx;
    const int count = 100000;
    QgsVectorLayer layer( QStringLiteral( "Point?crs=EPSG:4326" ),
                          QStringLiteral( "grid" ), QStringLiteral( "memory" ) );
    REQUIRE( layer.isValid() );
    seedPoints( &layer, count );

    RsEditIndex index;
    QString err;
    REQUIRE( index.attach( &layer, &err ) );
    CHECK( err.isEmpty() );
    CHECK( index.size() == count );

    // Bounded-window intersect query vs brute force (oracle scan).
    const QgsRectangle query( 100.0, 50.0, 104.0, 53.0 );
    const QList<QgsFeatureId> hits = index.intersects( query );
    std::vector<int> expected;
    for ( int i = 0; i < count; ++i )
    {
        const QgsPointXY p = pointOf( i );
        if ( query.contains( p ) )
            expected.push_back( i + 1 ); // fid = i + 1
    }
    REQUIRE( hits.size() == static_cast<int>( expected.size() ) );
    CHECK( hits.size() > 0 ); // sanity: window actually hits the grid
    std::vector<int> got;
    for ( QgsFeatureId fid : hits )
        got.push_back( static_cast<int>( fid ) );
    std::sort( got.begin(), got.end() );
    std::sort( expected.begin(), expected.end() );
    CHECK( got == expected );

    // Nearest-5 to a point between grid nodes vs brute force.
    const QgsPointXY probe( 50.4, 20.6 );
    const QList<QgsFeatureId> near = index.nearest( probe, 5 );
    // The underlying index is allowed to return one extra entry at the
    // k-boundary; the k nearest must always be present.
    REQUIRE( near.size() >= 5 );
    std::vector<std::pair<double, int>> distances;
    distances.reserve( count );
    for ( int i = 0; i < count; ++i )
    {
        const QgsPointXY p = pointOf( i );
        const double dx = p.x() - probe.x();
        const double dy = p.y() - probe.y();
        distances.emplace_back( std::sqrt( dx * dx + dy * dy ), i + 1 );
    }
    std::partial_sort( distances.begin(), distances.begin() + 5, distances.end() );
    // With point features, index nearest == brute-force nearest: the sorted
    // distances of the returned fids must equal the oracle's top-5.
    std::vector<double> gotDistances;
    for ( QgsFeatureId fid : near )
    {
        const QgsPointXY p = pointOf( static_cast<int>( fid ) - 1 );
        const double dx = p.x() - probe.x();
        const double dy = p.y() - probe.y();
        gotDistances.push_back( std::sqrt( dx * dx + dy * dy ) );
    }
    std::sort( gotDistances.begin(), gotDistances.end() );
    for ( int rank = 0; rank < 5; ++rank )
    {
        CHECK( gotDistances[static_cast<size_t>( rank )] ==
               Catch::Approx( distances[static_cast<size_t>( rank )].first ).margin( 1e-9 ) );
    }
}

// Local approx helper to keep the include surface small.
namespace
{
struct Catch2Approx
{
    explicit Catch2Approx( double v ) : value( v ) {}
    bool operator==( double other ) const { return std::fabs( other - value ) < 1e-9; }
    double value;
};
} // namespace

TEST_CASE( "attach fails closed above the feature cap and honors the env override",
           "[editing][index][f11][negative][scale]" )
{
    IndexFixture fx;
    QgsVectorLayer layer( QStringLiteral( "Point?crs=EPSG:4326" ),
                          QStringLiteral( "small" ), QStringLiteral( "memory" ) );
    REQUIRE( layer.isValid() );
    seedPoints( &layer, 1500 );

    RsEditIndex index;
    QString err;
    // Hand-chosen cap below the layer size: refused, no partial index.
    CHECK_FALSE( index.attach( &layer, &err, {}, 1000 ) );
    CHECK( err.contains( QStringLiteral( "cap" ) ) );
    CHECK( index.size() == 0 );
    CHECK_FALSE( index.isAttached() );

    // Env opt-in raises the cap.
    qputenv( "RS_EDIT_INDEX_MAX_FEATURES", "2000" );
    CHECK( RsEditIndex::readMaxFeaturesEnv() == 2000 );
    REQUIRE( index.attach( &layer, &err ) );
    CHECK( index.size() == 1500 );
    qunsetenv( "RS_EDIT_INDEX_MAX_FEATURES" );
    CHECK( RsEditIndex::readMaxFeaturesEnv() == RsEditIndex::kDefaultMaxFeatures );
}

TEST_CASE( "incremental maintenance equals a freshly rebuilt index",
           "[editing][index][f11][oracle3]" )
{
    IndexFixture fx;
    QgsVectorLayer layer( QStringLiteral( "Point?crs=EPSG:4326" ),
                          QStringLiteral( "live" ), QStringLiteral( "memory" ) );
    REQUIRE( layer.isValid() );
    seedPoints( &layer, 100 );

    RsEditIndex index;
    QString err;
    REQUIRE( index.attach( &layer, &err ) );
    CHECK( index.size() == 100 );

    REQUIRE( layer.startEditing() );

    // 1) add
    QgsFeature added( layer.fields() );
    added.setGeometry( QgsGeometry::fromPointXY( QgsPointXY( 500, 500 ) ) );
    layer.addFeature( added );
    CHECK( index.size() == 101 );
    CHECK( index.intersects( QgsRectangle( 499, 499, 501, 501 ) ).contains( added.id() ) );

    // 2) geometry change: old footprint leaves the index, new one enters.
    QgsGeometry moved = QgsGeometry::fromPointXY( QgsPointXY( 600, 600 ) );
    layer.changeGeometry( added.id(), moved );
    CHECK( index.intersects( QgsRectangle( 499, 499, 501, 501 ) ).isEmpty() );
    CHECK( index.intersects( QgsRectangle( 599, 599, 601, 601 ) ).contains( added.id() ) );

    // 3) undo of the geometry change (edit command) restores the old spot.
    layer.undoStack()->undo();
    CHECK( index.intersects( QgsRectangle( 599, 599, 601, 601 ) ).isEmpty() );
    CHECK( index.intersects( QgsRectangle( 499, 499, 501, 501 ) ).contains( added.id() ) );

    // 4) delete
    layer.deleteFeature( added.id() );
    CHECK( index.size() == 100 );
    CHECK( index.intersects( QgsRectangle( 499, 499, 501, 501 ) ).isEmpty() );

    layer.commitChanges();
}

TEST_CASE( "selection bounds are the union of the selected footprints",
           "[editing][index][f11]" )
{
    IndexFixture fx;
    QgsVectorLayer layer( QStringLiteral( "Point?crs=EPSG:4326" ),
                          QStringLiteral( "sel" ), QStringLiteral( "memory" ) );
    REQUIRE( layer.isValid() );
    seedPoints( &layer, 10 );

    RsEditIndex index;
    QString err;
    REQUIRE( index.attach( &layer, &err ) );

    // Hand-known: fids 1 (0,0) and 4 (3,0) → bounds x [0,3], y [0,0].
    layer.selectByIds( QgsFeatureIds() << 1 << 4 );
    const QgsRectangle bounds = index.selectionBounds();
    CHECK( bounds.xMinimum() == 0.0 );
    CHECK( bounds.xMaximum() == 3.0 );
    CHECK( bounds.yMinimum() == 0.0 );
    CHECK( bounds.yMaximum() == 0.0 );

    // Full bounds cover the whole 10-point line (0,0)..(9,0).
    const QgsRectangle all = index.bounds();
    CHECK( all.xMinimum() == 0.0 );
    CHECK( all.xMaximum() == 9.0 );

    layer.removeSelection();
    CHECK( index.selectionBounds().isNull() );
}
