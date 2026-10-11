// digest.cpp — see digest.h for why hashing goes through canonicalJson first.

#include "verification/digest.h"

#include "verification/canonical_json.h"

// The repo-wide single SHA-256 implementation. Borrowed rather than
// reimplemented: a second hash implementation is a second thing to audit and a
// second place for the two to disagree.
#include "geospatial/util/sha256.h"

namespace sicnu::verification
{

std::string digestSha256Hex( const std::string &text )
{
    return sicnu::geo::sha256Hex( text );
}

std::string canonicalDigestSha256( const Json::Value &value, std::string &error )
{
    error.clear();
    std::string canonical;
    if ( !canonicalJson( value, canonical, error ) )
    {
        // Refusing is the only safe answer: hashing something that could not be
        // canonicalized would produce a digest that drifts between runs.
        return std::string();
    }
    return digestSha256Hex( canonical );
}

} // namespace sicnu::verification
