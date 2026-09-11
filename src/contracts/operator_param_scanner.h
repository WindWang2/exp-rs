/***************************************************************************
 * operator_param_scanner.h — implementation-vs-schema parameter extraction
 *
 * Contract Platform 9.0 (M2). For every REGISTER_RS_OPERATOR site this
 * scanner mechanically extracts, from source:
 *
 *   - declaredParams : parameter names declared in the class's schema()
 *                      body (the container passed to makeRootSchema, plus
 *                      root["properties"]["name"] additions);
 *   - readParams     : parameter keys the implementation actually reads in
 *                      run() and same-file helpers reachable from run()
 *                      (direct indexing, isMember/get, the shared
 *                      sicnu::operators::params helpers, parseBands);
 *   - unresolved     : constructs the scanner refuses to guess about; the
 *                      contract test fails loudly on any entry so a silent
 *                      pass is impossible.
 *
 * Comparison (done by the test, not here):
 *   read - declared = undeclared_read  (the #872/#879/#880 drift class)
 *   declared - read = dead_schema_param
 *
 * Plain C++ + std::filesystem + std::regex, no Qt: usable headless and
 * portable. Bounded by design: one pass per file, depth-limited helper
 * reachability, per-file size cap.
 ***************************************************************************/
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

namespace sicnu::contracts {

struct OperatorScanResult
{
    std::string operatorId;
    std::string className;
    std::string file; // path as passed to the scanner (relative to root)
    std::set<std::string> declaredParams;
    std::set<std::string> readParams;
    std::vector<std::string> unresolved; // reasons; non-empty ⇒ must fail
    bool schemaFound = false;
    bool runFound = false;

    std::set<std::string> undeclaredReads() const;
    std::set<std::string> deadSchemaParams() const;
};

class OperatorParamScanner
{
  public:
    /// @param sourceRoot repository root; scans <root>/src/operators/**.cpp
    explicit OperatorParamScanner( std::string sourceRoot,
                                   size_t maxFileBytes = 2u * 1024u * 1024u );

    /// Scans every operator registration found under the operators tree.
    /// Registration and implementation may live in different files (the
    /// *_init.cpp pattern): method bodies are located across all scanned
    /// files. Files larger than maxFileBytes produce an `unresolved` entry
    /// instead of being skipped silently.
    std::vector<OperatorScanResult> scanAll() const;

    /// Scans one in-memory source buffer (exposed for unit/mutation tests).
    std::vector<OperatorScanResult> scanSource( std::string_view src,
                                                const std::string &fileName ) const;

  private:
    std::string m_sourceRoot;
    size_t m_maxFileBytes;
};

/// Directory walk helper: all .cpp files under `dir`, sorted (stable output).
std::vector<std::string> collectCppFiles( const std::string &dir );

} // namespace sicnu::contracts
