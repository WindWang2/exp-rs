/***************************************************************************
  scientific_state/asset_state_json.h
  RS14-01 Scientific Data Passport — deterministic JSON serialization.

  Canonical document: schema "sicnu.asset_state.v1". Serialization is
  byte-deterministic: jsoncpp objects serialize in lexicographic key order
  and every array is explicitly normalized (sorted) before writing, so the
  same state always produces the same bytes on the same build.
 ***************************************************************************/

#ifndef SICNU_SCIENTIFIC_STATE_ASSET_STATE_JSON_H
#define SICNU_SCIENTIFIC_STATE_ASSET_STATE_JSON_H

#include "scientific_state/asset_state_types.h"

#include <json/json.h>

#include <string>

namespace sicnu::state
{

/// Serializes @p state to the canonical JSON value (schema id included).
Json::Value assetStateToJson( const RemoteSensingAssetState &state );

/// Parses a canonical document. Fails with a typed error on schema mismatch,
/// malformed JSON text, or invalid field types; never guesses.
bool assetStateFromJson( const Json::Value &json, RemoteSensingAssetState &out,
                         AssetStateError &error );

/// Convenience overload for raw JSON text (stackLimit-hardened parsing).
bool assetStateFromJson( const std::string &text, RemoteSensingAssetState &out,
                         AssetStateError &error );

/// Byte-deterministic serialization of the canonical document.
std::string serializeState( const RemoteSensingAssetState &state );

/// Normalizes @p state in place for canonical form: sorts bands by index,
/// claims by path, notes by (code, path, detail), input refs by
/// (assetId, revision), temporal refs by collectionId; sorts and dedups
/// label/assumption/unknown/source/alternative arrays. Called by
/// assetStateToJson before writing.
void normalizeState( RemoteSensingAssetState &state );

} // namespace sicnu::state

#endif // SICNU_SCIENTIFIC_STATE_ASSET_STATE_JSON_H
