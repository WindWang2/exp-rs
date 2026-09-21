/***************************************************************************
  scientific_state/asset_state_diff.h
  RS14-01 Scientific Data Passport — state diff API (sicnu.asset_state_diff.v1).

  Compares two canonical asset states and reports per-path differences.
  Flattening contract (see tests/test_scientific_state_diff.cpp): both states
  are serialized with assetStateToJson and walked in fixed key order into
  path → canonical-JSON-text maps — object members become dotted paths
  ("radiometric.unit"), arrays and scalars compare as their serialized text,
  and each claim record lives at the special path "claims[<claim path>]".

  Diff kinds:
    "changed"       — same path, different value text
    "added"         — only in @p after
    "removed"       — only in @p before
    "claim_changed" — the ClaimRecord at a claim path differs; before/after
                      carry the two claim kind texts
  Diffs are sorted by path; equal paths count into unchangedFields.
  Serialization is byte-deterministic (compact jsoncpp, sorted keys).
 ***************************************************************************/

#ifndef SICNU_SCIENTIFIC_STATE_ASSET_STATE_DIFF_H
#define SICNU_SCIENTIFIC_STATE_ASSET_STATE_DIFF_H

#include "scientific_state/asset_state_schema.h"
#include "scientific_state/asset_state_types.h"

#include <json/json.h>

#include <cstddef>
#include <string>
#include <vector>

namespace sicnu::state
{

/// One flattened-field difference.
struct FieldDiff
{
    std::string path;
    std::string kind;  ///< "changed" | "added" | "removed" | "claim_changed"
    std::string before;
    std::string after;

    bool operator==( const FieldDiff &other ) const
    {
        return path == other.path && kind == other.kind && before == other.before &&
               after == other.after;
    }
    bool operator!=( const FieldDiff &other ) const { return !( *this == other ); }
};

/// Result of comparing two asset states.
struct StateDiff
{
    std::string schemaId = kAssetStateDiffSchemaId;
    std::vector<FieldDiff> diffs;  ///< sorted by path
    std::size_t unchangedFields = 0;

    /// True when the two states flatten identically (no diff entries).
    bool empty() const { return diffs.empty(); }

    bool operator==( const StateDiff &other ) const
    {
        return schemaId == other.schemaId && diffs == other.diffs &&
               unchangedFields == other.unchangedFields;
    }
    bool operator!=( const StateDiff &other ) const { return !( *this == other ); }
};

/// Diffs @p before against @p after. Deterministic; never throws.
StateDiff diffStates( const RemoteSensingAssetState &before,
                      const RemoteSensingAssetState &after );

/// Serializes @p diff to the canonical JSON value (schema id included).
Json::Value stateDiffToJson( const StateDiff &diff );

/// Parses a canonical diff document. Fails with a typed error on schema
/// mismatch or invalid field types; never guesses.
bool stateDiffFromJson( const Json::Value &json, StateDiff &out, AssetStateError &error );

/// Byte-deterministic serialization of the canonical diff document.
std::string serializeStateDiff( const StateDiff &diff );

} // namespace sicnu::state

#endif // SICNU_SCIENTIFIC_STATE_ASSET_STATE_DIFF_H
