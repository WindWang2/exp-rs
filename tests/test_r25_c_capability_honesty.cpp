// tests/test_r25_c_capability_honesty.cpp — R25-C W2 (capability honesty).
//
// The AlgorithmDescriptor's AgentMetadata carries DECLARED prerequisites
// and limitations (populated from operator metadata(), e.g.
// rs:spectral_index's "Input raster must have sufficient bands for the
// selected index."), and get_algorithm_schema/preflight already consume
// them — but the list_algorithms / search_algorithms ENTRY projection
// (algorithmEntryMap) dropped both fields, so an agent DISCOVERING the
// catalog saw memoryPolicy/tags/outputs yet no required preconditions.
// This file pins the honest entry shape:
//   - declared prerequisites/limitations surface as string lists;
//   - entries without them OMIT the keys (bounded token cost, no empty
//     arrays);
//   - deterministic/idempotent guarantees (planning hints preflight
//     advertises) are exposed compactly.
//
// Compile-in-process pattern: test_r14_C_mcp_workflow_cancel /
// test_mcp_server (McpServer handlers exercised directly).
#include <catch2/catch_test_macros.hpp>

#include <QVariantList>
#include <QVariantMap>
#include <QMetaType>

#include "agent/mcp_server.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/algorithm_engine.h"
#include "processing/framework/task_center.h"
#include <processing/qgsprocessingregistry.h>
#include <processing/providers/qgis_algorithms/provider.h>
#include <qgsapplication.h>

// The MCP sandbox is default-deny (unset SICNU_MCP_WORKSPACE = process
// CWD); this binary needs no file fixtures, so opt into the widest root
// explicitly — same contract as test_mcp_server.cpp.
static const bool kFixtureWorkspaceInstalled = [] {
    if ( qEnvironmentVariableIsEmpty( "SICNU_MCP_WORKSPACE" ) )
        qputenv( "SICNU_MCP_WORKSPACE", QDir::rootPath().toUtf8() );
    return true;
}();

namespace
{

class TestServer : public McpServer
{
public:
    // Paginate handleListAlgorithms to the full catalog (the MCP page cap
    // is 50; assertions target entries anywhere in it).
    QVariantList listAllAlgorithms()
    {
        QVariantList entries;
        int cursor = 0;
        for ( int guard = 0; guard < 200; ++guard )
        {
            const QVariantMap page = handleListAlgorithms( 500, cursor );
            const QVariantList slice = page.value( "algorithms" ).toList();
            if ( slice.isEmpty() )
                break;
            entries.append( slice );
            const QVariant next = page.value( "nextCursor" );
            bool ok = false;
            const int nextCursor = next.toInt( &ok );
            if ( !ok || nextCursor < 0 || nextCursor == cursor )
                break;
            cursor = nextCursor;
        }
        return entries;
    }

    QVariantMap search( const QVariantMap &arguments ) { return handleSearchAlgorithms( arguments ); }
};

QVariantMap findEntry( const QVariantList &entries, const QString &id )
{
    for ( const QVariant &v : entries )
    {
        const QVariantMap m = v.toMap();
        if ( m.value( "id" ).toString() == id )
            return m;
    }
    return {};
}

} // namespace

