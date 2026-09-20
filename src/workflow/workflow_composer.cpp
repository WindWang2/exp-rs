// src/workflow/workflow_composer.cpp — subflow fragment expansion
#include "workflow/workflow_composer.h"

#include "workflow/workflow_dag_analyzer.h"

#include <QJsonArray>
#include <QSet>

namespace sicnu::workflow {
namespace {

constexpr const char *kFragmentKey = "fragment";
constexpr const char *kInterfaceKey = "interface";
constexpr const char *kBindingsKey = "bindings";
constexpr const char *kInputsKey = "inputs";
constexpr const char *kOutputsKey = "outputs";

struct PortTarget
{
    QString nodeId;
    QString portName;
};

QString subflowError( const QString &nodeId, const QString &detail )
{
    return QStringLiteral( "ir2.subflow: node '%1': %2" ).arg( nodeId, detail );
}

Result<WorkflowDocument> expandSubflowsImpl( const WorkflowDocument &authored, int depth );

/// Parses interface["inputs"|"outputs"] into port -> {node, port}. Every
/// entry must be an object carrying non-empty "node"/"port" strings.
Result<QHash<QString, PortTarget>> parseInterfaceSide( const QJsonObject &interfaceObj,
                                                       const QString &key,
                                                       const QString &instanceId )
{
    QHash<QString, PortTarget> map;
    const QJsonValue side = interfaceObj.value( key );
    if ( side.isUndefined() )
        return Result<QHash<QString, PortTarget>>::ok( map );
    if ( !side.isObject() )
        return Result<QHash<QString, PortTarget>>::error(
            subflowError( instanceId, QStringLiteral( "interface.%1 must be an object" ).arg( key ) ) );

    const QJsonObject sideObj = side.toObject();
    for ( auto it = sideObj.begin(); it != sideObj.end(); ++it )
    {
        const QJsonObject target = it.value().toObject();
        const QString node = target.value( QLatin1String( "node" ) ).toString();
        const QString port = target.value( QLatin1String( "port" ) ).toString();
        if ( node.isEmpty() || port.isEmpty() )
            return Result<QHash<QString, PortTarget>>::error(
                subflowError( instanceId,
                              QStringLiteral( "interface.%1['%2'] needs non-empty 'node' and 'port'" )
                                  .arg( key, it.key() ) ) );
        map.insert( it.key(), PortTarget{ node, port } );
    }
    return Result<QHash<QString, PortTarget>>::ok( map );
}

bool hasPort( const QVector<PortFact> &ports, const QString &name )
{
    for ( const PortFact &p : ports )
        if ( p.portName == name )
            return true;
    return false;
}

/// Applies bindings { "<fragNodeId>": { "<param>": value, ... } }: each
/// bound object is merged into that fragment node's parameters (bound keys
/// overwrite). Unknown node ids and non-object values fail closed.
Result<bool> applyBindings( WorkflowDocument &fragment,
                            const QJsonObject &bindings,
                            const QString &instanceId )
{
    for ( auto it = bindings.begin(); it != bindings.end(); ++it )
    {
        if ( !it.value().isObject() )
            return Result<bool>::error(
                subflowError( instanceId,
                              QStringLiteral( "bindings['%1'] must be an object of parameter overrides" )
                                  .arg( it.key() ) ) );
        NodeFact *node = nullptr;
        for ( NodeFact &candidate : fragment.nodes )
            if ( candidate.nodeId == it.key() )
                node = &candidate;
        if ( !node )
            return Result<bool>::error(
                subflowError( instanceId,
                              QStringLiteral( "bindings reference unknown fragment node '%1'" ).arg( it.key() ) ) );
        const QJsonObject overrides = it.value().toObject();
        for ( auto p = overrides.begin(); p != overrides.end(); ++p )
            node->parameters.insert( p.key(), p.value() );
    }
    return Result<bool>::ok( true );
}

/// Expands one instance node inside @p working. On success the instance is
/// replaced by the (already recursively expanded, namespaced) fragment
/// nodes and every edge that touched the instance is rewired through the
/// interface map.
Result<bool> expandInstance( WorkflowDocument &working, const QString &instanceId, int depth )
{
    const NodeFact *instance = working.findNode( instanceId );
    if ( !instance )
        return Result<bool>::error( subflowError( instanceId, QStringLiteral( "instance vanished during expansion" ) ) );

    const QJsonObject params = instance->parameters;

    // --- fragment payload -------------------------------------------------
    const QJsonValue fragmentVal = params.value( QLatin1String( kFragmentKey ) );
    if ( fragmentVal.isString() )
        return Result<bool>::error(
            subflowError( instanceId,
                          QStringLiteral( "fragment file/path references are not supported; embed the document" ) ) );
    if ( !fragmentVal.isObject() )
        return Result<bool>::error(
            subflowError( instanceId, QStringLiteral( "missing or non-object 'fragment' parameter" ) ) );

    Result<WorkflowDocument> parsed = WorkflowIR::fromJson( fragmentVal.toObject() );
    if ( !parsed.isSuccess() )
        return Result<bool>::error( subflowError( instanceId, QStringLiteral( "fragment parse: %1" ).arg( parsed.error() ) ) );
    WorkflowDocument fragment = parsed.value();

    // --- bindings ---------------------------------------------------------
    const QJsonValue bindingsVal = params.value( QLatin1String( kBindingsKey ) );
    if ( !bindingsVal.isUndefined() )
    {
        if ( !bindingsVal.isObject() )
            return Result<bool>::error( subflowError( instanceId, QStringLiteral( "'bindings' must be an object" ) ) );
        const Result<bool> bound = applyBindings( fragment, bindingsVal.toObject(), instanceId );
        if ( !bound.isSuccess() )
            return bound;
    }

    // --- recursive expansion (nested subflows) ----------------------------
    if ( WorkflowComposer::hasSubflowNodes( fragment ) )
    {
        Result<WorkflowDocument> nested = expandSubflowsImpl( fragment, depth + 1 );
        if ( !nested.isSuccess() )
            return Result<bool>::error( subflowError( instanceId, nested.error() ) );
        fragment = nested.value();
    }

    // --- fragment must be semantically valid before inlining --------------
    QString semanticError;
    if ( !WorkflowIR::validateSemantics( fragment, &semanticError ) )
        return Result<bool>::error(
            subflowError( instanceId, QStringLiteral( "fragment invalid: %1" ).arg( semanticError ) ) );

    // --- interface --------------------------------------------------------
    const QJsonValue interfaceVal = params.value( QLatin1String( kInterfaceKey ) );
    QJsonObject interfaceObj;
    if ( !interfaceVal.isUndefined() )
    {
        if ( !interfaceVal.isObject() )
            return Result<bool>::error( subflowError( instanceId, QStringLiteral( "'interface' must be an object" ) ) );
        interfaceObj = interfaceVal.toObject();
    }
    const Result<QHash<QString, PortTarget>> inMap = parseInterfaceSide( interfaceObj, QLatin1String( kInputsKey ), instanceId );
    if ( !inMap.isSuccess() )
        return Result<bool>::error( inMap.error() );
    const Result<QHash<QString, PortTarget>> outMap = parseInterfaceSide( interfaceObj, QLatin1String( kOutputsKey ), instanceId );
    if ( !outMap.isSuccess() )
        return Result<bool>::error( outMap.error() );

    // Every interface key must name a port the instance actually declares,
    // and every mapped target must be an existing fragment port of the
    // matching direction.
    for ( auto it = inMap.value().cbegin(); it != inMap.value().cend(); ++it )
    {
        if ( !hasPort( instance->inputPorts, it.key() ) )
            return Result<bool>::error(
                subflowError( instanceId,
                              QStringLiteral( "interface.inputs maps undeclared instance port '%1'" ).arg( it.key() ) ) );
        const NodeFact *target = fragment.findNode( it.value().nodeId );
        if ( !target || !hasPort( target->inputPorts, it.value().portName ) )
            return Result<bool>::error(
                subflowError( instanceId,
                              QStringLiteral( "interface.inputs['%1'] targets missing input port %2.%3" )
                                  .arg( it.key(), it.value().nodeId, it.value().portName ) ) );
    }
    for ( auto it = outMap.value().cbegin(); it != outMap.value().cend(); ++it )
    {
        if ( !hasPort( instance->outputPorts, it.key() ) )
            return Result<bool>::error(
                subflowError( instanceId,
                              QStringLiteral( "interface.outputs maps undeclared instance port '%1'" ).arg( it.key() ) ) );
        const NodeFact *target = fragment.findNode( it.value().nodeId );
        if ( !target || !hasPort( target->outputPorts, it.value().portName ) )
            return Result<bool>::error(
                subflowError( instanceId,
                              QStringLiteral( "interface.outputs['%1'] targets missing output port %2.%3" )
                                  .arg( it.key(), it.value().nodeId, it.value().portName ) ) );
    }

    // --- inline nodes -----------------------------------------------------
    const QString prefix = instanceId + QStringLiteral( "__" );
    QSet<QString> existing;
    for ( const NodeFact &n : working.nodes )
        existing.insert( n.nodeId );

    QVector<NodeFact> inlined;
    inlined.reserve( fragment.nodes.size() );
    for ( NodeFact node : fragment.nodes )
    {
        node.nodeId = prefix + node.nodeId;
        node.originNodeId = instanceId; // designer-level attribution (schema 2.1)
        if ( existing.contains( node.nodeId ) )
            return Result<bool>::error(
                subflowError( instanceId,
                              QStringLiteral( "expanded node id '%1' collides with an existing node" ).arg( node.nodeId ) ) );
        inlined.append( node );
    }

    QVector<NodeFact> newNodes;
    newNodes.reserve( working.nodes.size() + inlined.size() );
    for ( const NodeFact &n : working.nodes )
    {
        if ( n.nodeId == instanceId )
            newNodes.append( inlined );
        else
            newNodes.append( n );
    }
    working.nodes = newNodes;

    // --- edges -------------------------------------------------------------
    QVector<EdgeFact> newEdges;
    newEdges.reserve( working.edges.size() + fragment.edges.size() );
    for ( EdgeFact e : working.edges )
    {
        if ( e.targetNodeId == instanceId )
        {
            const auto target = inMap.value().constFind( e.targetPortName );
            if ( target == inMap.value().cend() )
                return Result<bool>::error(
                    subflowError( instanceId,
                                  QStringLiteral( "edge '%1' lands on instance input '%2' with no interface.inputs mapping" )
                                      .arg( e.edgeId, e.targetPortName ) ) );
            e.targetNodeId = prefix + target->nodeId;
            e.targetPortName = target->portName;
        }
        if ( e.sourceNodeId == instanceId )
        {
            const auto target = outMap.value().constFind( e.sourcePortName );
            if ( target == outMap.value().cend() )
                return Result<bool>::error(
                    subflowError( instanceId,
                                  QStringLiteral( "edge '%1' leaves instance output '%2' with no interface.outputs mapping" )
                                      .arg( e.edgeId, e.sourcePortName ) ) );
            e.sourceNodeId = prefix + target->nodeId;
            e.sourcePortName = target->portName;
        }
        newEdges.append( e );
    }
    for ( EdgeFact e : fragment.edges )
    {
        e.edgeId = prefix + e.edgeId;
        e.sourceNodeId = prefix + e.sourceNodeId;
        e.targetNodeId = prefix + e.targetNodeId;
        newEdges.append( e );
    }
    working.edges = newEdges;
    return Result<bool>::ok( true );
}

Result<WorkflowDocument> expandSubflowsImpl( const WorkflowDocument &authored, int depth )
{
    if ( depth > WorkflowComposer::kMaxSubflowDepth )
        return Result<WorkflowDocument>::error(
            QStringLiteral( "ir2.subflow: expansion depth exceeds %1 (runaway nesting?)" )
                .arg( WorkflowComposer::kMaxSubflowDepth ) );

    WorkflowDocument working = authored;
    for ( const NodeFact &node : authored.nodes )
    {
        if ( node.operatorId != QLatin1String( WorkflowComposer::kSubflowOperatorId ) )
            continue;
        const Result<bool> expanded = expandInstance( working, node.nodeId, depth );
        if ( !expanded.isSuccess() )
            return Result<WorkflowDocument>::error( expanded.error() );
    }

    // Expansion produced origin annotations -> the document exercises 2.1
    // fields, so it must claim a version that defines them.
    if ( working.version == QLatin1String( "2.0" ) )
    {
        for ( const NodeFact &node : working.nodes )
            if ( !node.originNodeId.isEmpty() )
            {
                working.version = WorkflowIR::currentSchemaVersion();
                break;
            }
    }

    // The merged document must stand on its own: rewired boundary edges can
    // introduce a cycle through the instance, blow an input port's in-degree
    // (two instance ports mapped to one fragment port), or collide on a
    // namespaced edgeId. The public composer contract is fail-closed — do
    // not rely on the sole caller revalidating.
    QString mergedError;
    if ( !WorkflowIR::validateSemantics( working, &mergedError ) )
        return Result<WorkflowDocument>::error(
            QStringLiteral( "ir2.subflow: expanded document is not semantically valid: %1" )
                .arg( mergedError ) );
    const DagAnalysisResult dag = WorkflowDagAnalyzer::analyzeDag( working );
    if ( !dag.isAcyclic )
        return Result<WorkflowDocument>::error(
            QStringLiteral( "ir2.subflow: expanded document is not acyclic: %1" )
                .arg( dag.errorMessage ) );
    return Result<WorkflowDocument>::ok( working );
}

} // namespace

bool WorkflowComposer::hasSubflowNodes( const WorkflowDocument &def )
{
    for ( const NodeFact &node : def.nodes )
        if ( node.operatorId == QLatin1String( kSubflowOperatorId ) )
            return true;
    return false;
}

Result<WorkflowDocument> WorkflowComposer::expandSubflows( const WorkflowDocument &authored )
{
    return expandSubflowsImpl( authored, 0 );
}

} // namespace sicnu::workflow
