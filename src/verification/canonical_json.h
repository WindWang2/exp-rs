/***************************************************************************
  canonical_json.h — deterministic JSON serialization for digests (Slice A)

  A verification report is only reproducible if two runs over the same facts
  produce byte-identical text, because that text is what gets hashed. Three
  things break that, and each is handled here:

    1. member order        -> members are written sorted by name;
    2. floating point      -> doubles are pinned to 12 significant digits
                              (0.1 + 0.2 and 0.3 serialize identically);
    3. non-finite numbers  -> refused outright. Writing "nan" or "inf" into
                              something that will be hashed is how two "equal"
                              runs get different digests.

  Depth and size are bounded for a second reason than tidiness: the repo has
  already been hit twice by jsoncpp default-reader recursion bombs (#1154,
  #1155). This module never parses text — callers hand it an already-parsed
  Json::Value — but it must still not recurse unboundedly over a document some
  other layer handed it. Exceeding a limit is a typed refusal, never a crash
  and never an infinite loop.
 ***************************************************************************/
#pragma once

#include <json/json.h>

#include <cstddef>
#include <string>

namespace sicnu::verification
{

/// Bounds for canonicalization. Every default has a test that trips it.
struct CanonicalLimits
{
    int maxDepth = 32;                            ///< nesting depth of object/array
    std::size_t maxMembers = 10000;               ///< members per object level
    std::size_t maxElements = 50000;              ///< total nodes serialized
    std::size_t maxStringChars = 4096;            ///< per key and per string value
    std::size_t maxOutputBytes = 4u * 1024u * 1024u;
};

/// Serializes @p value into @p out deterministically.
///
/// @returns false and fills @p error when a limit is exceeded or the document
///          carries something that cannot be canonicalized. @p out is left
///          untouched on failure.
bool canonicalJson( const Json::Value &value, std::string &out, std::string &error,
                    const CanonicalLimits &limits = CanonicalLimits{} );

/// Rounds to @p digits SIGNIFICANT digits and formats deterministically
/// (classic locale, shortest stable form). Used for every number that reaches
/// an evidence record or a digest, following the grading-preview discipline in
/// src/agent/output_verifier.h that rounds to 12 significant digits.
std::string roundSignificant( double value, int digits = 12 );

} // namespace sicnu::verification
