// tests/test_r15_C_publish_recovery.cpp — Track 15 WP-C13: the crash suffixes
// the publish park ladders can leave behind must ALL be recoverable.
//
// The ladder parks sidecars/companions FIRST and the main file LAST, so a run
// killed mid-ladder leaves states the adoption branches (gated on the MAIN
// file's presence) never matched — and removeBackupFamily() then DELETED the
// only copies of the previous product's companions (detection) or its
// provenance sidecar (raster). Both suffixes are hand-built here (the
// established orphan-test pattern) and the guard is constructed directly.
//
// Oracles (.planning/r15-c/ORACLES.md C13):
//   F1: after DetectionPublishGuard over the companion-parked suffix, every
//       companion is back at its final name (byte-identical) and no
//       *.det-prev~ litter survives — the product is openable again.
//   F2: after ProductPublishGuard over the sidecar-parked suffix, the
//       sidecar is back next to the product — never a verified product
//       downgraded to MissingSidecar.
#include <catch2/catch_test_macros.hpp>

#include "operators/runtime/model_publish.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <string>

namespace {

using sicnu::operators::runtime::DetectionPublishGuard;
using sicnu::operators::runtime::ProductPublishGuard;

void writeFile( const QString &path, const QByteArray &bytes )
{
    QFile f( path );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( bytes );
    f.close();
}

QByteArray readFile( const QString &path )
{
    QFile f( path );
    REQUIRE( f.open( QIODevice::ReadOnly ) );
    return f.readAll();
}

bool exists( const QString &path )
{
    return QFileInfo::exists( path );
}

} // namespace

TEST_CASE( "R15-C C13: the companion-parked detection suffix is adopted back, not deleted",
           "[models][publish][r15-c]" )
{
    QTemporaryDir dir;
    const QString base = dir.filePath( QStringLiteral( "vec" ) );
    const QString shp = base + QStringLiteral( ".shp" );
    const QString prov = shp + QStringLiteral( ".prov.json" );

    // The previous product, complete, at the final names.
    writeFile( shp, QByteArrayLiteral( "shp-bytes" ) );
    writeFile( prov, QByteArrayLiteral( "prov-bytes" ) );
    // The crash suffix the park ladder leaves between the companion renames
    // and the prov/main renames: companions parked, main still at the final
    // name. Only the parked copies exist.
    writeFile( base + QStringLiteral( ".dbf.det-prev~" ), QByteArrayLiteral( "dbf-bytes" ) );
    writeFile( base + QStringLiteral( ".shx.det-prev~" ), QByteArrayLiteral( "shx-bytes" ) );
    writeFile( base + QStringLiteral( ".prj.det-prev~" ), QByteArrayLiteral( "prj-bytes" ) );
    REQUIRE_FALSE( exists( base + QStringLiteral( ".dbf" ) ) );

    {
        // Constructing the guard over the suffix must fold the parked
        // companions back onto the final names (suffix-complete adoption)
        // before the park ladder runs — and the destructor then restores the
        // whole product exactly as it was.
        DetectionPublishGuard guard( shp, QStringLiteral( ".det-prev~" ) );
        ( void )guard;
    }

    CHECK( exists( base + QStringLiteral( ".dbf" ) ) );
    CHECK( readFile( base + QStringLiteral( ".dbf" ) ) == QByteArrayLiteral( "dbf-bytes" ) );
    CHECK( exists( base + QStringLiteral( ".shx" ) ) );
    CHECK( readFile( base + QStringLiteral( ".shx" ) ) == QByteArrayLiteral( "shx-bytes" ) );
    CHECK( exists( base + QStringLiteral( ".prj" ) ) );
    CHECK( readFile( base + QStringLiteral( ".prj" ) ) == QByteArrayLiteral( "prj-bytes" ) );
    CHECK_FALSE( exists( base + QStringLiteral( ".dbf.det-prev~" ) ) );
    CHECK_FALSE( exists( base + QStringLiteral( ".shx.det-prev~" ) ) );
    CHECK( exists( shp ) );
    CHECK( readFile( shp ) == QByteArrayLiteral( "shp-bytes" ) );
    CHECK( exists( prov ) );
    CHECK( readFile( prov ) == QByteArrayLiteral( "prov-bytes" ) );
}

TEST_CASE( "R15-C C13: the sidecar-parked raster suffix is adopted back, not deleted",
           "[models][publish][r15-c]" )
{
    QTemporaryDir dir;
    const QString out = dir.filePath( QStringLiteral( "out.tif" ) );

    // The previous product at the final path with its sidecar's ONLY copy
    // parked (the ladder parks the sidecar first, the main last).
    writeFile( out, QByteArrayLiteral( "old-product" ) );
    writeFile( out + QStringLiteral( ".prev~.prov.json" ), QByteArrayLiteral( "old-prov" ) );
    REQUIRE_FALSE( exists( out + QStringLiteral( ".prov.json" ) ) );

    {
        ProductPublishGuard guard( out, QStringLiteral( ".prev~" ), nullptr );
        ( void )guard;
    }

    CHECK( exists( out + QStringLiteral( ".prov.json" ) ) );
    CHECK( readFile( out + QStringLiteral( ".prov.json" ) ) == QByteArrayLiteral( "old-prov" ) );
    CHECK( exists( out ) );
    CHECK( readFile( out ) == QByteArrayLiteral( "old-product" ) );
    CHECK_FALSE( exists( out + QStringLiteral( ".prev~" ) ) );
    CHECK_FALSE( exists( out + QStringLiteral( ".prev~.prov.json" ) ) );
}

TEST_CASE( "R15-C C13: the main-parked detection suffix still adopts the whole family",
           "[models][publish][r15-c]" )
{
    // Window A regression (the historical adoption branch): main + companions
    // + prov all parked, final absent. The next run must restore every
    // member — companions and prov first, the main last.
    QTemporaryDir dir;
    const QString base = dir.filePath( QStringLiteral( "vec" ) );
    const QString shp = base + QStringLiteral( ".shp" );

    writeFile( shp + QStringLiteral( ".det-prev~" ), QByteArrayLiteral( "shp-bytes" ) );
    writeFile( base + QStringLiteral( ".dbf.det-prev~" ), QByteArrayLiteral( "dbf-bytes" ) );
    writeFile( base + QStringLiteral( ".shx.det-prev~" ), QByteArrayLiteral( "shx-bytes" ) );
    writeFile( shp + QStringLiteral( ".det-prev~.prov.json" ), QByteArrayLiteral( "prov-bytes" ) );

    {
        DetectionPublishGuard guard( shp, QStringLiteral( ".det-prev~" ) );
        ( void )guard;
    }

    CHECK( exists( shp ) );
    CHECK( readFile( shp ) == QByteArrayLiteral( "shp-bytes" ) );
    CHECK( exists( base + QStringLiteral( ".dbf" ) ) );
    CHECK( exists( base + QStringLiteral( ".shx" ) ) );
    CHECK( exists( shp + QStringLiteral( ".prov.json" ) ) );
    CHECK_FALSE( exists( shp + QStringLiteral( ".det-prev~" ) ) );
    CHECK_FALSE( exists( base + QStringLiteral( ".dbf.det-prev~" ) ) );
}
