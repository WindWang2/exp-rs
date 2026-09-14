// tests/test_workflow_ir_v2.cpp — Workflow IR 2.0 contract tests (D17 Package A)
//
// Ground truth: hand-authored golden fixtures under
// tests/fixtures/workflow_ir_v2/ (canonical documents), hand-constructed
// adversarial documents, and a hand-traced V1 migration expectation.
// No expectation is derived by calling into the code under test.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QDir>

#include "workflow/workflow_ir_v2.h"

using sicnu::workflow::EdgeFact;
using sicnu::workflow::NodeFact;
using sicnu::workflow::PortFact;
using sicnu::workflow::Result;
using sicnu::workflow::WorkflowDefinition;
using sicnu::workflow::WorkflowIR;

namespace {

QString fixturesDir()
{
    return QString( "%1/tests/fixtures/workflow_ir_v2" ).arg( CMAKE_SOURCE_DIR );
}

QJsonObject loadGoldenJson( const QString &fileName )
{
    QFile file( QString( "%1/%2" ).arg( fixturesDir(), fileName ) );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    QJsonDocument doc = QJsonDocument::fromJson( file.readAll() );
    REQUIRE( !doc.isNull() );
    REQUIRE( doc.isObject() );
    return doc.object();
}

} // namespace

TEST_CASE( "Golden linear pipeline parses and round-trips byte-identically", "[d17][workflow][ir]" )
{
    const QJsonObject inDoc = loadGoldenJson( QStringLiteral( "linear_pipeline_v2.json" ) );

    auto parseResult = WorkflowIR::fromJson( inDoc );
    REQUIRE( parseResult.isSuccess() );

    const WorkflowDefinition &def = parseResult.value();
    REQUIRE( def.version == QStringLiteral( "2.0" ) );
    REQUIRE( def.nodes.size() == 5 );
    REQUIRE( def.edges.size() == 4 );
    REQUIRE( def.findNode( "node_ndvi" ) != nullptr );
    REQUIRE( def.findNode( "node_ndvi" )->operatorId == QStringLiteral( "rs:spectral_index" ) );
    REQUIRE( def.findNode( "node_ndvi" )->parameters["index"].toString() == QStringLiteral( "NDVI" ) );

    QJsonObject outJson = WorkflowIR::toJson( def );
    REQUIRE( QJsonDocument( outJson ).toJson( QJsonDocument::Indented )
             == QJsonDocument( inDoc ).toJson( QJsonDocument::Indented ) );
}

TEST_CASE( "All golden fixtures survive parse -> serialize -> parse unchanged", "[d17][workflow][ir]" )
{
    const QDir dir( fixturesDir() );
    const QStringList v2Fixtures = dir.entryList( { "*_v2.json" }, QDir::Files, QDir::Name );
    REQUIRE( v2Fixtures.size() == 9 );

    for ( const QString &fixture : v2Fixtures )
    {
        DYNAMIC_SECTION( fixture.toStdString() )
        {
            const QJsonObject inDoc = loadGoldenJson( fixture );
            auto first = WorkflowIR::fromJson( inDoc );
            REQUIRE( first.isSuccess() );

            const QJsonObject once = WorkflowIR::toJson( first.value() );
            INFO( "fixture: " << fixture.toStdString() );
            REQUIRE( QJsonDocument( once ).toJson( QJsonDocument::Indented )
                     == QJsonDocument( inDoc ).toJson( QJsonDocument::Indented ) );

            // D(S(A)) == A: reparsing the canonical serialization is the same AST.
            auto second = WorkflowIR::fromJson( once );
            REQUIRE( second.isSuccess() );
            REQUIRE( second.value() == first.value() );
        }
    }
}

TEST_CASE( "fromDocument round trip of in-memory AST equals AST (D(S(A)) == A)", "[d17][workflow][ir]" )
{
    WorkflowDefinition def;
    def.workflowId = QStringLiteral( "wf-mem-1" );
    def.name = QStringLiteral( "In-memory" );
    NodeFact node;
    node.nodeId = QStringLiteral( "n1" );
    node.operatorId = QStringLiteral( "rs:import_raster" );
    node.displayName = QStringLiteral( "Import" );
    node.parameters = QJsonObject{ { "sensor", QStringLiteral( "GF-1" ) } };
    node.inputPorts = {};
    node.outputPorts = { PortFact{ QStringLiteral( "output" ), QStringLiteral( "Raster" ), QStringLiteral( "EPSG:32649" ),
                                   QStringLiteral( "DN" ), 10.0, 10.0, 4, false } };
    node.canvasPosition = QPointF( 12.5, -3.25 );
    def.nodes = { node };

    auto reparsed = WorkflowIR::fromJson( WorkflowIR::toJson( def ) );
    REQUIRE( reparsed.isSuccess() );
    REQUIRE( reparsed.value() == def );
}

