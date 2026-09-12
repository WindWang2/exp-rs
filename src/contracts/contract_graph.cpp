/***************************************************************************
 * contract_graph.cpp — contract inventory graph (M0)
 ***************************************************************************/
#include "contract_graph.h"

#include <algorithm>

namespace sicnu::contracts {

namespace {
const char *const kSchema = "exp.contract.graph.v1";

/// Endpoint node kinds per edge kind. Reference edges between commands
/// (lookups/CTAs/preflight actions) have command endpoints on both sides.
struct EndpointKinds
{
    std::string from;
    std::string to;
};

EndpointKinds endpointKinds( const std::string &edgeKind )
{
    if ( edgeKind == "command_help" )
        return { "command", "help_topic" };
    if ( edgeKind == "diagnostic_for" )
        return { "diagnostic", "error_code" };
    if ( edgeKind == "capability_for" )
        return { "capability_entry", "operator" };
    // surface_lookup | empty_state_cta | preflight_action
    return { "command", "command" };
}

} // namespace

void ContractGraph::addNode( ContractNode node )
{
    const auto key = std::make_pair( node.kind, node.id );
    if ( m_nodeKeys.count( key ) )
    {
        // First registration wins (like HelpRegistry), but the rejection is
        // remembered — computeFindings surfaces it as duplicate_node.
        m_duplicateKeys.insert( key.first + ":" + key.second );
        return;
    }
    m_nodeKeys.insert( key );
    m_nodes.push_back( std::move( node ) );
}

void ContractGraph::addEdge( ContractEdge edge )
{
    m_edges.push_back( std::move( edge ) );
}

bool ContractGraph::hasNode( const std::string &kind,
                             const std::string &id ) const
{
    return m_nodeKeys.count( std::make_pair( kind, id ) ) != 0;
}

std::vector<ContractFinding> ContractGraph::computeFindings() const
{
    std::vector<ContractFinding> findings;

    // Duplicate nodes: re-registrations rejected by addNode (first wins).
    for ( const auto &key : m_duplicateKeys )
        findings.push_back(
            { "duplicate_node", key, "registered more than once" } );

    // Dangling references: edge endpoints without a node.
    for ( const auto &e : m_edges )
    {
        const auto kinds = endpointKinds( e.kind );
        if ( !hasNode( kinds.from, e.from ) )
            findings.push_back(
                { "dangling_ref", e.kind + ":" + e.from,
                  "source " + kinds.from + " node missing (" + e.origin +
                      ")" } );
        if ( !hasNode( kinds.to, e.to ) )
            findings.push_back(
                { "dangling_ref", e.kind + ":" + e.to,
                  "target " + kinds.to + " node missing (" + e.origin +
                      ")" } );
    }

    std::sort( findings.begin(), findings.end(),
               []( const ContractFinding &a, const ContractFinding &b ) {
                   return std::tie( a.kind, a.id, a.detail ) <
                          std::tie( b.kind, b.id, b.detail );
               } );
    return findings;
}

Json::Value ContractGraph::toJson() const
{
    Json::Value root( Json::objectValue );
    root["schema"] = kSchema;

    Json::Value nodesJson( Json::arrayValue );
    auto sortedNodes = m_nodes;
    std::sort( sortedNodes.begin(), sortedNodes.end(),
               []( const ContractNode &a, const ContractNode &b ) {
                   return std::tie( a.kind, a.id ) < std::tie( b.kind, b.id );
               } );
    for ( const auto &n : sortedNodes )
    {
        Json::Value o( Json::objectValue );
        o["id"] = n.id;
        o["kind"] = n.kind;
        o["origin"] = n.origin;
        if ( !n.attributes.empty() )
        {
            Json::Value attrs( Json::arrayValue );
            for ( const auto &a : n.attributes )
                attrs.append( a );
            o["attributes"] = attrs;
        }
        nodesJson.append( o );
    }
    root["nodes"] = nodesJson;

    Json::Value edgesJson( Json::arrayValue );
    auto sortedEdges = m_edges;
    std::sort( sortedEdges.begin(), sortedEdges.end(),
               []( const ContractEdge &a, const ContractEdge &b ) {
                   // origin in the key: duplicate triples must still have a
                   // stable order for the byte-compare.
                   return std::tie( a.kind, a.from, a.to, a.origin ) <
                          std::tie( b.kind, b.from, b.to, b.origin );
               } );
    for ( const auto &e : sortedEdges )
    {
        Json::Value o( Json::objectValue );
        o["kind"] = e.kind;
        o["from"] = e.from;
        o["to"] = e.to;
        o["origin"] = e.origin;
        edgesJson.append( o );
    }
    root["edges"] = edgesJson;
    return root;
}

bool ContractGraph::fromJson( const Json::Value &json, ContractGraph &out,
                              std::string &error )
{
    out = ContractGraph{};
    if ( !json.isObject() || !json.isMember( "schema" ) ||
         !json["schema"].isString() ||
         json["schema"].asString() != kSchema )
    {
        error = "not an " + std::string( kSchema ) + " document";
        return false;
    }
    if ( !json.isMember( "nodes" ) || !json["nodes"].isArray() ||
         !json.isMember( "edges" ) || !json["edges"].isArray() )
    {
        error = "graph document needs nodes/edges arrays";
        return false;
    }
    for ( const auto &n : json["nodes"] )
    {
        ContractNode node;
        node.id = n.get( "id", "" ).asString();
        node.kind = n.get( "kind", "" ).asString();
        node.origin = n.get( "origin", "" ).asString();
        for ( const auto &a : n["attributes"] )
            node.attributes.push_back( a.asString() );
        out.addNode( std::move( node ) );
    }
    for ( const auto &e : json["edges"] )
    {
        ContractEdge edge;
        edge.kind = e.get( "kind", "" ).asString();
        edge.from = e.get( "from", "" ).asString();
        edge.to = e.get( "to", "" ).asString();
        edge.origin = e.get( "origin", "" ).asString();
        out.addEdge( std::move( edge ) );
    }
    return true;
}

} // namespace sicnu::contracts
