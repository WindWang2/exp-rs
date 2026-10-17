/***************************************************************************
 * geospatial/remote/process_network_policy.cpp — see the header.
 ***************************************************************************/
#include "geospatial/remote/process_network_policy.h"

#include "geospatial/remote/offline_gate.h"
#include "geospatial/remote/range_cache.h"
#include "geospatial/util/gdal_compat.h"

#include <cpl_conv.h>
#include <cpl_error.h>
#include <cpl_http.h>
#include <cpl_string.h>
#include <cpl_vsi.h>
#include <cpl_vsi_virtual.h>

#include <cerrno>
#include <cstring>
#include <mutex>
#include <string>

namespace sicnu::geo::network_policy
{
namespace
{

#if SICNU_GDAL_VSI_OPEN_RETURNS_UNIQUE_PTR
using OpenResult = VSIVirtualHandleUniquePtr;
#else
using OpenResult = VSIVirtualHandle *;
#endif

enum class Mode
{
  RerouteThroughRangeCache, ///< /vsicurl/, /vsicurl_streaming/
  RefuseAll,                ///< /vsicurl? option form
  HostCheckOnly,            ///< /vsiwebhdfs/
};

bool isHttpUrl( const std::string &url )
{
  return url.rfind( "http://", 0 ) == 0 || url.rfind( "https://", 0 ) == 0 ||
         url.rfind( "HTTP://", 0 ) == 0 || url.rfind( "HTTPS://", 0 ) == 0;
}

std::mutex g_cacheMutex;

/// The guarded range cache must exist for a re-routed read. An already
/// installed cache is kept (its config is forced guarded by the policy).
void ensureGuardedRangeCache()
{
  std::lock_guard<std::mutex> lock( g_cacheMutex );
  if ( RemoteRangeCache::installed() )
    return;
  RangeCacheConfig config;
  config.blockPrivateNetworks = true;
  RemoteRangeCache::install( config );
}

class PolicyHandler final : public VSIFilesystemHandler
{
  public:
    PolicyHandler( std::string prefix, VSIFilesystemHandler *original, Mode mode )
      : mPrefix( std::move( prefix ) ), mOriginal( original ), mMode( mode )
    {
    }

    OpenResult Open( const char *pszFilename, const char *pszAccess, bool bSetError,
                     CSLConstList papszOptions ) override
    {
      if ( !offline::blockPrivateNetworks() )
        return mOriginal->Open( pszFilename, pszAccess, bSetError, papszOptions );
      std::string target;
      if ( !admit( pszFilename, target ) )
      {
        CPLError( CE_Failure, CPLE_AppDefined,
                  "%s: refused by the private-network policy (use --allow-private-network to opt out)",
                  mPrefix.c_str() );
        errno = EACCES;
        return OpenResult{};
      }
      if ( mMode == Mode::HostCheckOnly )
        return mOriginal->Open( pszFilename, pszAccess, bSetError, papszOptions );
      ensureGuardedRangeCache();
      const std::string cached = RemoteRangeCache::cachedPath( target );
      VSIFilesystemHandler *rangeCache = VSIFileManager::GetHandler( cached.c_str() );
      if ( rangeCache == nullptr || rangeCache == this )
        return OpenResult{};
      return rangeCache->Open( cached.c_str(), pszAccess, bSetError, papszOptions );
    }

    int Stat( const char *pszFilename, VSIStatBufL *pStatBuf, int nFlags ) override
    {
      if ( !offline::blockPrivateNetworks() )
        return mOriginal->Stat( pszFilename, pStatBuf, nFlags );
      std::string target;
      if ( !admit( pszFilename, target ) )
      {
        errno = ENOENT;
        return -1;
      }
      if ( mMode == Mode::HostCheckOnly )
        return mOriginal->Stat( pszFilename, pStatBuf, nFlags );
      ensureGuardedRangeCache();
      const std::string cached = RemoteRangeCache::cachedPath( target );
      return VSIStatExL( cached.c_str(), pStatBuf, nFlags );
    }

