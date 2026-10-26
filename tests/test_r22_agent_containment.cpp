// tests/test_r22_agent_containment.cpp
//
// R22-04 (#1704): GDAL/OGR driver-prefixed connection strings
// (CSV:/etc/passwd, GPKG:/outside/x.gpkg:tbl, PG:host=…, WMS:http://…) used
// to fall through classifyReference() as LocalPath, get joined onto the
// workspace root, look "inside", and pass the MCP sandbox — while GDAL then
// opened the REAL target named after the prefix (workspace-external file,
// database or URL). These tests pin the fail-closed classification and the
// workspace containment of the embedded path, without linking GDAL: the
// policy authority is the header under test, and the GDAL-side escape was
// already reproduced live in the issue thread (R17 verify).
//
// Light binary on purpose: Catch2 + Qt Core only (the containment policy is
// header-only — same reason the CLI pipeline runner can reuse it).

#include "agent/tool_catalog/workspace_containment.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>

namespace
{
struct SandboxEnvGuard
{
    QByteArray workspace = qgetenv( "SICNU_MCP_WORKSPACE" );
    bool hadWorkspace = qEnvironmentVariableIsSet( "SICNU_MCP_WORKSPACE" );
    QByteArray remote = qgetenv( "SICNU_MCP_ALLOW_REMOTE" );
    bool hadRemote = qEnvironmentVariableIsSet( "SICNU_MCP_ALLOW_REMOTE" );
    ~SandboxEnvGuard()
    {
        if ( hadWorkspace )
            qputenv( "SICNU_MCP_WORKSPACE", workspace );
        else
            qunsetenv( "SICNU_MCP_WORKSPACE" );
        if ( hadRemote )
            qputenv( "SICNU_MCP_ALLOW_REMOTE", remote );
        else
            qunsetenv( "SICNU_MCP_ALLOW_REMOTE" );
    }
};

QString writeFile( const QString &path, const QByteArray &bytes )
{
    QDir().mkpath( QFileInfo( path ).absolutePath() );
    QFile file( path );
    if ( !file.open( QIODevice::WriteOnly ) )
        return QString();
    file.write( bytes );
    return path;
}
} // namespace

using namespace sicnu::agent::tool_catalog;
namespace containment = sicnu::agent::tool_catalog::containment;

