/***************************************************************************
 * contract_graph.h — contract inventory graph (M0)
 *
 * Nodes and reference edges across the authoritative contract surfaces,
 * with structural findings (duplicate ids, dangling references). The graph
 * is a plain data structure: assembly from the live registries/scanners
 * lives in graph_assembly.cpp so this stays dependency-free and stable to
 * serialize.
 *
 * Canonical serialization: schema "exp.contract.graph.v1", members sorted,
 * deterministic — the committed snapshot data/contracts/contract_graph.snap.json
 * byte-compares against a fresh generation (any drift forces a conscious
 * contract update).
 ***************************************************************************/
#pragma once

#include <json/json.h>

#include <map>
#include <set>
#include <string>
#include <vector>

namespace sicnu::contracts {

struct ContractNode
{
    std::string id;
    std::string kind; // operator | command | help_topic | diagnostic |
                      // error_code | capability_entry
    std::string origin; // evidence file (source or data path)
    // Optional attributes (e.g. declared params of an operator).
    std::vector<std::string> attributes;
};

struct ContractEdge
{
    std::string kind; // command_help | surface_lookup | empty_state_cta |
                      // preflight_action | diagnostic_for |
                      // capability_for | tool_for
    std::string from;
    std::string to;
    std::string origin;
};

struct ContractFinding
{
    std::string kind;     // duplicate_node | dangling_ref
    std::string id;
    std::string detail;
};

class ContractGraph
{
  public:
    void addNode( ContractNode node );
    void addEdge( ContractEdge edge );

    const std::vector<ContractNode> &nodes() const { return m_nodes; }
    const std::vector<ContractEdge> &edges() const { return m_edges; }
    std::vector<ContractNode> &nodesMutable() { return m_nodes; }
    std::vector<ContractEdge> &edgesMutable() { return m_edges; }

    /// Structural findings, stable order.
    std::vector<ContractFinding> computeFindings() const;

    bool hasNode( const std::string &kind, const std::string &id ) const;

    /// Deterministic canonical JSON (exp.contract.graph.v1).
    Json::Value toJson() const;

    /// Parses a graph document; returns false on schema/version mismatch.
    static bool fromJson( const Json::Value &json, ContractGraph &out,
                          std::string &error );

  private:
    std::vector<ContractNode> m_nodes;
    std::vector<ContractEdge> m_edges;
    std::set<std::pair<std::string, std::string>> m_nodeKeys; // (kind,id)
};

} // namespace sicnu::contracts
