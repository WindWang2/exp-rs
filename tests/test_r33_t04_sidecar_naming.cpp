// test_r33_t04_sidecar_naming.cpp — R33-T04 / WP03 (#2124):
//
//   serveFromExecutionCache built sidecar names by concatenating
//   artifact+suffix (scene.shp → "scene.shp.dbf") while GC and the output
//   committer use the STEM (scene.dbf) with PAM (.aux.xml) riding the full
//   name. Result: cached serves never carried real sidecars and never
//   cleared stale destination ones. The derivation now lives in ONE
//   authority — ArtifactGC::sidecarPathForMain — shared by GC, publish and
//   cache-serve.
#include <catch2/catch_test_macros.hpp>

#include "workflow/artifact_gc.h"

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

#include <QFile>

using namespace sicnu::workflow;

TEST_CASE( "sidecar names derive from the stem with the PAM full-name exception",
           "[workflow][sidecar][r33t04]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    SECTION( "vector sidecars replace the extension" )
    {
        const QString shp = dir.filePath( QStringLiteral( "scene.shp" ) );
        CHECK( ArtifactGC::sidecarPathForMain( shp, QStringLiteral( ".shx" ) )
               == dir.filePath( QStringLiteral( "scene.shx" ) ) );
        CHECK( ArtifactGC::sidecarPathForMain( shp, QStringLiteral( ".dbf" ) )
               == dir.filePath( QStringLiteral( "scene.dbf" ) ) );
        CHECK( ArtifactGC::sidecarPathForMain( shp, QStringLiteral( ".prj" ) )
               == dir.filePath( QStringLiteral( "scene.prj" ) ) );
        CHECK( ArtifactGC::sidecarPathForMain( shp, QStringLiteral( ".cpg" ) )
               == dir.filePath( QStringLiteral( "scene.cpg" ) ) );
    }

    SECTION( "multi-dot stems keep every dot (completeBaseName)" )
    {
        // scene.2020.v2.shp → scene.2020.v2.dbf (the FULL stem, not the
        // first-segment name).
        const QString shp = dir.filePath( QStringLiteral( "scene.2020.v2.shp" ) );
        CHECK( ArtifactGC::sidecarPathForMain( shp, QStringLiteral( ".dbf" ) )
               == dir.filePath( QStringLiteral( "scene.2020.v2.dbf" ) ) );
        // .shp.xml rides the stem as a whole suffix.
        CHECK( ArtifactGC::sidecarPathForMain( shp, QStringLiteral( ".shp.xml" ) )
               == dir.filePath( QStringLiteral( "scene.2020.v2.shp.xml" ) ) );
    }

    SECTION( "raster world files and PAM" )
    {
        const QString tif = dir.filePath( QStringLiteral( "ndvi.tif" ) );
        CHECK( ArtifactGC::sidecarPathForMain( tif, QStringLiteral( ".tfw" ) )
               == dir.filePath( QStringLiteral( "ndvi.tfw" ) ) );
        // PAM is the ONE full-name sidecar: ndvi.tif.aux.xml.
        CHECK( ArtifactGC::sidecarPathForMain( tif, QStringLiteral( ".aux.xml" ) )
               == dir.filePath( QStringLiteral( "ndvi.tif.aux.xml" ) ) );
    }

    SECTION( "stem sidecars never use the artifact+suffix concatenation" )
    {
        const QString shp = dir.filePath( QStringLiteral( "scene.shp" ) );
        for ( const QString &suffix : ArtifactGC::sidecarSuffixes() )
        {
            // PAM (.aux.xml) is the one full-name sidecar by contract; the
            // #2124 defect shape is artifact+suffix for a STEM suffix.
            if ( suffix == QLatin1String( ".aux.xml" ) )
                continue;
            const QString sidecar = ArtifactGC::sidecarPathForMain( shp, suffix );
            CHECK( sidecar != shp + suffix ); // the #2124 defect shape
            CHECK( sidecar.contains( QLatin1Char( '/' ) ) );
        }
    }
}

TEST_CASE( "removeFilesWithSidecars and sidecarPathForMain agree on disk",
           "[workflow][sidecar][r33t04]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString main = QDir( dir.path() ).filePath( QStringLiteral( "lake.shp" ) );
    {
        QFile f( main );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
    }

    // Materialize sidecars exactly where the authority says they belong.
    const QString dbf = ArtifactGC::sidecarPathForMain( main, QStringLiteral( ".dbf" ) );
    const QString shx = ArtifactGC::sidecarPathForMain( main, QStringLiteral( ".shx" ) );
    for ( const QString &path : { dbf, shx } )
    {
        QFile f( path );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( QByteArrayLiteral( "x" ) );
    }

    QStringList errors;
    const QStringList removed = ArtifactGC::removeFilesWithSidecars( { main }, &errors );
    CHECK( removed.contains( dbf ) );
    CHECK( removed.contains( shx ) );
    CHECK( removed.contains( main ) );
    CHECK( errors.isEmpty() );
    CHECK( !QFileInfo::exists( dbf ) );
    CHECK( !QFileInfo::exists( shx ) );
    CHECK( !QFileInfo::exists( main ) );
}
