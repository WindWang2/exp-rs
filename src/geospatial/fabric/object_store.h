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
  PROCESS-GLOBAL. While a ScopedObjectStoreCredentials window is open — AND
  through its destructor's VSICURL cache wipe — no other thread may open
  /vsi* paths through GDAL. Callers with UI threads must already be
  off-thread (same contract as every remote call here); the window
  additionally requires process-wide serialization of /vsi* opens covering
  construction, use, and destruction.
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_FABRIC_OBJECT_STORE_H
#define SICNU_GEOSPATIAL_FABRIC_OBJECT_STORE_H

#include "geospatial/common.h"
#include "geospatial/identity/asset_identity.h"
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

/// Looks a scheme up in the table (BY VALUE — registrations may
/// reallocate the underlying storage at any time; a returned reference
/// would be a race the caller cannot see). Empty when the scheme is not a
/// registered object-store scheme (the caller then falls through to the
/// generic ResourceUri classification).
ObjectStoreProfile findObjectStoreProfile( const std::string &scheme );   // empty scheme = miss

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

/// Canonical, credential-free object key (11.0, DECISIONS D-1101). The ONE
/// identity spelling of an object within a provider family: every accepted
/// spelling ("s3://b/k", "s3a://b/k", "s3c://b/k", "/vsis3/b/k")
/// canonicalizes to "<canonical-scheme>://<bucket>/<key>". Pure string work —
/// never touches the network and never carries credentials (userinfo, query
/// and fragment shapes are refused with valid=false, mirroring
/// resolveObjectStore's refusals).
struct CanonicalObjectKey
{
    std::string scheme;    ///< canonical scheme ("s3" for s3/s3a/s3c)
    std::string provider;  ///< "aws" | "s3-compatible" | "gcs" | "azure"
    std::string bucket;    ///< as written (bucket identity is provider-caseful)
    std::string key;       ///< object key, verbatim bytes, no leading slash
    std::string canonical; ///< "<scheme>://<bucket>/<key>"
    bool valid = false;    ///< false: not an object-store spelling / malformed
};

/// Canonicalizes an object-store resource. Total function: valid=false for
/// anything resolveObjectStore would refuse; never throws, never networks.
CanonicalObjectKey canonicalObjectKey( const std::string &resource );

/// True when the resource carries an object-store VSI prefix from a
/// registered profile ("/vsis3/…", "/vsigs/…", "/vsiaz/…"). Pure check.
bool isObjectStoreVsiPath( const std::string &fetchablePath );

/// Identity facts of one object-store resource, captured through the VSI
/// stack (WP A): GDAL signs the request and honors the ambient credential
/// window (D-1003 serialization applies, same as every /vsi* open). The
/// ETag is what makes an object PROVABLE; absent/weak ⇒ unprovable
/// (fail-closed — mirrors and caches must not key on it).
struct ObjectStoreIdentityFacts
{
    bool probed = false;            ///< the store answered the metadata probe
    std::string etag;               ///< strong ETag ("" when absent/weak)
    std::uintmax_t sizeBytes = 0;
    bool hasSize = false;
    std::string errorText;          ///< typed text when !probed

    bool provable() const { return probed && !etag.empty(); }
};

/// Probes identity facts (HEAD-equivalent) for an object-store resource.
/// Accepts every object-store spelling (VSI or scheme — resolved through
/// the profile table). Offline/unreachable stores yield probed=false (never
/// a guess); network failures fold into errorText, never throw. Non-
/// object-store inputs throw GeoError(InvalidArgument).
ObjectStoreIdentityFacts probeObjectStoreIdentity( const std::string &resource );

/// The 11.0 object-store identity token: the SAME ri1:v1 basis the 8.0
/// remote tokens use, with the URL component replaced by the credential-
/// free canonical object key (DECISIONS D-1101 — every spelling of the same
/// object converges to one token; http(s)-URL identities keep their 8.0
/// basis because a URL and its bucket/key spelling cannot prove byte
/// equality without endpoint knowledge). Returns "" when the facts are not
/// provable (fail-closed).
std::string objectStoreIdentityToken( const std::string &canonicalObjectKey,
                                      const ObjectStoreIdentityFacts &facts );

/// The fabric's ONE identity entry point (WP A): object-store spellings
/// ("s3://b/k", "/vsis3/b/k", …) take the canonical-object-key identity;
/// everything else delegates to assetIdentityToken unchanged. Fabric
/// callers MUST use this (never assetIdentityToken directly) so cache and
/// mirror keys are spelling-stable.
AssetIdentity fabricAssetIdentity( const std::string &resource,
                                   const AssetIdentityOptions &options = {} );

/// Credential-context fingerprint for cache/identity separation (WP B,
/// D-1102): a short SHA-256 prefix over the credential window's NON-secret
/// shape (prefix + endpoint + access key id + token presence + anonymous
/// flag). Two windows fingerprint the same ONLY when they address the same
/// store as the same principal. The secret access key NEVER enters the
/// fingerprint, any cache key, or any log.
std::string objectStoreCredentialContext( const std::string &vsiPrefix,
                                          const ObjectStoreCredentials &credentials );

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
    std::string mOwnContext;                          ///< cache-context fingerprint
                                                      ///< this window installed
                                                      ///< (D-1102)
    std::string mPriorContext;                        ///< cache-context fingerprint
                                                      ///< live before this window
                                                      ///< opened (exact restore)
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
