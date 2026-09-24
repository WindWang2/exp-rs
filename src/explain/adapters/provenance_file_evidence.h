/***************************************************************************
 * provenance_file_evidence.h — provenance_<runId>.json → StepEvidence
 *
 * Projects the per-run provenance records written by PipelineRunCoordinator
 * (envelope kind "d17_provenance" v1.0, parsed by the real ProvenanceGraph
 * reader) onto the explain layer's IExecutionEvidence interface.
 *
 * Only what the record actually carries is projected: nodeExec state,
 * elapsedMs, isCacheHit, errorMessage and the produced/reused artifact
 * path + fingerprint. The record has no wall-clock start/end stamps, so
 * startedUtc/endedUtc stay empty — absence is kept as absence, never
 * synthesized. Every evidence set cites its record through the pinned
 * machine-kind link grammar (provenance:<runId>#node:<nodeId>), so the
 * builder's ungrounded-evidence refusal cannot fire on adapter output.
 *
 * Loading is fail-closed: unreadable, oversized, unparseable or
 * envelope-refused files are recorded as typed problems and skipped; a
 * missing directory is a problem, not an empty success. Unknown runs or
 * steps answer nullopt — unavailable stays unavailable.
 ***************************************************************************/
#pragma once

#include "explain/explanation_sources.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sicnu::workflow
{
class ProvenanceGraph;
}

namespace sicnu::explain::adapters
{

struct EvidenceLoadProblem
{
  std::string file;
  std::string code; // typed: directory_missing | malformed_name | file_unreadable |
                    // file_too_large | parse_failed | envelope_refused | duplicate_run |
                    // run_limit_exceeded
  std::string message;
};

class ProvenanceFileEvidence final : public IExecutionEvidence
{
public:
  // Loads every provenance_<runId>.json directly inside @p directory
  // (sorted, bounded). Never throws: bad files are recorded in the load
  // problems and skipped; the returned adapter is always non-null
  // (possibly empty).
  static std::unique_ptr<ProvenanceFileEvidence> loadFromDirectory(
    const std::string &directory, std::vector<EvidenceLoadProblem> &problems );

  // Run/step lookup over the loaded records. Unknown run or step → nullopt.
  std::optional<StepEvidence> evidenceFor( const std::string &runId,
                                           const std::string &stepId ) const override;

  size_t runCount() const { return runs_.size(); }
  const std::vector<EvidenceLoadProblem> &loadProblems() const { return problems_; }

  static constexpr size_t kMaxRuns = 256;
  static constexpr size_t kMaxFileBytes = 64u * 1024u * 1024u;

private:
  struct RunRecord
  {
    std::string runId;
    std::shared_ptr<const sicnu::workflow::ProvenanceGraph> graph;
  };

  std::vector<RunRecord> runs_;
  std::vector<EvidenceLoadProblem> problems_;
};

} // namespace sicnu::explain::adapters
