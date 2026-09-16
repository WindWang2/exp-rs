/***************************************************************************
  geospatial/remote/vsi_object_identity.cpp — network VSI object identity.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "geospatial/remote/vsi_object_identity.h"

#include "geospatial/remote/offline_gate.h"
#include "geospatial/util/resource_uri.h"

#include <cpl_string.h>
#include <cpl_vsi.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace sicnu::geo
{

namespace
{

std::string lowerAscii( std::string text )
{
  std::transform( text.begin(), text.end(), text.begin(),
                  [] ( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
  return text;
}

} // namespace

VsiObjectIdentityFacts probeVsiObjectIdentity( const std::string &vsiPath )
{
  VsiObjectIdentityFacts facts;
  // The offline gate is checked HERE (not only at GDAL open): a forced-
  // offline process must never dispatch a HEAD-shaped probe, and the typed
  // refusal carries the same text every other remote path reports.
  if ( offline::enabled() && offline::isRemoteTarget( vsiPath ) )
  {
    facts.errorText = offline::refusalMessage( vsiPath );
    return facts;
  }
  char **headers = VSIGetFileMetadata( vsiPath.c_str(), "HEADERS", nullptr );
  if ( headers == nullptr )
  {
    facts.errorText = "vsi object answered no metadata (offline or refused)";
    return facts;
  }
  facts.probed = true;
  std::string rawEtag;
  for ( char **it = headers; *it != nullptr; ++it )
  {
    const std::string entry = *it;
    const std::size_t eq = entry.find( '=' );
    if ( eq == std::string::npos )
      continue;
    const std::string name = lowerAscii( entry.substr( 0, eq ) );
    const std::string value = entry.substr( eq + 1 );
    if ( name == "etag" )
      rawEtag = value;
    else if ( name == "content-length" )
    {
      try
      {
        facts.sizeBytes = static_cast<std::uintmax_t>( std::stoull( value ) );
        facts.hasSize = true;
      }
      catch ( const std::exception & )
      {
        facts.hasSize = false;
      }
    }
  }
  CSLDestroy( headers );
  // Fail-closed: only a STRONG ETag proves byte-level freshness. "null"
  // (S3 multipart placeholder) and weak "W/" validators are unprovable.
  if ( rawEtag.empty() || rawEtag == "null" || rawEtag.rfind( "W/", 0 ) == 0 )
    return facts;
  facts.etag = rawEtag;
  return facts;
}

} // namespace sicnu::geo
