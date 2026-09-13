/***************************************************************************
  geospatial/fabric/object_store.h
  Cloud-Native Data Fabric / Data Cube 10.0 — object storage profile seam.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  WHAT THIS IS: the narrow seam between object-store URI spellings ("s3://…",
  "gs://…", "az://…") and the GDAL /vsi* network family this layer already
  owns, plus an explicit credential-injection window. No SDK, no second HTTP
  stack (ADR 0139 stays: CPL is the transport); profiles are data, signing is
  GDAL's job.

  WHAT THIS IS NOT: not a credential store. Credentials arrive as function
  arguments, live inside the RAII window, and are erased on scope exit. They
  never enter identity tokens, display forms, logs, JSON reports, or the
  mirror manifest (display surfaces keep going through ResourceUri::display(),
  which redacts credential shapes).

  Threading contract (DECISIONS D-1003): GDAL credential config options are
  PROCESS-GLOBAL. While a ScopedObjectStoreCredentials window is open, no
  other thread may open /vsi* paths through GDAL. Callers with UI threads
  must already be off-thread (same contract as every remote call here); the
  window additionally requires process-wide serialization of /vsi* opens for
  its duration.
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_FABRIC_OBJECT_STORE_H
#define SICNU_GEOSPATIAL_FABRIC_OBJECT_STORE_H

#include "geospatial/common.h"
#include "geospatial/util/resource_uri.h"

#include <json/json.h>

#include <memory>
#include <string>
#include <vector>

namespace sicnu::geo
{

/// One object-storage provider mapping: URI scheme → GDAL VSI prefix.
struct ObjectStoreProfile
{
    std::string scheme;        ///< "s3" (the part before "://" — lowercased)
    std::string vsiPrefix;     ///< "/vsis3/" (with both slashes)
    std::string provider;      ///< "aws" | "s3-compatible" | "gcs" | "azure"
    bool anonymousSupported = true;  ///< true when unsigned public reads work

    Json::Value toJson() const;
};

/// Registered provider table (the built-ins plus anything registered in this
/// process). Built-ins: s3→/vsis3/ (aws), s3a & s3c→/vsis3/ (s3-compatible
/// spellings used by the Hadoop and Containous ecosystems), gs→/vsigs/
/// (gcs), az→/vsiaz/ (azure).
std::vector<ObjectStoreProfile> objectStoreProfiles();

/// Looks a scheme up in the table. nullptr when the scheme is not a
/// registered object-store scheme (the caller then falls through to the
/// generic ResourceUri classification).
const ObjectStoreProfile *findObjectStoreProfile( const std::string &scheme );

/// Extends the table. Throws GeoError(InvalidArgument) for an empty scheme,
/// a missing VSI prefix, or a duplicate scheme. Thread-safety: registration
/// is a startup-time action, not synchronized against lookups.
void registerObjectStoreProfile( const ObjectStoreProfile &profile );

/// Credentials for one injection window. Everything is optional except the
/// pair (anonymous==false) → at least accessKeyId+secretAccessKey must be
/// present (validated at window construction, never at display time).
struct ObjectStoreCredentials
{
    std::string accessKeyId;
    std::string secretAccessKey;
    std::string sessionToken;   ///< "" when none
    std::string region;         ///< "" leaves the ambient region untouched
    std::string endpoint;       ///< custom endpoint ("http://127.0.0.1:9000")
                                ///< for S3-compatible stores; "" = provider default
    bool anonymous = false;     ///< true = force unsigned requests (public data)

    bool empty() const;
    Json::Value toJsonShape() const;   ///< SHAPE ONLY — booleans and set/unset
                                       ///< keys, never values
};

/// Resolved object-store resource: the classified input, the profile that
/// claimed it, and the fetchable VSI spelling downstream layers open.
struct ObjectStoreResolution
{
    ResourceUri uri;             ///< re-classified input (raw spelling kept)
    ObjectStoreProfile profile;  ///< the claiming profile
    std::string vsiPath;         ///< "<prefix><bucket>/<key>" (no credentials)
    std::string bucket;
    std::string key;
    bool needsCredentials = false;  ///< false when the caller passed nothing and
                                    ///< the profile allows anonymous reads

    Json::Value toJson() const;  ///< redacted — never carries credentials
};

/// Classifies an object-store URI and maps it to its VSI spelling.
/// Accepts "<scheme>://bucket/key" spellings (profile must exist) and
/// direct VSI spellings ("​/vsis3/bucket/key" — identity of the claiming
/// profile comes from the prefix). Throws GeoError(InvalidArgument) for
/// non-object-store inputs and unknown schemes; never hits the network.
ObjectStoreResolution resolveObjectStore( const std::string &rawUri );

/// Maps any fetchable path (VSI or plain URL) to the /vsirangecache/
/// spelling when the cache is installed; returns the input unchanged
/// otherwise. Pure string mapping — the cache owns all semantics.
std::string fabricCachedPath( const std::string &fetchablePath );

/// RAII credential window: installs the GDAL config options that make the
/// profile's VSI prefix authenticate with these credentials, and on
/// destruction restores exactly those keys to their prior state AND wipes
/// GDAL's per-URL VSICURL handle cache (a later window for the same URL
/// must never reuse this window's signed context). Throws GeoError(InvalidArgument) when !anonymous lacks the
/// key pair, or the prefix is not a known /vsi* network prefix.
/// The process-global serialization requirement is D-1003 — enforced by
/// convention, documented here, and asserted by a test-visible counter
/// (activeScopedCredentialWindows()) for diagnostics.
/// One config key a credential window installed (with the prior state to
/// restore — the window erases exactly what it set, nothing else).
struct FabricInstalledConfigKey
{
    std::string key;
    std::string priorValue;
    bool hadPrior = false;
};

class ScopedObjectStoreCredentials
{
  public:
    ScopedObjectStoreCredentials( const std::string &vsiPrefix,
                                  const ObjectStoreCredentials &credentials );
    ~ScopedObjectStoreCredentials();
    ScopedObjectStoreCredentials( const ScopedObjectStoreCredentials & ) = delete;
    ScopedObjectStoreCredentials &operator=( const ScopedObjectStoreCredentials & ) = delete;

  private:
    std::string mPrefix;
    std::vector<FabricInstalledConfigKey> mSetKeys;   ///< exact restore journal
};

/// Diagnostic count of currently-open credential windows (tests observe
/// RAII balance through this; zero at rest).
int activeScopedCredentialWindows();

/// True when the input is an offline-refused remote target (delegates to
/// the offline gate's isRemoteTarget) — object-store reads consult this
/// before any open and refuse with the gate's typed message.
bool fabricOfflineRefusal( const std::string &fetchablePath, std::string &messageOut );

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_FABRIC_OBJECT_STORE_H