TEST_CASE( "fromJson rejects malformed documents with fail-closed errors", "[d17][workflow][ir]" )
{
    SECTION( "missing version" )
    {
        QJsonObject doc = loadGoldenJson( QStringLiteral( "minimal_single_node_v2.json" ) );
        doc.remove( QStringLiteral( "version" ) );
        auto result = WorkflowIR::fromJson( doc );
        REQUIRE_FALSE( result.isSuccess() );
        REQUIRE( result.error().contains( QStringLiteral( "version" ) ) );
    }
    SECTION( "wrong version" )
    {
        QJsonObject doc = loadGoldenJson( QStringLiteral( "minimal_single_node_v2.json" ) );
        doc[QStringLiteral( "version" )] = QStringLiteral( "9.9" );
        REQUIRE_FALSE( WorkflowIR::fromJson( doc ).isSuccess() );
    }
    SECTION( "node without nodeId" )
    {
        QJsonObject doc = loadGoldenJson( QStringLiteral( "minimal_single_node_v2.json" ) );
        QJsonArray nodes = doc[QStringLiteral( "nodes" )].toArray();
        QJsonObject first = nodes[0].toObject();
        first.remove( QStringLiteral( "nodeId" ) );
        nodes[0] = first;
        doc[QStringLiteral( "nodes" )] = nodes;
        auto result = WorkflowIR::fromJson( doc );
        REQUIRE_FALSE( result.isSuccess() );
        REQUIRE( result.error().contains( QStringLiteral( "nodeId" ) ) );
    }
    SECTION( "node without operatorId" )
    {
        QJsonObject doc = loadGoldenJson( QStringLiteral( "minimal_single_node_v2.json" ) );
        QJsonArray nodes = doc[QStringLiteral( "nodes" )].toArray();
        QJsonObject first = nodes[0].toObject();
        first.remove( QStringLiteral( "operatorId" ) );
        nodes[0] = first;
        doc[QStringLiteral( "nodes" )] = nodes;
        REQUIRE_FALSE( WorkflowIR::fromJson( doc ).isSuccess() );
    }
    SECTION( "duplicate node ids" )
    {
        QJsonObject doc = loadGoldenJson( QStringLiteral( "minimal_single_node_v2.json" ) );
        QJsonArray nodes = doc[QStringLiteral( "nodes" )].toArray();
        nodes.append( nodes[0] );
        doc[QStringLiteral( "nodes" )] = nodes;
        auto result = WorkflowIR::fromJson( doc );
        // Structural duplication is a semantics failure.
        REQUIRE( result.isSuccess() );
        QString error;
        REQUIRE_FALSE( WorkflowIR::validateSemantics( result.value(), &error ) );
        REQUIRE( error.contains( QStringLiteral( "solo" ) ) );
    }
}

TEST_CASE( "Semantic validation rejects dangling edges naming the offender", "[d17][workflow][ir]" )
{
    WorkflowDefinition def;
    def.version = QStringLiteral( "2.0" );
    def.nodes = { NodeFact{ QStringLiteral( "node_b" ), QStringLiteral( "rs:threshold" ), QStringLiteral( "B" ), {},
                            { PortFact{ QStringLiteral( "in" ), QStringLiteral( "Raster" ), QStringLiteral( "*" ), QStringLiteral( "None" ), 0, 0, 0, true } },
                            {}, QPointF() } };
    def.edges = { EdgeFact{ QStringLiteral( "e1" ), QStringLiteral( "non_existent_node" ), QStringLiteral( "out" ),
                            QStringLiteral( "node_b" ), QStringLiteral( "in" ) } };

    QString error;
    REQUIRE_FALSE( WorkflowIR::validateSemantics( def, &error ) );
    REQUIRE( error.contains( QStringLiteral( "non_existent_node" ) ) );
}

