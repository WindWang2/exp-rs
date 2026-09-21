#pragma once

#include "processing/framework/algorithm_descriptor.h"

#include <optional>
#include <string>
#include <vector>

namespace sicnu::processing {

/// Authoritative query model for algorithm discovery (capability search
/// track). Every field maps to a declared descriptor fact — nothing is
/// inferred from prose. Shared by CLI `algorithms search` and MCP
/// `search_algorithms` so both surfaces return identical ids/order.
///
/// Semantics contract:
///  - AND across dimensions; ANY-of inside comma-separated lists;
///    AND across free-text tokens.
///  - All matching is case-folded (NFKD + non-spacing-mark strip +
///    toCaseFolded) — locale-free, accent-insensitive.
///  - Structured filters are exact matches on the declared value;
///    `purpose` alone is a substring filter (it is prose by contract).
///  - Empty query is legal: returns every filter-passing entry.
struct AlgorithmSearchQuery
{
  /// Free text: tokenized on non-[A-Za-z0-9_:] characters; each token must
  /// appear (substring) in the folded id/displayName/group/tag/purpose/
  /// description haystack. Max kMaxTextLength bytes.
  std::string text;
  /// Exact group (case-folded).
  std::string group;
  /// Tag list (comma-separated at the surface layer): ANY-of, exact.
  std::vector<std::string> tags;
  /// Substring filter on the declared agentMetadata.purpose.
  std::string purpose;
  /// Exact task family (agentMetadata.taskFamily), case-folded.
  std::string taskFamily;
  /// Modality list: ANY-of, exact, matched against the declared modality
  /// vocabulary of INPUT ports (rsContract modality / modalities[] /
  /// dataKind).
  std::vector<std::string> modalities;
  /// Exact input/output port data type (dataTypeToString), case-folded.
  std::string inputType;
  std::string outputType;
  /// When true, only agentMetadata.largeRasterSafe entries.
  bool largeRasterSafeOnly = false;

  /// Offset pagination. limit <= 0 means "default page size"; callers
  /// clamp to kMaxLimit. cursor < 0 is treated as 0.
  int limit = 0;
  int cursor = 0;

  static constexpr int kDefaultLimit = 50;
  static constexpr int kMaxLimit = 500;
  static constexpr size_t kMaxTextLength = 1024;
  static constexpr size_t kMaxFilterLength = 256;
  static constexpr size_t kMaxListValues = 16;

  /// Returns a refusal reason when the query exceeds the hard bounds
  /// (oversized text / filter values / list lengths). Empty = valid.
  std::string validationError() const;
};

/// One hit: index into the universe vector + the computed score.
struct AlgorithmSearchHit
{
  size_t index = 0;
  int score = 0;
};

struct AlgorithmSearchResult
{
  /// Page of hits (sorted: score desc, id asc — a total order).
  std::vector<AlgorithmSearchHit> hits;
  /// Total number of matching entries (before pagination).
  int total = 0;
  /// Echo of effective pagination.
  int limit = 0;
  int cursor = 0;
  /// Offset of the next page; -1 when this page reaches the end.
  int nextCursor = -1;
  /// Typed refusal when set (oversized/malformed query): hits empty.
  std::optional<std::string> error;
  /// Filter vocabulary present in the searched universe — surfaces expose
  /// it on zero hits so agents learn the honest filter space.
  struct Vocabulary
  {
    std::vector<std::string> groups;
    std::vector<std::string> tags;
    std::vector<std::string> taskFamilies;
    std::vector<std::string> modalities;
    std::vector<std::string> dataTypes;
  } vocabulary;
  /// Up to 5 ids closest to a query token by bounded edit distance over
  /// id segments (zero-hit aid only; empty when hits exist).
  std::vector<std::string> suggestions;
};

/// Runs the authoritative search over the injected universe. The caller
/// passes AtomicAlgorithmRegistry::instance().listDescriptors() — tests
/// inject mutated corpora to prove the filters (and only they) drive the
/// result set. Deterministic: total order (score desc, id asc), no locale,
/// no floating point.
AlgorithmSearchResult searchAlgorithms( const std::vector<AlgorithmDescriptor> &universe,
                                        const AlgorithmSearchQuery &query );

/// The modality vocabulary declared by one descriptor's input ports:
/// rsContract "modality", "modalities"[], and "dataKind" values.
std::vector<std::string> declaredModalities( const AlgorithmDescriptor &descriptor );

/// Splits a comma-separated surface value into trimmed non-empty items.
std::vector<std::string> splitSearchList( const std::string &csv );

} // namespace sicnu::processing