TEST_CASE( "GDAL driver-prefixed connection strings cannot escape the workspace (#1704)",
           "[agent][mcp][sandbox][security][r22]" )
{
    const SandboxEnvGuard guard;
    qunsetenv( "SICNU_MCP_ALLOW_REMOTE" );
    qputenv( "SICNU_MCP_WORKSPACE", QDir::currentPath().toUtf8() );

    QTemporaryDir workspace;
    QTemporaryDir outside;
    REQUIRE( workspace.isValid() );
    REQUIRE( outside.isValid() );
    const QString wsRoot = workspace.path();
    qputenv( "SICNU_MCP_WORKSPACE", wsRoot.toUtf8() );

    // A real workspace-external CSV: the exact payload the R17 verify used
    // to prove GDALOpenEx("CSV:"+file) reads outside the sandbox.
    const QString secretCsv = writeFile( outside.filePath( QStringLiteral( "secret.csv" ) ),
                                         "k,v\n1,pwned\n" );
    const QString secretGpkg = outside.filePath( QStringLiteral( "secret.gpkg" ) );

    SECTION( "file-backed driver prefixes reading outside are rejected" )
    {
        QString detail;
        CHECK( containment::pathOutsideWorkspace( QStringLiteral( "CSV:/etc/passwd" ), wsRoot, &detail ) );
        CHECK( containment::pathOutsideWorkspace( QStringLiteral( "CSV:" ) + secretCsv, wsRoot, &detail ) );
        CHECK( containment::pathOutsideWorkspace( QStringLiteral( "csv:" ) + secretCsv, wsRoot, &detail ) );
        CHECK( containment::pathOutsideWorkspace(
            QStringLiteral( "GPKG:" ) + secretGpkg + QStringLiteral( ":layer" ), wsRoot, &detail ) );
        CHECK( containment::pathOutsideWorkspace(
            QStringLiteral( "NETCDF:\"" ) + outside.filePath( QStringLiteral( "x.nc" ) )
                + QStringLiteral( "\":var" ),
            wsRoot, &detail ) );
        CHECK( containment::pathOutsideWorkspace( QStringLiteral( "SQLite:" ) + secretGpkg, wsRoot,
                                                  &detail ) );
        // Relative payload with ".." climbs out of the CWD anchor.
        CHECK( containment::pathOutsideWorkspace( QStringLiteral( "CSV:../escape.csv" ), wsRoot,
                                                  &detail ) );
        CHECK( detail.contains( QStringLiteral( "GDAL connection string" ) ) );
    }

    SECTION( "network driver prefixes follow the remote default-deny" )
    {
        QString detail;
        CHECK( containment::pathOutsideWorkspace(
            QStringLiteral( "WMS:http://169.254.169.254/latest/meta-data/" ), wsRoot, &detail ) );
        CHECK( containment::pathOutsideWorkspace( QStringLiteral( "PG:host=attacker dbname=x" ),
                                                  wsRoot, &detail ) );
        CHECK( containment::pathOutsideWorkspace( QStringLiteral( "pg:host=attacker" ), wsRoot,
                                                  &detail ) );
        CHECK( containment::pathOutsideWorkspace( QStringLiteral( "WFS:http://example.com/f" ), wsRoot,
                                                  &detail ) );
        CHECK( containment::pathOutsideWorkspace( QStringLiteral( "MSSQL:server=srv db=x" ), wsRoot,
                                                  &detail ) );
        // R2-F1: the connection-string SHORT names, not display names —
        // OCI is the Oracle driver ("oracle" never matched anything).
        CHECK( containment::pathOutsideWorkspace( QStringLiteral( "OCI:usr/pwd@db" ), wsRoot,
                                                  &detail ) );
        CHECK( containment::pathOutsideWorkspace( QStringLiteral( "EEDA:..." ), wsRoot, &detail ) );
        CHECK( containment::pathOutsideWorkspace( QStringLiteral( "AmigoCloud:token" ), wsRoot,
                                                  &detail ) );
        CHECK( containment::pathOutsideWorkspace( QStringLiteral( "GeoRaster:usr/pwd@db" ), wsRoot,
                                                  &detail ) );
        CHECK( detail.contains( QStringLiteral( "SICNU_MCP_ALLOW_REMOTE" ) ) );
        // With the opt-in flag the SAME connection follows the remote policy
        // (allowed class, not a workspace path) — parity with http(s)://.
        qputenv( "SICNU_MCP_ALLOW_REMOTE", "1" );
        CHECK_FALSE( containment::pathOutsideWorkspace(
            QStringLiteral( "WMS:http://example.com/x" ), wsRoot, &detail ) );
        qunsetenv( "SICNU_MCP_ALLOW_REMOTE" );
    }

    SECTION( "unknown driver prefixes with dangerous payloads are denied" )
    {
        QString detail;
        CHECK( containment::pathOutsideWorkspace( QStringLiteral( "BOGUSDRV:/etc/passwd" ), wsRoot,
                                                  &detail ) );
        CHECK( containment::pathOutsideWorkspace( QStringLiteral( "BOGUS:\"" ) + secretCsv
                                                      + QStringLiteral( "\"" ),
                                                  wsRoot, &detail ) );
        CHECK( containment::pathOutsideWorkspace( QStringLiteral( "WEIRDDRV:host=attacker" ), wsRoot,
                                                  &detail ) );
        CHECK( containment::pathOutsideWorkspace( QStringLiteral( "WEIRDDRV:server=x port=5432" ),
                                                  wsRoot, &detail ) );
        CHECK( containment::pathOutsideWorkspace( QStringLiteral( "WEIRDDRV:../up.csv" ), wsRoot,
                                                  &detail ) );
        CHECK( containment::pathOutsideWorkspace( QStringLiteral( "WEIRDDRV:<xml>config</xml>" ),
                                                  wsRoot, &detail ) );
        CHECK( containment::pathOutsideWorkspace( QStringLiteral( "WEIRDDRV:~/x" ), wsRoot, &detail ) );
        // R1-F1: non-http scheme:// payloads never degrade to relative names.
        CHECK( containment::pathOutsideWorkspace( QStringLiteral( "Zarr:s3://bucket/arr" ),
                                                  wsRoot, &detail ) );
        CHECK( containment::pathOutsideWorkspace( QStringLiteral( "CSV:s3://bucket/data.csv" ),
                                                  wsRoot, &detail ) );
        CHECK( containment::pathOutsideWorkspace( QStringLiteral( "GeoJSON:ftp://h/x.json" ),
                                                  wsRoot, &detail ) );
        CHECK( containment::pathOutsideWorkspace( QStringLiteral( "TileDB:s3://bucket/array" ),
                                                  wsRoot, &detail ) );
        // R1-F2: nested driver prefixes are classified recursively — the
        // outer CSV: cannot vouch for an absolute GPKG: target.
        CHECK( containment::pathOutsideWorkspace(
            QStringLiteral( "CSV:GPKG:/etc/x.gpkg" ), wsRoot, &detail ) );
        CHECK( containment::pathOutsideWorkspace(
            QStringLiteral( "CSV:CSV:/etc/passwd" ), wsRoot, &detail ) );
        // R2-F2: pathological nesting is a typed refusal, never a stack
        // overflow ("KML:"x100000 used to SIGSEGV at a tenth of the frame
        // cap). 50 levels >> the 8-level cap.
        {
            QString deep;
            for ( int i = 0; i < 50; ++i )
                deep += QStringLiteral( "KML:" );
            deep += QStringLiteral( "x" );
            CHECK( containment::pathOutsideWorkspace( deep, wsRoot, &detail ) );
            CHECK( detail.contains( QStringLiteral( "nests more than" ) ) );
        }
        // TileDB by its real short name: relative local payload stays a
        // contained workspace reference; s3 payload is remote (denied).
        CHECK( containment::pathOutsideWorkspace( QStringLiteral( "TileDB:s3://b/a" ), wsRoot,
                                                  &detail ) );
        CHECK_FALSE( containment::pathOutsideWorkspace( QStringLiteral( "TileDB:arr.tdb" ), wsRoot,
                                                          &detail ) );
        // A nested /vsicurl inside an unknown driver is still remote input.
        CHECK( containment::pathOutsideWorkspace( QStringLiteral( "WEIRDDRV:/vsicurl/http://x/y.json" ),
                                                  wsRoot, &detail ) );
        CHECK( detail.contains( QStringLiteral( "Unknown GDAL driver prefix" ) ) );
    }

    SECTION( "in-workspace driver connection strings keep working (#560)" )
    {
        const QString insideCsv = writeFile( workspace.filePath( QStringLiteral( "inside.csv" ) ),
                                             "a,b\n1,2\n" );
        QString detail;
        // Absolute embedded path inside the workspace.
        CHECK_FALSE( containment::pathOutsideWorkspace( QStringLiteral( "CSV:" ) + insideCsv, wsRoot,
                                                         &detail ) );
        // Workspace-relative embedded path.
        CHECK_FALSE( containment::pathOutsideWorkspace( QStringLiteral( "CSV:inside.csv" ), wsRoot,
                                                         &detail ) );
        CHECK_FALSE( containment::pathOutsideWorkspace(
            QStringLiteral( "GPKG:" ) + workspace.filePath( QStringLiteral( "ok.gpkg" ) )
                + QStringLiteral( ":layer" ),
            wsRoot, &detail ) );
        CHECK_FALSE( containment::pathOutsideWorkspace( QStringLiteral( "NETCDF:\"x.nc\":var" ), wsRoot,
                                                         &detail ) );
        // Windows drive letter is NOT a driver prefix (classification only —
        // containment of Q:/x on POSIX is the ordinary LocalPath join).
        CHECK( containment::classifyReference( QStringLiteral( "Q:/data/x.tif" ) ).kind
                == containment::ReferenceKind::LocalPath );
    }

    SECTION( "benign colon-bearing relative names are not regressions" )
    {
        // POSIX file names may contain ':'; an unknown multi-letter prefix
        // with a benign RELATIVE payload stays a plain workspace-relative
        // path (old behavior), and "./" always disambiguates.
        writeFile( workspace.filePath( QStringLiteral( "data:a.tif" ) ), "x" );
        QString detail;
        CHECK_FALSE( containment::pathOutsideWorkspace( QStringLiteral( "data:a.tif" ), wsRoot,
                                                         &detail ) );
        CHECK_FALSE( containment::pathOutsideWorkspace( QStringLiteral( "./data:a.tif" ), wsRoot,
                                                         &detail ) );
        CHECK( containment::classifyReference( QStringLiteral( "data:a.tif" ) ).kind
                == containment::ReferenceKind::LocalPath );
        // Known file-backed driver with an extractable path that does not
        // exist yet inside the workspace stays allowed (outputs).
        CHECK_FALSE( containment::pathOutsideWorkspace( QStringLiteral( "CSV:out/result.csv" ), wsRoot,
                                                         &detail ) );
        // R1-F2: nested in-workspace driver reference stays allowed.
        CHECK_FALSE( containment::pathOutsideWorkspace( QStringLiteral( "CSV:GPKG:ok.gpkg" ), wsRoot,
                                                         &detail ) );
    }

    SECTION( "no-workspace mode still applies the scheme policy" )
    {
        // Empty root = "no filesystem sandbox": only scheme/driver policy.
        // A network driver still follows the remote default-deny, and a
        // file-backed driver with an ABSOLUTE embedded path behaves exactly
        // like the bare absolute path it names (also allowed in this mode).
        QString detail;
        CHECK( containment::pathOutsideWorkspace( QStringLiteral( "WMS:http://x/y" ), QString(),
                                                  &detail ) );
        CHECK( containment::pathOutsideWorkspace( QStringLiteral( "PG:host=attacker" ), QString(),
                                                  &detail ) );
        CHECK_FALSE( containment::pathOutsideWorkspace(
            QStringLiteral( "CSV:/etc/passwd" ), QString(), &detail ) );
        CHECK_FALSE( containment::pathOutsideWorkspace( QStringLiteral( "/etc/passwd" ), QString(),
                                                         &detail ) ); // bare-path parity
        CHECK_FALSE( containment::pathOutsideWorkspace( QStringLiteral( "data:a.tif" ), QString(),
                                                         &detail ) );
    }
}

