/***************************************************************************
  digest.h — the single hashing seam of the verification core (Slice A)

  The repo already has exactly one SHA-256 implementation
  (src/geospatial/util/sha256.h) and policy requires there be exactly one. So
  this header deliberately contains NO hash code; it only decides WHAT gets
  hashed.

  That decision is the whole point of the file. Hashing jsoncpp's default
  writer output would hash member insertion order and platform float
  formatting, so two runs over logically identical facts would diverge. Every
  digest here therefore goes through canonicalJson() first, which sorts members
  and pins floats to 12 significant digits. That is what makes a digest usable
  as identity rather than as decoration.
 ***************************************************************************/
#pragma once

#include <json/json.h>

#include <string>

namespace sicnu::verification
{

/// Lowercase hex sha256 (64 chars) over the canonical form of @p value.
///
/// On failure returns "" and fills @p error — including for non-finite
/// numbers, whose text form would otherwise poison any comparison. An empty
/// digest is never a valid identity; callers must surface it, not default it.
std::string canonicalDigestSha256( const Json::Value &value, std::string &error );

/// Lowercase hex sha256 over @p text, for values already in canonical form.
std::string digestSha256Hex( const std::string &text );

} // namespace sicnu::verification
