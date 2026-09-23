/***************************************************************************
 * registry_operator_knowledge.h — live RSOperatorRegistry → OperatorFacts
 *
 * The adapters layer is the only place allowed to link the operator
 * registry / workflow runtime (see src/explain/CMakeLists.txt). This
 * adapter projects a real RSOperatorRegistry onto the explain layer's
 * IOperatorKnowledge interface: an operator instance is created from the
 * registry and its facts are read off schema() — parameter names, types,
 * descriptions, defaults, enums, ranges and required-ness are the live
 * surface verbatim.
 *
 * Schema drift gate: whenever the live schema surface deviates from the
 * shape the projection contract understands (properties not an object,
 * per-parameter entries that are not objects, missing/wrong-typed types,
 * malformed enum or range constraints), a typed drift record is kept and
 * the offending fragment is degraded honestly (skipped, or projected with
 * an empty field). Drift is reported, never silently normalized, and the
 * drift log is bounded. Facts the operator surface does not carry
 * (purpose, use cases, prerequisites, …) stay empty: absence is never
 * upgraded into invented content.
 ***************************************************************************/
#pragma once

#include "explain/explanation_sources.h"

#include <json/json.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace sicnu::operators
{
class RSOperatorRegistry;
}

namespace sicnu::explain::adapters
{

struct AdapterDriftRecord
{
  std::string operatorId;
  std::string code; // typed: schema_properties_not_object | schema_param_invalid |
                    // schema_type_missing | schema_enum_invalid | schema_range_invalid |
                    // schema_name_mismatch
  std::string field;
  std::string message;
};

class RegistryOperatorKnowledge final : public IOperatorKnowledge
{
public:
  // The drift log keeps this many most-recent records; further records are
  // counted in droppedDriftRecords() instead of retained.
  static constexpr size_t kMaxDriftRecords = 256;

  explicit RegistryOperatorKnowledge( sicnu::operators::RSOperatorRegistry &registry );

  // Resolves @p operatorId through the live registry. Absent operators
  // yield nullopt — callers fail closed on them.
  // Concurrency: instances are NOT thread-safe by design (the drift log is
  // mutated on lookup without a lock); use one instance per thread, as the
  // explain:step tool does.
  std::optional<OperatorFacts> findOperator( const std::string &operatorId ) const override;

  const std::vector<AdapterDriftRecord> &driftLog() const { return driftLog_; }
  size_t droppedDriftRecords() const { return droppedDriftRecords_; }

private:
  void recordDrift( const std::string &operatorId, const char *code, const std::string &field,
                    const std::string &message ) const;

  sicnu::operators::RSOperatorRegistry &registry_;
  mutable std::vector<AdapterDriftRecord> driftLog_;
  mutable size_t droppedDriftRecords_ = 0;
};

} // namespace sicnu::explain::adapters
