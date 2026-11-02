// tests/test_r27_t03_containment_gdal_prefix.cpp — #1741 (AS-013)
//
// RED matrix for the subdataset-prefix bypass: unknown GDAL drivers whose
// real path sits behind selector segments (GTIFF_DIR:1:/abs, NITF_IM:0:…,
// HDF4_SDS:…:"quoted", GTIFF_DIR:1:/vsicurl/http://169.254.169.254/…, …)
// used to be classified LocalPath-inside because the danger scan looked
// only at the payload's first character. Every segment is scanned now; the
// in-workspace relative forms keep working. Same light shape as
// test_r22_agent_containment: Catch2 + Qt Core, no GDAL link — the header
// is the policy authority.
#include "agent/tool_catalog/workspace_containment.h"

#include <QDir>
#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>

namespace
{
struct SandboxEnvGuard
{
    QByteArray remote = qgetenv( "SICNU_MCP_ALLOW_REMOTE" );
    bool hadRemote = qEnvironmentVariableIsSet( "SICNU_MCP_ALLOW_REMOTE" );
    ~SandboxEnvGuard()
    {
        if ( hadRemote )
            qputenv( "SICNU_MCP_ALLOW_REMOTE", remote );
        else
            qunsetenv( "SICNU_MCP_ALLOW_REMOTE" );
    }
};
} // namespace

using namespace sicnu::agent::tool_catalog;
namespace containment = sicnu::agent::tool_catalog::containment;

TEST_CASE( "unknown GDAL subdataset prefixes cannot escape or fetch (#1741)",
           "[r27t03][containment][p1]" )
{
    SandboxEnvGuard guard;
    qunsetenv( "SICNU_MCP_ALLOW_REMOTE" );

    QTemporaryDir workspace;
    REQUIRE( workspace.isValid() );

    // Each entry must classify as DENIED (never LocalPath-inside, never
    // RemoteAllowed): the embedded absolute path, quoted path, vsi/http
    // target, Windows drive or device reference is the REAL open target.
    const QStringList escapes = {
        QStringLiteral( "GTIFF_DIR:1:/etc/shadow" ),
        QStringLiteral( "NITF_IM:0:/data/secret.ntf" ),
        QStringLiteral( "GTIFF_DIR:1:/vsicurl/http://169.254.169.254/latest/meta-data/" ),
        QStringLiteral( "GTIFF_DIR:1:/vsicurl/http://localhost:8080/inner.tif" ),
        QStringLiteral( "HDF4_SDS:HDF4_EOS:EOS_GRID:\"/abs/x.hdf\":0" ),
        QStringLiteral( "HDF5:\"//gid/data/file.h5\"://dset" ),
        QStringLiteral( "HDF4_SDS:UNKNOWN_KIND:key:/abs/y.hdf:1" ),
        QStringLiteral( "GTIFF_DIR:2:C:\\\\secret\\\\band.tif" ),
        QStringLiteral( "GTIFF_DIR:2:C:/secret/band.tif" ),
        QStringLiteral( "NITF_IM:1:../../../etc/passwd" ),
        QStringLiteral( "GTIFF_DIR:1:~/x.tif" ),
        QStringLiteral( "GTIFF_DIR:1:url=http://169.254.169.254/x" ),
        QStringLiteral( "GPSBABEL:garmin:usb:" ),
        QStringLiteral( "SENTINEL2_L1C:/abs/MTD_MSIL1C.xml" ),
    };
    for ( const QString &reference : escapes )
    {
        const containment::ReferenceVerdict verdict =
            containment::classifyReference( reference );
        INFO( "reference: " << reference.toStdString()
                            << " kind=" << static_cast<int>( verdict.kind ) );
        CHECK( verdict.kind == containment::ReferenceKind::Denied );
    }

    // And through the full containment decision with the workspace set —
    // the escape shapes are rejected, not merely re-classified.
    QString detail;
    for ( const QString &reference : escapes )
    {
        INFO( "pathOutsideWorkspace: " << reference.toStdString() );
        CHECK( containment::pathOutsideWorkspace( reference, workspace.path(), &detail ) );
    }
}

TEST_CASE( "in-workspace relative subdataset references still work (#1741)",
           "[r27t03][containment][regression]" )
{
    SandboxEnvGuard guard;
    qunsetenv( "SICNU_MCP_ALLOW_REMOTE" );

    QTemporaryDir workspace;
    REQUIRE( workspace.isValid() );

    // Relative payloads anchor at the process CWD (the server chdir()s into
    // the workspace), so the fallback keeps subdataset workflows usable.
    const QStringList benign = {
        QStringLiteral( "GTIFF_DIR:1:data.tif" ),
        QStringLiteral( "NITF_IM:0:scene.ntf" ),
        QStringLiteral( "HDF4_SDS:HDF4_EOS:EOS_GRID:x.hdf:0" ),
    };
    for ( const QString &reference : benign )
    {
        const containment::ReferenceVerdict verdict =
            containment::classifyReference( reference );
        INFO( "reference: " << reference.toStdString()
                            << " kind=" << static_cast<int>( verdict.kind ) );
        CHECK( verdict.kind == containment::ReferenceKind::LocalPath );
    }

    // A POSIX filename that merely contains a colon keeps its legacy
    // relative-name treatment.
    CHECK( containment::classifyReference( QStringLiteral( "report:final.tif" ) ).kind ==
           containment::ReferenceKind::LocalPath );
}