    // Listing / metadata go to the origin through the ORIGINAL (unguarded)
    // transport — under the policy they answer "unknown" instead.
    char **ReadDirEx( const char *pszDirname, int nMaxFiles ) override
    {
      return offline::blockPrivateNetworks() ? nullptr : mOriginal->ReadDirEx( pszDirname, nMaxFiles );
    }
    char **SiblingFiles( const char *pszFilename ) override
    {
      return offline::blockPrivateNetworks() ? nullptr : mOriginal->SiblingFiles( pszFilename );
    }
    char **GetFileMetadata( const char *pszFilename, const char *pszDomain,
                            CSLConstList papszOptions ) override
    {
      return offline::blockPrivateNetworks() ? nullptr
                                             : mOriginal->GetFileMetadata( pszFilename, pszDomain, papszOptions );
    }
    char *GetSignedURL( const char *pszFilename, CSLConstList papszOptions ) override
    {
      return offline::blockPrivateNetworks() ? nullptr : mOriginal->GetSignedURL( pszFilename, papszOptions );
    }
    // Pure, network-free answers forward unchanged.
    int IsCaseSensitive( const char *pszFilename ) override { return mOriginal->IsCaseSensitive( pszFilename ); }
    int HasOptimizedReadMultiRange( const char *pszPath ) override
    {
      return offline::blockPrivateNetworks() ? FALSE : mOriginal->HasOptimizedReadMultiRange( pszPath );
    }
    const char *GetActualURL( const char *pszFilename ) override { return mOriginal->GetActualURL( pszFilename ); }
    const char *GetOptions() override { return mOriginal->GetOptions(); }
    bool IsLocal( const char *pszPath ) override { return mOriginal->IsLocal( pszPath ); }
    bool SupportsRead( const char *pszPath ) override { return mOriginal->SupportsRead( pszPath ); }

  private:
    /// Policy verdict for one spelling; \a target receives the http(s) URL.
    bool admit( const char *pszFilename, std::string &target ) const
    {
      if ( pszFilename == nullptr || mMode == Mode::RefuseAll )
        return false;
      const std::string name( pszFilename );
      if ( name.size() < mPrefix.size() )
        return false;
      target = name.substr( mPrefix.size() );
      if ( !isHttpUrl( target ) )
        return false; // ftp:// and friends have no guarded transport here
      return !offline::isPrivateHost( target );
    }

    std::string mPrefix;
    VSIFilesystemHandler *mOriginal; // owned by GDAL (InstallHandler replaced, never deleted)
    Mode mMode;
};

CPLHTTPResult *policyFetch( const char *pszURL, CSLConstList papszOptions, GDALProgressFunc, void *,
                            CPLHTTPFetchWriteFunc, void *, void * )
{
  if ( !offline::blockPrivateNetworks() )
    return nullptr; // default network layer
  if ( CSLFetchNameValue( papszOptions, "CLOSE_PERSISTENT" ) != nullptr )
    return nullptr; // no request is issued for this one
  const std::string url = pszURL ? pszURL : "";
  if ( !offline::isPrivateHost( url ) )
    return nullptr;
  auto *result = static_cast<CPLHTTPResult *>( CPLCalloc( 1, sizeof( CPLHTTPResult ) ) );
  result->nStatus = 1;
  result->pszErrBuf = CPLStrdup( "private network blocked by the process policy" );
  CPLError( CE_Failure, CPLE_AppDefined, "CPLHTTPFetch: private network blocked by the process policy" );
  return result;
}

std::once_flag g_hooksOnce;

void installHooks()
{
  const struct
  {
    const char *prefix;
    Mode mode;
  } targets[] = {
    { "/vsicurl/", Mode::RerouteThroughRangeCache },
    { "/vsicurl_streaming/", Mode::RerouteThroughRangeCache },
    { "/vsicurl?", Mode::RefuseAll },
    { "/vsiwebhdfs/", Mode::HostCheckOnly },
  };
  for ( const auto &t : targets )
  {
    VSIFilesystemHandler *original = VSIFileManager::GetHandler( t.prefix );
    // GetHandler falls back to the local filesystem for unknown prefixes —
    // only wrap a handler that really owns this prefix.
    if ( original == nullptr || original == VSIFileManager::GetHandler( "" ) )
      continue;
    VSIFileManager::InstallHandler( t.prefix, new PolicyHandler( t.prefix, original, t.mode ) );
  }
  CPLHTTPPushFetchCallback( policyFetch, nullptr );
}

} // namespace

void engageProcessPrivateNetworkBlocking()
{
  offline::setBlockPrivateNetworks( true );
  std::call_once( g_hooksOnce, installHooks );
}

void disengageProcessPrivateNetworkBlocking()
{
  offline::setBlockPrivateNetworks( false );
}

bool processPrivateNetworkBlockingEngaged()
{
  return offline::blockPrivateNetworks();
}

} // namespace sicnu::geo::network_policy
