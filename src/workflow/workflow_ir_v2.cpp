// src/workflow/workflow_ir_v2.cpp — Workflow IR 2.0 parsing / serialization / validation (D17)
#include "workflow/workflow_ir_v2.h"

#include <algorithm>
#include <utility>
#include <QHash>
#include <QSet>

namespace sicnu::workflow {
namespace {

constexpr const char *kVersion20 = "2.0";
constexpr const char *kVersionCurrent = "2.1";
constexpr const char *kV1Kind = "workflow_ir";
constexpr const char *kV1SchemaVersion = "1.0";
// Deterministic migration grid: 4 nodes per row, 280 x 140 px cells.
constexpr double kGridCellWidth = 280.0;
constexpr double kGridCellHeight = 140.0;
constexpr int kGridColumns = 4;

QString portString( const QJsonObject &port, const char *key, QString *error, const QString &context )
{
    const QJsonValue value = port.value( QLatin1String( key ) );
    if ( !value.isString() )
    {
        if ( error && error->isEmpty() )
            *error = QStringLiteral( "port '%1': missing or non-string field '%2'" )
                         .arg( context, QLatin1String( key ) );
        return QString();
    }
    return value.toString();
}

double portDouble( const QJsonObject &port, const char *key, QString *error, const QString &context )
{
    const QJsonValue value = port.value( QLatin1String( key ) );
    if ( !value.isDouble() )
    {
        if ( error && error->isEmpty() )
            *error = QStringLiteral( "port '%1': missing or non-numeric field '%2'" )
                         .arg( context, QLatin1String( key ) );
        return 0.0;
    }
    return value.toDouble();
}

Result<PortFact> parsePort( const QJsonValue &value, const QString &context )
{
    // A fresh error buffer per port: a stale message from a previous port
    // must never reject THIS port, and every failure must fail closed.
    QString error;
    if ( !value.isObject() )
        return Result<PortFact>::error( QStringLiteral( "%1: port is not an object" ).arg( context ) );
    const QJsonObject obj = value.toObject();

    PortFact port;
    port.portName = portString( obj, "portName", &error, context );
    if ( !error.isEmpty() )
        return Result<PortFact>::error( error );
    port.dataType = portString( obj, "dataType", &error, context );
    if ( !error.isEmpty() )
        return Result<PortFact>::error( error );
    port.crs = portString( obj, "crs", &error, context );
    if ( !error.isEmpty() )
        return Result<PortFact>::error( error );
    port.radiometricState = portString( obj, "radiometricState", &error, context );
    if ( !error.isEmpty() )
        return Result<PortFact>::error( error );
    port.resolutionX = portDouble( obj, "resolutionX", &error, context );
    if ( !error.isEmpty() )
        return Result<PortFact>::error( error );
    port.resolutionY = portDouble( obj, "resolutionY", &error, context );
    if ( !error.isEmpty() )
        return Result<PortFact>::error( error );
    const QJsonValue bands = obj.value( QLatin1String( "bandCount" ) );
    if ( !bands.isDouble() )
        return Result<PortFact>::error( QStringLiteral( "%1: missing or non-numeric field 'bandCount'" ).arg( context ) );
    port.bandCount = bands.toInt();
    const QJsonValue required = obj.value( QLatin1String( "isRequired" ) );
    if ( !required.isBool() )
        return Result<PortFact>::error( QStringLiteral( "%1: missing or non-bool field 'isRequired'" ).arg( context ) );
    port.isRequired = required.toBool();
    return Result<PortFact>::ok( port );
}

QJsonObject portToJson( const PortFact &port )
{
    QJsonObject obj;
    obj.insert( QLatin1String( "bandCount" ), port.bandCount );
    obj.insert( QLatin1String( "crs" ), port.crs );
    obj.insert( QLatin1String( "dataType" ), port.dataType );
    obj.insert( QLatin1String( "isRequired" ), port.isRequired );
    obj.insert( QLatin1String( "portName" ), port.portName );
    obj.insert( QLatin1String( "radiometricState" ), port.radiometricState );
    obj.insert( QLatin1String( "resolutionX" ), port.resolutionX );
    obj.insert( QLatin1String( "resolutionY" ), port.resolutionY );
    return obj;
}

QJsonArray portsToJson( const QVector<PortFact> &ports )
{
    QJsonArray array;
    for ( const PortFact &port : ports )
        array.append( portToJson( port ) );
    return array;
}

QString radiometricStateFromV1Domain( const QJsonObject &artifact )
{
    const QString domain = artifact.value( QLatin1String( "numeric_domain" ) ).toString();
    if ( domain == QLatin1String( "dn" ) )
        return QStringLiteral( "DN" );
    if ( domain == QLatin1String( "surface_reflectance" ) )
        return QStringLiteral( "BOA" );
    if ( domain == QLatin1String( "toa" ) )
        return QStringLiteral( "TOA" );
    if ( domain == QLatin1String( "index" ) )
        return QStringLiteral( "Index" );
    if ( domain == QLatin1String( "masked" ) )
        return QStringLiteral( "Mask" );
    return QStringLiteral( "None" );
}

PortFact v1Port( const QString &name, const QJsonObject &artifact )
{
    PortFact port;
    port.portName = name;
    port.dataType = QStringLiteral( "Raster" );
    port.crs = artifact.value( QLatin1String( "crs" ) ).toString();
    if ( port.crs.isEmpty() )
        port.crs = QStringLiteral( "*" );
    port.radiometricState = radiometricStateFromV1Domain( artifact );
    port.resolutionX = artifact.value( QLatin1String( "resolution_m" ) ).toDouble();
    port.resolutionY = port.resolutionX;
    port.bandCount = artifact.value( QLatin1String( "bands" ) ).toInt();
    port.isRequired = false;
    return port;
}

} // namespace

bool WorkflowDocument::isValid() const
{
    if ( !WorkflowIR::supportedSchemaVersions().contains( version ) )
        return false;
    QSet<QString> ids;
    for ( const NodeFact &node : nodes )
    {
        if ( node.nodeId.isEmpty() || node.operatorId.isEmpty() )
            return false;
        if ( ids.contains( node.nodeId ) )
            return false;
        ids.insert( node.nodeId );
    }
    return true;
}

const NodeFact *WorkflowDocument::findNode( const QString &id ) const
{
    for ( const NodeFact &node : nodes )
        if ( node.nodeId == id )
            return &node;
    return nullptr;
}

const EdgeFact *WorkflowDocument::findEdge( const QString &id ) const
{
    for ( const EdgeFact &edge : edges )
        if ( edge.edgeId == id )
            return &edge;
    return nullptr;
}

QStringList WorkflowIR::supportedSchemaVersions()
{
    return { QStringLiteral( "2.0" ), QStringLiteral( "2.1" ) };
}

QString WorkflowIR::currentSchemaVersion()
{
    return QStringLiteral( "2.1" );
}

Result<WorkflowDocument> WorkflowIR::fromJson( const QJsonObject &doc )
{
    const QString claimed = doc.value( QLatin1String( "version" ) ).toString();
    if ( claimed != QLatin1String( kVersion20 ) && claimed != QLatin1String( kVersionCurrent ) )
        return Result<WorkflowDocument>::error(
            QStringLiteral( "unsupported or missing workflow IR version '%1' (supported: %2)" )
                .arg( claimed, supportedSchemaVersions().join( QLatin1String( ", " ) ) ) );

    WorkflowDocument def;
    // Preserve the claimed version: a 2.0 document stays "2.0" through the
    // round-trip; additive-optional fields parse on both members.
    def.version = claimed;
    def.workflowId = doc.value( QLatin1String( "workflowId" ) ).toString();
    def.name = doc.value( QLatin1String( "name" ) ).toString();
    def.description = doc.value( QLatin1String( "description" ) ).toString();
    def.metadata = doc.value( QLatin1String( "metadata" ) ).toObject();

    const QJsonArray nodes = doc.value( QLatin1String( "nodes" ) ).toArray();
    def.nodes.reserve( nodes.size() );
    for ( const QJsonValue &nodeValue : nodes )
    {
        if ( !nodeValue.isObject() )
            return Result<WorkflowDocument>::error( QStringLiteral( "node entry is not an object" ) );
        const QJsonObject nodeObj = nodeValue.toObject();

        NodeFact node;
        node.nodeId = nodeObj.value( QLatin1String( "nodeId" ) ).toString();
        node.operatorId = nodeObj.value( QLatin1String( "operatorId" ) ).toString();
        node.displayName = nodeObj.value( QLatin1String( "displayName" ) ).toString();
        if ( node.nodeId.isEmpty() )
            return Result<WorkflowDocument>::error( QStringLiteral( "node: missing 'nodeId'" ) );
        if ( node.operatorId.isEmpty() )
            return Result<WorkflowDocument>::error(
                QStringLiteral( "node '%1': missing 'operatorId'" ).arg( node.nodeId ) );

        const QJsonValue params = nodeObj.value( QLatin1String( "parameters" ) );
        if ( !params.isObject() )
            return Result<WorkflowDocument>::error(
                QStringLiteral( "node '%1': missing or non-object 'parameters'" ).arg( node.nodeId ) );
        node.parameters = params.toObject();

        const QJsonValue position = nodeObj.value( QLatin1String( "canvasPosition" ) );
        if ( !position.isObject() )
            return Result<WorkflowDocument>::error(
                QStringLiteral( "node '%1': missing or non-object 'canvasPosition'" ).arg( node.nodeId ) );
        const QJsonObject positionObj = position.toObject();
        const QJsonValue x = positionObj.value( QLatin1String( "x" ) );
        const QJsonValue y = positionObj.value( QLatin1String( "y" ) );
        if ( !x.isDouble() || !y.isDouble() )
            return Result<WorkflowDocument>::error(
                QStringLiteral( "node '%1': canvasPosition needs numeric 'x' and 'y'" ).arg( node.nodeId ) );
        node.canvasPosition = QPointF( x.toDouble(), y.toDouble() );

        // 2.1 additive-optional: composition provenance. Absent/empty for
        // authored nodes; only ever written by subflow expansion.
        node.originNodeId = nodeObj.value( QLatin1String( "originNodeId" ) ).toString();

        for ( const QString &listKey : { QStringLiteral( "inputPorts" ), QStringLiteral( "outputPorts" ) } )
        {
            const QJsonArray ports = nodeObj.value( listKey ).toArray();
            QVector<PortFact> &target = ( listKey == QLatin1String( "inputPorts" ) ) ? node.inputPorts : node.outputPorts;
            target.reserve( ports.size() );
            for ( const QJsonValue &portValue : ports )
            {
                auto port = parsePort( portValue, node.nodeId );
                if ( !port.isSuccess() )
                    return Result<WorkflowDocument>::error(
                        QStringLiteral( "node '%1': %2" ).arg( node.nodeId, port.error() ) );
                target.append( port.value() );
            }
        }
        def.nodes.append( node );
    }

    const QJsonArray edges = doc.value( QLatin1String( "edges" ) ).toArray();
    def.edges.reserve( edges.size() );
    for ( const QJsonValue &edgeValue : edges )
    {
        if ( !edgeValue.isObject() )
            return Result<WorkflowDocument>::error( QStringLiteral( "edge entry is not an object" ) );
        const QJsonObject edgeObj = edgeValue.toObject();
        EdgeFact edge;
        edge.edgeId = edgeObj.value( QLatin1String( "edgeId" ) ).toString();
        edge.sourceNodeId = edgeObj.value( QLatin1String( "sourceNodeId" ) ).toString();
        edge.sourcePortName = edgeObj.value( QLatin1String( "sourcePortName" ) ).toString();
        edge.targetNodeId = edgeObj.value( QLatin1String( "targetNodeId" ) ).toString();
        edge.targetPortName = edgeObj.value( QLatin1String( "targetPortName" ) ).toString();
        if ( edge.edgeId.isEmpty() || edge.sourceNodeId.isEmpty() || edge.targetNodeId.isEmpty()
             || edge.sourcePortName.isEmpty() || edge.targetPortName.isEmpty() )
            return Result<WorkflowDocument>::error(
                QStringLiteral( "edge '%1': all five fields are required" ).arg( edge.edgeId ) );
        def.edges.append( edge );
    }

    return Result<WorkflowDocument>::ok( def );
}

QJsonObject WorkflowIR::toJson( const WorkflowDocument &def )
{
    QJsonObject doc;
    doc.insert( QLatin1String( "description" ), def.description );
    doc.insert( QLatin1String( "metadata" ), def.metadata );
    doc.insert( QLatin1String( "name" ), def.name );

    QJsonArray edges;
    for ( const EdgeFact &edge : def.edges )
    {
        QJsonObject edgeObj;
        edgeObj.insert( QLatin1String( "edgeId" ), edge.edgeId );
        edgeObj.insert( QLatin1String( "sourceNodeId" ), edge.sourceNodeId );
        edgeObj.insert( QLatin1String( "sourcePortName" ), edge.sourcePortName );
        edgeObj.insert( QLatin1String( "targetNodeId" ), edge.targetNodeId );
        edgeObj.insert( QLatin1String( "targetPortName" ), edge.targetPortName );
        edges.append( edgeObj );
    }
    doc.insert( QLatin1String( "edges" ), edges );

    QJsonArray nodes;
    for ( const NodeFact &node : def.nodes )
    {
        QJsonObject nodeObj;
        nodeObj.insert( QLatin1String( "canvasPosition" ),
                        QJsonObject{ { "x", node.canvasPosition.x() }, { "y", node.canvasPosition.y() } } );
        nodeObj.insert( QLatin1String( "displayName" ), node.displayName );
        nodeObj.insert( QLatin1String( "inputPorts" ), portsToJson( node.inputPorts ) );
        nodeObj.insert( QLatin1String( "nodeId" ), node.nodeId );
        nodeObj.insert( QLatin1String( "operatorId" ), node.operatorId );
        nodeObj.insert( QLatin1String( "outputPorts" ), portsToJson( node.outputPorts ) );
        nodeObj.insert( QLatin1String( "parameters" ), node.parameters );
        if ( !node.originNodeId.isEmpty() )
            nodeObj.insert( QLatin1String( "originNodeId" ), node.originNodeId );
        nodes.append( nodeObj );
    }
    doc.insert( QLatin1String( "nodes" ), nodes );
    doc.insert( QLatin1String( "version" ), def.version );
    doc.insert( QLatin1String( "workflowId" ), def.workflowId );
    return doc;
}

bool WorkflowIR::validateSemantics( const WorkflowDocument &def, QString *outError )
{
    auto fail = [outError]( const QString &message ) {
        if ( outError )
            *outError = message;
        return false;
    };

    // The claimed schema version is semantic input: a document asserting a
    // version outside the closed set is not executable — a run built on it
    // would write checkpoints this build's own reader refuses.
    if ( !supportedSchemaVersions().contains( def.version ) )
        return fail( QStringLiteral( "workflow document claims unsupported version '%1' (supported: %2)" )
                         .arg( def.version, supportedSchemaVersions().join( QLatin1String( ", " ) ) ) );

    QSet<QString> nodeIds;
    for ( const NodeFact &node : def.nodes )
    {
        if ( node.nodeId.isEmpty() )
            return fail( QStringLiteral( "node with empty nodeId" ) );
        if ( nodeIds.contains( node.nodeId ) )
            return fail( QStringLiteral( "duplicate nodeId '%1'" ).arg( node.nodeId ) );
        nodeIds.insert( node.nodeId );
    }

    QSet<QString> edgeIds;
    // targetPortName -> incoming edge count (single-source invariant).
    QHash<QPair<QString, QString>, int> portInDegree;
    for ( const EdgeFact &edge : def.edges )
    {
        if ( edge.edgeId.isEmpty() )
            return fail( QStringLiteral( "edge with empty edgeId" ) );
        if ( edgeIds.contains( edge.edgeId ) )
            return fail( QStringLiteral( "duplicate edgeId '%1'" ).arg( edge.edgeId ) );
        edgeIds.insert( edge.edgeId );

        const NodeFact *source = def.findNode( edge.sourceNodeId );
        if ( !source )
            return fail( QStringLiteral( "edge '%1' references non-existent source node '%2'" )
                             .arg( edge.edgeId, edge.sourceNodeId ) );
        const NodeFact *target = def.findNode( edge.targetNodeId );
        if ( !target )
            return fail( QStringLiteral( "edge '%1' references non-existent target node '%2'" )
                             .arg( edge.edgeId, edge.targetNodeId ) );

        const bool sourceHasPort = std::any_of( source->outputPorts.cbegin(), source->outputPorts.cend(),
                                                [&]( const PortFact &p ) { return p.portName == edge.sourcePortName; } );
        if ( !sourceHasPort )
            return fail( QStringLiteral( "edge '%1': source node '%2' has no output port '%3'" )
                             .arg( edge.edgeId, edge.sourceNodeId, edge.sourcePortName ) );
        const bool targetHasPort = std::any_of( target->inputPorts.cbegin(), target->inputPorts.cend(),
                                                [&]( const PortFact &p ) { return p.portName == edge.targetPortName; } );
        if ( !targetHasPort )
            return fail( QStringLiteral( "edge '%1': target node '%2' has no input port '%3'" )
                             .arg( edge.edgeId, edge.targetNodeId, edge.targetPortName ) );

        const auto portKey = qMakePair( edge.targetNodeId, edge.targetPortName );
        portInDegree[portKey]++;
        if ( portInDegree[portKey] > 1 )
            return fail( QStringLiteral( "edge '%1': input port '%2' of node '%3' already receives an edge (in-degree > 1)" )
                             .arg( edge.edgeId, edge.targetPortName, edge.targetNodeId ) );
    }
    return true;
}

Result<WorkflowDocument> WorkflowIR::migrateFromV1( const QJsonObject &v1Doc )
{
    if ( v1Doc.value( QLatin1String( "kind" ) ).toString() != QLatin1String( kV1Kind ) )
        return Result<WorkflowDocument>::error( QStringLiteral( "V1 migration: missing kind \"workflow_ir\"" ) );
    if ( v1Doc.value( QLatin1String( "schema_version" ) ).toString() != QLatin1String( kV1SchemaVersion ) )
        return Result<WorkflowDocument>::error( QStringLiteral( "V1 migration: unsupported schema_version" ) );

    WorkflowDocument def;
    def.version = QLatin1String( kVersionCurrent ); // migrations land on the current schema
    def.workflowId = v1Doc.value( QLatin1String( "ir_id" ) ).toString();
    def.name = v1Doc.value( QLatin1String( "goal" ) ).toString();
    def.description = QStringLiteral( "Migrated from WorkflowIR 1.0 (%1)" )
                          .arg( v1Doc.value( QLatin1String( "intent" ) ).toString() );
    QJsonObject metadata;
    metadata.insert( QLatin1String( "migratedFrom" ), QStringLiteral( "workflow_ir/1.0" ) );
    metadata.insert( QLatin1String( "v1Intent" ), v1Doc.value( QLatin1String( "intent" ) ).toString() );
    metadata.insert( QLatin1String( "v1Inputs" ), v1Doc.value( QLatin1String( "inputs" ) ).toArray() );
    metadata.insert( QLatin1String( "v1Outputs" ), v1Doc.value( QLatin1String( "outputs" ) ).toArray() );
    def.metadata = metadata;

    const QJsonArray v1Nodes = v1Doc.value( QLatin1String( "nodes" ) ).toArray();
    int index = 0;
    for ( const QJsonValue &nodeValue : v1Nodes )
    {
        const QJsonObject nodeObj = nodeValue.toObject();
        NodeFact node;
        node.nodeId = nodeObj.value( QLatin1String( "id" ) ).toString();
        node.operatorId = nodeObj.value( QLatin1String( "operator" ) ).toString();
        if ( node.nodeId.isEmpty() || node.operatorId.isEmpty() )
            return Result<WorkflowDocument>::error( QStringLiteral( "V1 migration: node missing id/operator" ) );
        node.displayName = node.operatorId;
        node.parameters = nodeObj.value( QLatin1String( "params" ) ).toObject();
        node.canvasPosition = QPointF( ( index % kGridColumns ) * kGridCellWidth,
                                       ( index / kGridColumns ) * kGridCellHeight );

        for ( const QJsonValue &outValue : nodeObj.value( QLatin1String( "outputs" ) ).toArray() )
        {
            const QJsonObject outObj = outValue.toObject();
            node.outputPorts.append(
                v1Port( outObj.value( QLatin1String( "name" ) ).toString(),
                        outObj.value( QLatin1String( "artifact" ) ).toObject() ) );
        }
        // Input ports derive from the wiring (as-name + the upstream port's
        // facts once resolved below).
        int asLessIndex = 0;
        for ( const QJsonValue &inValue : nodeObj.value( QLatin1String( "inputs" ) ).toArray() )
        {
            const QJsonObject inObj = inValue.toObject();
            PortFact in;
            in.portName = inObj.value( QLatin1String( "as" ) ).toString();
            if ( in.portName.isEmpty() )
            {
                // Multiple as-less inputs must not collide on one port name
                // (single-source invariant would reject the lift).
                in.portName = asLessIndex == 0 ? QStringLiteral( "input" )
                                               : QStringLiteral( "input_%1" ).arg( asLessIndex + 1 );
            }
            in.dataType = QStringLiteral( "Raster" );
            in.crs = QStringLiteral( "*" );
            in.radiometricState = QStringLiteral( "None" );
            in.isRequired = true;
            node.inputPorts.append( in );
            ++asLessIndex;
        }
        def.nodes.append( node );
        ++index;
    }

    // Second pass: propagate upstream output facts onto input ports, and
    // build the edge list. Wiring referencing ghost nodes fails closed.
    for ( const QJsonValue &nodeValue : v1Nodes )
    {
        const QJsonObject nodeObj = nodeValue.toObject();
        const QString nodeId = nodeObj.value( QLatin1String( "id" ) ).toString();
        NodeFact *node = nullptr;
        for ( NodeFact &candidate : def.nodes )
            if ( candidate.nodeId == nodeId )
                node = &candidate;

        int inputIndex = 0;
        for ( const QJsonValue &inValue : nodeObj.value( QLatin1String( "inputs" ) ).toArray() )
        {
            const QJsonObject inObj = inValue.toObject();
            const QString upstreamId = inObj.value( QLatin1String( "node" ) ).toString();
            const QString upstreamPort = inObj.value( QLatin1String( "output" ) ).toString();
            const NodeFact *upstream = def.findNode( upstreamId );
            if ( !upstream )
                return Result<WorkflowDocument>::error(
                    QStringLiteral( "V1 migration: node '%1' wires ghost upstream '%2'" ).arg( nodeId, upstreamId ) );

            QString resolvedState = QStringLiteral( "None" );
            QString resolvedCrs = QStringLiteral( "*" );
            double resolvedRes = 0.0;
            int resolvedBands = 0;
            for ( const PortFact &port : upstream->outputPorts )
                if ( port.portName == upstreamPort || upstream->outputPorts.size() == 1 )
                {
                    resolvedState = port.radiometricState;
                    resolvedCrs = port.crs;
                    resolvedRes = port.resolutionX;
                    resolvedBands = port.bandCount;
                    break;
                }

            PortFact &in = ( *node ).inputPorts[inputIndex];
            in.radiometricState = resolvedState;
            in.crs = resolvedCrs;
            in.resolutionX = resolvedRes;
            in.resolutionY = resolvedRes;
            in.bandCount = resolvedBands;

            EdgeFact edge;
            edge.edgeId = QStringLiteral( "mig_%1_%2_%3" ).arg( upstreamId, nodeId, in.portName );
            edge.sourceNodeId = upstreamId;
            edge.sourcePortName = upstreamPort.isEmpty() ? QStringLiteral( "output" ) : upstreamPort;
            edge.targetNodeId = nodeId;
            edge.targetPortName = in.portName;
            def.edges.append( edge );
            ++inputIndex;
        }
    }

    if ( !validateSemantics( def ) )
        return Result<WorkflowDocument>::error( QStringLiteral( "V1 migration produced an invalid document" ) );
    return Result<WorkflowDocument>::ok( def );
}

} // namespace sicnu::workflow
