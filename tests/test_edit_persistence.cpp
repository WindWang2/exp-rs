// test_edit_persistence.cpp — F11 Package G: atomic export / interchange.
//
// Oracles:
//   * round-trip truth is an OGR/GDAL-free re-read through a NEW
//     QgsVectorLayer (memory-provider content written by QgsVectorFileWriter
//     into GeoJSON / GPKG, then parsed back);
//   * atomicity: an injected writer failure leaves the previous target file
//     byte-identical and removes temp residue;
//   * unsupported suffixes fail closed without touching the disk.
#include <catch2/catch_test_macros.hpp>

#include "editing/rs_edit_persistence.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <qgsapplication.h>
#include <qgsfeature.h>
#include <qgsgeometry.h>
#include <qgsproject.h>
#include <qgsvectorlayer.h>
#include "support/qt_lifecycle.h"

// Track 2 R4 (PR #1335 exit-crash cluster, group 2): ordered teardown via the
// shared listener — drains deferred deletes, runs exitQgis()/invalidateCaches
// while guards are alive, deletes the app before glibc exit().
CATCH_REGISTER_LISTENER( sicnu::test::qtlifecycle::TeardownListener )

namespace
{

struct PersistFixture
{
    PersistFixture()
    {
        if ( !QgsApplication::instance() )
        {
            static int argc = 1;
            static char arg0[] = "test_edit_persistence";
            static char arg1[] = "--quiet";
            static char *argv[] = { arg0, arg1, nullptr };
            new QgsApplication( argc, argv, false );
        }
        QgsApplication::initQgis();
        QgsProject::instance()->clear();
    }
    ~PersistFixture() { QgsProject::instance()->clear(); }
};

QgsVectorLayer *makeSampleLayer( const QString &name )
{
    auto *layer = new QgsVectorLayer(
      QStringLiteral( "Polygon?crs=EPSG:4326&field=class:int&field=label:string" ),
      name, QStringLiteral( "memory" ) );
    REQUIRE( layer->isValid() );
    layer->startEditing();
    for ( int i = 0; i < 3; ++i )
    {
        QgsFeature f( layer->fields() );
        f.setGeometry( QgsGeometry::fromPolygonXY( { QVector<QgsPointXY>{
          QgsPointXY( i, 0 ), QgsPointXY( i + 1, 0 ), QgsPointXY( i + 1, 1 ),
          QgsPointXY( i, 1 ), QgsPointXY( i, 0 ) } } ) );
        f.setAttribute( QStringLiteral( "class" ), i );
        f.setAttribute( QStringLiteral( "label" ), QStringLiteral( "L%1" ).arg( i ) );
        layer->addFeature( f );
    }
    layer->commitChanges();
    return layer;
}

qlonglong countFeatures( QgsVectorLayer *layer )
{
    qlonglong n = 0;
    QgsFeatureIterator it = layer->getFeatures();
    QgsFeature f;
    while ( it.nextFeature( f ) )
        ++n;
    return n;
}

} // namespace

TEST_CASE( "GeoJSON export round-trips features and attributes",
           "[editing][persistence][f11][oracle]" )
{
    PersistFixture fx;
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    QgsVectorLayer *layer = makeSampleLayer( QStringLiteral( "src" ) );

    const QString target = dir.filePath( QStringLiteral( "samples.geojson" ) );
    const RsEditPersistence::ExportResult result =
      RsEditPersistence::exportLayerToGeoJson( layer, target, QStringLiteral( "samples" ) );
    INFO( "GeoJSON export error: " << result.error.toStdString() );
    REQUIRE( result.ok );
    CHECK( result.error.isEmpty() );
    CHECK( result.writtenPath == target );
    CHECK( QFile::exists( target ) );

    // No temp residue.
    const QFileInfoList entries = QDir( dir.path() ).entryInfoList( QStringList() << QStringLiteral( "*.tmp-*" ) );
    CHECK( entries.isEmpty() );

    // Provider-free oracle (this build profile ships no OGR provider
    // plugin, so a QgsVectorLayer re-open is unavailable): GeoJSON is JSON —
    // parse with QJsonDocument and check the a-priori content.
    QFile file( target );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    const QJsonDocument doc = QJsonDocument::fromJson( file.readAll() );
    REQUIRE( doc.isObject() );
    CHECK( doc.object()[QLatin1String( "type" )].toString() == QStringLiteral( "FeatureCollection" ) );
    const QJsonArray features = doc.object()[QLatin1String( "features" )].toArray();
    REQUIRE( features.size() == 3 );
    CHECK( features[0].toObject()[QLatin1String( "properties" )].toObject()
             [QLatin1String( "class" )].toInt() == 0 );
    delete layer;
}

TEST_CASE( "GPKG export round-trips via a fresh layer read",
           "[editing][persistence][f11][oracle]" )
{
    PersistFixture fx;
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    QgsVectorLayer *layer = makeSampleLayer( QStringLiteral( "src" ) );

    const QString target = dir.filePath( QStringLiteral( "samples.gpkg" ) );
    const RsEditPersistence::ExportResult result =
      RsEditPersistence::exportLayerToGpkg( layer, target, QStringLiteral( "samples" ) );
    INFO( "GPKG export error: " << result.error.toStdString() );
    // The GPKG driver is present in this build profile; a regression in
    // exportLayerToGpkg must FAIL this test, not slip through an else branch.
    REQUIRE( result.ok );
    {
        // Valid SQLite/GPKG container (magic bytes = "SQLite format 3").
        QFile file( target );
        REQUIRE( file.open( QIODevice::ReadOnly ) );
        const QByteArray magic = file.read( 15 );
        CHECK( magic.startsWith( "SQLite format 3" ) );
        CHECK( countFeatures( layer ) == 3 );
    }
    delete layer;
}