TEST_CASE( "Semantic validation enforces the single-source in-degree invariant", "[d17][workflow][ir]" )
{
    // Two edges feeding the SAME input port of the same node — degree 2 > 1.
    PortFact outPort{ QStringLiteral( "output" ), QStringLiteral( "Raster" ), QStringLiteral( "*" ), QStringLiteral( "None" ), 0, 0, 0, false };
    PortFact inPort{ QStringLiteral( "in" ), QStringLiteral( "Raster" ), QStringLiteral( "*" ), QStringLiteral( "None" ), 0, 0, 0, true };
    NodeFact src1{ QStringLiteral( "src1" ), QStringLiteral( "rs:import_raster" ), {}, {}, {}, { outPort }, QPointF() };
    NodeFact src2{ QStringLiteral( "src2" ), QStringLiteral( "rs:import_raster" ), {}, {}, {}, { outPort }, QPointF() };
    NodeFact sink{ QStringLiteral( "sink" ), QStringLiteral( "rs:spatial_filter" ), {}, {}, { inPort }, {}, QPointF() };
    WorkflowDefinition def;
    def.nodes = { src1, src2, sink };
    def.edges = { EdgeFact{ QStringLiteral( "e1" ), QStringLiteral( "src1" ), QStringLiteral( "output" ), QStringLiteral( "sink" ), QStringLiteral( "in" ) },
                  EdgeFact{ QStringLiteral( "e2" ), QStringLiteral( "src2" ), QStringLiteral( "output" ), QStringLiteral( "sink" ), QStringLiteral( "in" ) } };

    QString error;
    REQUIRE_FALSE( WorkflowIR::validateSemantics( def, &error ) );
    REQUIRE( error.contains( QStringLiteral( "in" ) ) );

    // Feeding two DIFFERENT ports is legal (fan-in at node level).
    sink.inputPorts = { inPort, PortFact{ QStringLiteral( "aux" ), QStringLiteral( "Raster" ), QStringLiteral( "*" ), QStringLiteral( "None" ), 0, 0, 0, true } };
    def.nodes = { src1, src2, sink };
    def.edges = { EdgeFact{ QStringLiteral( "e1" ), QStringLiteral( "src1" ), QStringLiteral( "output" ), QStringLiteral( "sink" ), QStringLiteral( "in" ) },
                  EdgeFact{ QStringLiteral( "e2" ), QStringLiteral( "src2" ), QStringLiteral( "output" ), QStringLiteral( "sink" ), QStringLiteral( "aux" ) } };
    REQUIRE( WorkflowIR::validateSemantics( def ) );
}

TEST_CASE( "Semantic validation rejects edges onto missing ports", "[d17][workflow][ir]" )
{
    NodeFact src{ QStringLiteral( "src" ), QStringLiteral( "rs:import_raster" ), {}, {}, {}, { PortFact{ QStringLiteral( "output" ), QStringLiteral( "Raster" ), QStringLiteral( "*" ), QStringLiteral( "None" ), 0, 0, 0, false } }, QPointF() };
    NodeFact dst{ QStringLiteral( "dst" ), QStringLiteral( "rs:spatial_filter" ), {}, {}, { PortFact{ QStringLiteral( "in" ), QStringLiteral( "Raster" ), QStringLiteral( "*" ), QStringLiteral( "None" ), 0, 0, 0, true } }, {}, QPointF() };
    WorkflowDefinition def;
    def.nodes = { src, dst };
    def.edges = { EdgeFact{ QStringLiteral( "e1" ), QStringLiteral( "src" ), QStringLiteral( "no_such_port" ), QStringLiteral( "dst" ), QStringLiteral( "in" ) } };
    QString error;
    REQUIRE_FALSE( WorkflowIR::validateSemantics( def, &error ) );
    REQUIRE( error.contains( QStringLiteral( "no_such_port" ) ) );

    def.edges = { EdgeFact{ QStringLiteral( "e1" ), QStringLiteral( "src" ), QStringLiteral( "output" ), QStringLiteral( "dst" ), QStringLiteral( "no_such_port" ) } };
    REQUIRE_FALSE( WorkflowIR::validateSemantics( def ) );
}

TEST_CASE( "isValid reflects version, id and operator completeness", "[d17][workflow][ir]" )
{
    WorkflowDefinition def;
    REQUIRE( def.isValid() ); // empty document with default version is structurally valid

    def.version = QStringLiteral( "1.0" );
    REQUIRE_FALSE( def.isValid() );
    def.version = QStringLiteral( "2.0" );

    def.nodes = { NodeFact{ QString(), QStringLiteral( "rs:x" ), {}, {}, {}, {}, QPointF() } };
    REQUIRE_FALSE( def.isValid() );

    def.nodes = { NodeFact{ QStringLiteral( "n" ), QString(), {}, {}, {}, {}, QPointF() } };
    REQUIRE_FALSE( def.isValid() );

    def.nodes = { NodeFact{ QStringLiteral( "n" ), QStringLiteral( "rs:x" ), {}, {}, {}, {}, QPointF() } };
    REQUIRE( def.isValid() );
}