TEST_CASE( "GDAL driver prefix classification details (#1704)", "[agent][mcp][sandbox][r22]" )
{
    const SandboxEnvGuard guard;
    qunsetenv( "SICNU_MCP_ALLOW_REMOTE" );

    using containment::ReferenceKind;
    // Single-letter drive is not a driver token.
    CHECK( containment::classifyReference( QStringLiteral( "C:/x" ) ).kind == ReferenceKind::LocalPath );
    CHECK( containment::classifyReference( QStringLiteral( "C:" ) ).kind == ReferenceKind::LocalPath );
    // file:// is still mapped by its own branch, never by the driver logic.
    CHECK( containment::classifyReference( QStringLiteral( "file:///etc/passwd" ) ).kind
            == ReferenceKind::FileUrl );
    // http(s):// keeps its remote branch.
    CHECK( containment::classifyReference( QStringLiteral( "http://x/y" ) ).kind
            == ReferenceKind::Denied );
    // File-backed classification exposes the extracted path.
    const containment::ReferenceVerdict gpkg = containment::classifyReference(
        QStringLiteral( "GPKG:/data/x.gpkg:layer" ) );
    CHECK( gpkg.kind == ReferenceKind::DriverPath );
    CHECK( gpkg.localPath == QStringLiteral( "/data/x.gpkg" ) );
    const containment::ReferenceVerdict quoted = containment::classifyReference(
        QStringLiteral( "NETCDF:\"/data/x.nc\":temp" ) );
    CHECK( quoted.kind == ReferenceKind::DriverPath );
    CHECK( quoted.localPath == QStringLiteral( "/data/x.nc" ) );
    const containment::ReferenceVerdict drive = containment::classifyReference(
        QStringLiteral( "GPKG:C:/data/x.gpkg:layer" ) );
    CHECK( drive.kind == ReferenceKind::DriverPath );
    CHECK( drive.localPath == QStringLiteral( "C:/data/x.gpkg" ) );
    // R1-F2: a scheme:// payload inside a file-backed driver recurses into
    // the URL policy instead of degrading to a relative fragment.
    const containment::ReferenceVerdict url = containment::classifyReference(
        QStringLiteral( "GeoJSON:http://example.com/x.json" ) );
    CHECK( url.kind == ReferenceKind::Denied ); // remote disabled in this case
    // Unclosed quote / empty payload: fail closed.
    CHECK( containment::classifyReference( QStringLiteral( "NETCDF:\"/x.nc" ) ).kind
            == ReferenceKind::Denied );
    CHECK( containment::classifyReference( QStringLiteral( "CSV:" ) ).kind == ReferenceKind::Denied );
}