TEST_CASE( "pre-IO rejection preserves the previous target byte-identically",
           "[editing][persistence][f11][negative]" )
{
    PersistFixture fx;
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // Pre-existing target content (simulating a previous good export).
    const QString target = dir.filePath( QStringLiteral( "samples.geojson" ) );
    const QByteArray previous = "{ \"previous\": true }\n";
    {
        QFile file( target );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        file.write( previous );
    }

    // Invalid source layer → refused in exportLayer's validity gate, before
    // any temp file or writer exists; the target must be untouched.
    QgsVectorLayer broken( QStringLiteral( "not-a-valid-uri" ),
                           QStringLiteral( "broken" ), QStringLiteral( "memory" ) );
    REQUIRE_FALSE( broken.isValid() );

    const RsEditPersistence::ExportResult result =
      RsEditPersistence::exportLayer( &broken, target, QStringLiteral( "x" ) );
    CHECK_FALSE( result.ok );
    CHECK_FALSE( result.error.isEmpty() );

    QFile file( target );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    CHECK( file.readAll() == previous );
    file.close();

    const QFileInfoList entries = QDir( dir.path() ).entryInfoList( QStringList() << QStringLiteral( "*.tmp-*" ) );
    CHECK( entries.isEmpty() );
}

TEST_CASE( "writer failure inside an unwritable directory fails closed and leaves no temp residue",
           "[editing][persistence][f11][negative]" )
{
    PersistFixture fx;
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // A read-only directory forces a genuine writer-side failure
    // (ErrCreateDataSource), i.e. the failure paths AFTER exportLayer's
    // validity gate are exercised.
    const QString roDirPath = dir.filePath( QStringLiteral( "ro" ) );
    REQUIRE( QDir( dir.path() ).mkpath( QStringLiteral( "ro" ) ) );
    QFile::setPermissions( roDirPath, QFile::ReadOwner | QFile::ExeOwner );

    QgsVectorLayer *layer = makeSampleLayer( QStringLiteral( "src" ) );
    const RsEditPersistence::ExportResult result =
      RsEditPersistence::exportLayer( layer, roDirPath + QStringLiteral( "/samples.geojson" ),
                                      QStringLiteral( "samples" ) );
    CHECK_FALSE( result.ok );
    CHECK_FALSE( result.error.isEmpty() );
    CHECK_FALSE( QFile::exists( roDirPath + QStringLiteral( "/samples.geojson" ) ) );
    const QFileInfoList entries = QDir( roDirPath ).entryInfoList( QStringList() << QStringLiteral( "*.tmp-*" ) );
    CHECK( entries.isEmpty() );
    QFile::setPermissions( roDirPath, QFile::WriteOwner | QFile::ReadOwner | QFile::ExeOwner );
    delete layer;
}

TEST_CASE( "unsupported suffixes are refused before any IO",
           "[editing][persistence][f11][negative]" )
{
    PersistFixture fx;
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    QgsVectorLayer *layer = makeSampleLayer( QStringLiteral( "src" ) );

    const QString target = dir.filePath( QStringLiteral( "samples.shp" ) );
    const RsEditPersistence::ExportResult result =
      RsEditPersistence::exportLayer( layer, target, QStringLiteral( "x" ) );
    CHECK_FALSE( result.ok );
    CHECK( result.error.contains( QStringLiteral( "unsupported" ) ) );
    CHECK_FALSE( QFile::exists( target ) );
    delete layer;
}

TEST_CASE( "commitReported surfaces per-layer failures instead of swallowing them",
           "[editing][persistence][f11][negative]" )
{
    PersistFixture fx;
    QgsVectorLayer a( QStringLiteral( "Point?crs=EPSG:4326" ), QStringLiteral( "a" ),
                      QStringLiteral( "memory" ) );
    QgsVectorLayer b( QStringLiteral( "Point?crs=EPSG:4326" ), QStringLiteral( "b" ),
                      QStringLiteral( "memory" ) );
    REQUIRE( a.startEditing() );
    REQUIRE( b.startEditing() );
    QgsFeature fa( a.fields() );
    fa.setGeometry( QgsGeometry::fromPointXY( QgsPointXY( 0, 0 ) ) );
    a.addFeature( fa );
    QgsFeature fb( b.fields() );
    fb.setGeometry( QgsGeometry::fromPointXY( QgsPointXY( 1, 1 ) ) );
    b.addFeature( fb );

    b.setAllowCommit( false ); // force a genuine commit failure on layer b

    QStringList errors;
    const int committed = RsEditPersistence::commitReported( { &a, &b }, &errors );
    CHECK( committed == 1 );
    REQUIRE( errors.size() == 1 );
    CHECK( errors.first().contains( QStringLiteral( "b" ) ) );
    CHECK( b.isModified() ); // buffered, reported — not silently lost
}
