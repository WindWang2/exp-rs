/***************************************************************************
 * graph_assembly.h — build the contract graph from live sources (M0)
 *
 * Assembles the ContractGraph from the authoritative surfaces:
 *   - operators: RSOperatorRegistry (must be initialized by the caller —
 *     the call_once chain installs every family) + the parameter scanner;
 *   - commands: CommandRefScanner over the registration and consumer files;
 *   - help topics / diagnostics pages: data/help JSON (parsed with jsoncpp);
 *   - error codes: ErrorCodeScanner over the framework headers;
 *   - capability knowledge: data/agent/capabilities JSON.
 *
 * The returned graph is ready for findings computation and canonical
 * serialization (snapshot byte-compare). `assemblyNotes` reports bounded,
 * honest gaps (missing files, empty surfaces) instead of failing silently.
 ***************************************************************************/
#pragma once

#include "contract_graph.h"

#include <string>
#include <vector>

namespace sicnu::contracts {

struct AssemblyResult
{
    ContractGraph graph;
    std::vector<std::string> notes; // non-fatal assembly facts
};

/// @param sourceRoot repository root (reads src/** and data/**).
/// The caller must have initialized RSOperatorRegistry beforehand.
AssemblyResult buildLiveGraph( const std::string &sourceRoot );

} // namespace sicnu::contracts