TEST_CASE( "algorithm entries declare prerequisites and limitations honestly",
           "[agent][mcp][r25_c]" )
{
    if ( !QgsApplication::processingRegistry()->providerById( "qgis_algorithms" ) )
        QgsApplication::processingRegistry()->addProvider( new QgisAlgorithmsProvider() );
    sicnu::AlgorithmEngine::instance().initialize();

    TestServer server;
    const QVariantList catalog = server.listAllAlgorithms();
    REQUIRE( catalog.size() >= 15 );

    SECTION( "rs:spectral_index surfaces its declared band-count prerequisite" )
    {
        const QVariantMap entry = findEntry( catalog, QStringLiteral( "rs:spectral_index" ) );
        REQUIRE_FALSE( entry.isEmpty() );
        const QVariantList prereqs = entry.value( "prerequisites" ).toList();
        REQUIRE( prereqs.size() >= 1 );
        bool mentionsBands = false;
        for ( const QVariant &p : prereqs )
            if ( p.toString().contains( QStringLiteral( "bands" ), Qt::CaseInsensitive ) )
                mentionsBands = true;
        CHECK( mentionsBands );
    }

    SECTION( "opencv:gaussian_blur surfaces its declared kernel limitation" )
    {
        const QVariantMap entry = findEntry( catalog, QStringLiteral( "opencv:gaussian_blur" ) );
        REQUIRE_FALSE( entry.isEmpty() );
        const QVariantList limits = entry.value( "limitations" ).toList();
        REQUIRE( limits.size() >= 1 );
        bool mentionsKernel = false;
        for ( const QVariant &l : limits )
            if ( l.toString().contains( QStringLiteral( "Kernel size" ), Qt::CaseInsensitive ) )
                mentionsKernel = true;
        CHECK( mentionsKernel );
    }

    SECTION( "search_algorithms hits carry the same declared fields" )
    {
        QVariantMap args;
        args.insert( QStringLiteral( "query" ), QStringLiteral( "spectral index" ) );
        args.insert( QStringLiteral( "limit" ), 500 );
        const QVariantMap res = server.search( args );
        const QVariantList hits = res.value( "algorithms" ).toList();
        REQUIRE_FALSE( hits.isEmpty() );
        const QVariantMap entry = findEntry( hits, QStringLiteral( "rs:spectral_index" ) );
        REQUIRE_FALSE( entry.isEmpty() );
        CHECK( entry.value( "prerequisites" ).toList().size() >= 1 );
    }

    SECTION( "entries without declared fields omit the keys entirely" )
    {
        // Bounded token cost: no empty [] arrays for the dozens of entries
        // that declare nothing.
        for ( const QVariant &v : catalog )
        {
            const QVariantMap m = v.toMap();
            if ( m.contains( QStringLiteral( "prerequisites" ) ) )
                CHECK_FALSE( m.value( "prerequisites" ).toList().isEmpty() );
            if ( m.contains( QStringLiteral( "limitations" ) ) )
                CHECK_FALSE( m.value( "limitations" ).toList().isEmpty() );
        }
    }

    SECTION( "deterministic/idempotent planning guarantees are exposed compactly" )
    {
        const QVariantMap entry = findEntry( catalog, QStringLiteral( "rs:spectral_index" ) );
        REQUIRE_FALSE( entry.isEmpty() );
        const QVariantMap guarantees = entry.value( "guarantees" ).toMap();
        CHECK( guarantees.contains( QStringLiteral( "deterministic" ) ) );
        CHECK( guarantees.contains( QStringLiteral( "idempotent" ) ) );
        CHECK( guarantees.contains( QStringLiteral( "supportsCancellation" ) ) );
    }

    SECTION( "provider-sourced entries carry the same guarantees shape" )
    {
        // Provider algorithms enter the universe through their adapter
        // descriptor, so both listing paths speak ONE shape: the guarantees
        // map exists for every entry, boolean-typed, and for a provider
        // entry it reflects the descriptor default (deterministic true) —
        // never a missing key or a prose string like "true".
        const QVariantMap entry = findEntry( catalog, QStringLiteral( "gdal:reproject" ) );
        if ( entry.isEmpty() )
        {
            WARN( "gdal:reproject not in this host's catalog — provider shape unchecked" );
            return;
        }
        const QVariantMap guarantees = entry.value( "guarantees" ).toMap();
        REQUIRE( guarantees.contains( QStringLiteral( "deterministic" ) ) );
        REQUIRE( guarantees.contains( QStringLiteral( "idempotent" ) ) );
        REQUIRE( guarantees.contains( QStringLiteral( "supportsCancellation" ) ) );
        // Booleans (JSON true/false), not strings like "true".
        CHECK( guarantees.value( QStringLiteral( "deterministic" ) ).userType()
               == QMetaType::Bool );
        CHECK( guarantees.value( QStringLiteral( "idempotent" ) ).userType()
               == QMetaType::Bool );
        CHECK( guarantees.value( QStringLiteral( "supportsCancellation" ) ).userType()
               == QMetaType::Bool );
    }
}