TEST_CASE( "findNode and findEdge resolve by id and return nullptr when absent", "[d17][workflow][ir]" )
{
    const WorkflowDefinition def = WorkflowIR::fromJson(
        loadGoldenJson( QStringLiteral( "diamond_branch_merge_v2.json" ) ) ).value();

    REQUIRE( def.findNode( "node_a" ) != nullptr );
    REQUIRE( def.findNode( "node_zzz" ) == nullptr );
    REQUIRE( def.findEdge( "d_e3" ) != nullptr );
    REQUIRE( def.findEdge( "d_e3" )->sourceNodeId == QStringLiteral( "node_a" ) );
    REQUIRE( def.findEdge( "missing" ) == nullptr );
}

TEST_CASE( "migrateFromV1 lifts the ADR 0149 document with defaulted facts", "[d17][workflow][ir]" )
{
    const QJsonObject v1 = loadGoldenJson( QStringLiteral( "migrate_from_v1.json" ) );
    auto result = WorkflowIR::migrateFromV1( v1 );
    REQUIRE( result.isSuccess() );

    const WorkflowDefinition &def = result.value();
    REQUIRE( def.version == QStringLiteral( "2.0" ) );
    REQUIRE( def.nodes.size() == 2 );
    REQUIRE( def.edges.size() == 1 );

    // Hand-traced expectations: V1 nodes keep ids and operators, wiring is
    // lifted 1:1, DN domain maps to radiometricState "DN".
    const NodeFact *importNode = def.findNode( "v1_import" );
    REQUIRE( importNode != nullptr );
    REQUIRE( importNode->operatorId == QStringLiteral( "rs:import_raster" ) );
    REQUIRE( importNode->outputPorts.size() == 1 );
    REQUIRE( importNode->outputPorts[0].radiometricState == QStringLiteral( "DN" ) );
    REQUIRE( importNode->outputPorts[0].crs == QStringLiteral( "EPSG:32649" ) );
    REQUIRE( importNode->outputPorts[0].bandCount == 4 );
    // canvasPosition was absent in V1: deterministic grid layout.
    REQUIRE( importNode->canvasPosition == QPointF( 0.0, 0.0 ) );

    const NodeFact *ndvi = def.findNode( "v1_ndvi" );
    REQUIRE( ndvi != nullptr );
    REQUIRE( ndvi->inputPorts[0].radiometricState == QStringLiteral( "DN" ) );
    REQUIRE( ndvi->outputPorts[0].radiometricState == QStringLiteral( "Index" ) );
    REQUIRE( ndvi->canvasPosition == QPointF( 280.0, 0.0 ) );

    REQUIRE( def.edges[0].sourceNodeId == QStringLiteral( "v1_import" ) );
    REQUIRE( def.edges[0].targetNodeId == QStringLiteral( "v1_ndvi" ) );
    REQUIRE( def.edges[0].targetPortName == QStringLiteral( "input" ) );

    // The lifted document is semantically valid and round-trips.
    REQUIRE( WorkflowIR::validateSemantics( def ) );
    const QJsonObject canonical = WorkflowIR::toJson( def );
    auto reparsed = WorkflowIR::fromJson( canonical );
    REQUIRE( reparsed.isSuccess() );
    REQUIRE( reparsed.value() == def );
}

TEST_CASE( "migrateFromV1 fails closed on malformed V1 documents", "[d17][workflow][ir]" )
{
    SECTION( "wrong kind" )
    {
        QJsonObject v1 = loadGoldenJson( QStringLiteral( "migrate_from_v1.json" ) );
        v1[QStringLiteral( "kind" )] = QStringLiteral( "something_else" );
        REQUIRE_FALSE( WorkflowIR::migrateFromV1( v1 ).isSuccess() );
    }
    SECTION( "wrong schema_version" )
    {
        QJsonObject v1 = loadGoldenJson( QStringLiteral( "migrate_from_v1.json" ) );
        v1[QStringLiteral( "schema_version" )] = QStringLiteral( "2.0" );
        REQUIRE_FALSE( WorkflowIR::migrateFromV1( v1 ).isSuccess() );
    }
    SECTION( "wiring references a ghost upstream node" )
    {
        const QJsonObject v1 = loadGoldenJson( QStringLiteral( "invalid_v1_document.json" ) );
        auto result = WorkflowIR::migrateFromV1( v1 );
        // The lift itself succeeds structurally (id mapping is syntax-level);
        // the ghost reference becomes a semantic failure.
        QString error;
        if ( result.isSuccess() )
            REQUIRE_FALSE( WorkflowIR::validateSemantics( result.value(), &error ) );
        else
            REQUIRE( !result.error().isEmpty() );
    }
    SECTION( "empty object" )
    {
        REQUIRE_FALSE( WorkflowIR::migrateFromV1( QJsonObject{} ).isSuccess() );
    }
}
