/***************************************************************************
  geospatial/identity/asset_identity.h
  Cloud-Native Geospatial Data Fabric 9.0 — unified asset identity (M1).
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  One entry point answers "is THIS resource provably the same bytes it was?"
  for local files, remote URLs, VSI spellings and multidim subdataset
  selectors:

    * local files: strong identity over the canonical absolute path + size +
      mtime + inode + a budget-bounded content hash — a content change (or
      metadata change when only metadata is hashed) yields a DIFFERENT token,
      so caches/fingerprints keyed on the token invalidate themselves.
    * remote resources: the 8.0 fail-closed ETag tokens (strong ETag only,
      credential-shaped queries stripped from the basis) — reused verbatim,
      no second identity scheme.
    * subdatasets: the container's token plus the canonical subdataset
      selector — two variables of one NetCDF/HDF5 file are two identities.
    * everything unprovable (missing/unreadable, weak-only validators,
      directories, in-memory artifacts): token == "" — callers must treat ""
      as UNCACHEABLE, never as "reuse freely" (fail-closed, same doctrine as
      the 8.0 remote tokens).

  Secrets never enter any basis: credential-shaped query values are stripped
  by the remote path, and local bases carry filesystem facts only.
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_ASSET_IDENTITY_H
#define SICNU_GEOSPATIAL_ASSET_IDENTITY_H

#include "geospatial/common.h"

#include <json/json.h>

#include <cstdint>
#include <string>

namespace sicnu::geo
{

/// How much of a local file's content backs the identity (the hash budget).
/// The default hashes a prefix — cheap, and for EO formats the header region
/// is where format-changing mutations land. 0 = metadata-only identity.
inline constexpr std::uint64_t kDefaultLocalIdentityHashBytes = 8ull * 1024 * 1024;

struct LocalIdentityOptions
{
    std::uint64_t hashBytes = kDefaultLocalIdentityHashBytes;
};

/// Identity of one local file. `token` is "" exactly when the identity is
/// unprovable (unreadable/missing). `strength` is "content" when at least
/// one byte was hashed, "metadata" otherwise (still usable, honestly
/// labelled weaker), "" when unprovable.
struct LocalIdentityToken
{
    std::string token;       ///< "li1:v1:<hex>" or ""
    std::string strength;    ///< "content" | "metadata" | ""
    std::string canonicalPath;
    std::uintmax_t sizeBytes = 0;
    std::uintmax_t hashedBytes = 0;

    bool provable() const { return !token.empty(); }
    Json::Value toJson() const;
};

/// Strong local-file identity. Total with respect to filesystem errors:
/// missing/unreadable files yield an unprovable result, never a guess.
LocalIdentityToken localIdentityToken( const std::string &path,
                                       const LocalIdentityOptions &options = {} );

/// Unified identity over any resource spelling (local path, http(s) URL,
/// /vsi/ handle, "NETCDF:"f.nc":var" subdataset). Remote resources reuse the
/// 8.0 fail-closed ETag tokens ("ri1:v1:…") verbatim.
struct AssetIdentityOptions
{
    LocalIdentityOptions local;
    int timeoutSeconds = 10;
    int connectTimeoutSeconds = 5;
    int maxRetries = 1;
    std::uintmax_t probeBytes = 1024; ///< bounded remote metadata probe
};

struct AssetIdentity
{
    std::string token;       ///< scheme-tagged token, "" = unprovable
    std::string strength;    ///< "content" | "etag" | "metadata" | ""
    std::string resourceKind;///< ResourceKind name of the classified input
    Json::Value toJson() const;

    bool provable() const { return !token.empty(); }
};

AssetIdentity assetIdentityToken( const std::string &resource,
                                  const AssetIdentityOptions &options = {} );

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_ASSET_IDENTITY_H
