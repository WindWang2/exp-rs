/***************************************************************************
  geospatial/util/resource_uri.h
  Remote Sensing I/O Foundation 5.0 — resource URI classification & identity.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  One strict, Qt-free classifier for every source string the system accepts:
  local files/directories (Windows drives, UNC, long paths, Unicode),
  directory products, remote http(s), GDAL VSI handles, virtual datasets,
  subdataset selectors, STAC assets and in-memory sources.

  Contract:
  * parsing NEVER throws and never loses bytes: `raw` carries the input
    verbatim; classification failures produce kind = Invalid + a reason.
  * identity vs display: canonical() normalizes identity (separator + VSI
    spelling), display() is the human/log form — percent-decoded and
    credential-redacted (userinfo and credential-shaped query values are
    masked). Log/UI surfaces must use display(), never raw.
  * no shell, no network: this module is pure string/FS classification.
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_RESOURCE_URI_H
#define SICNU_GEOSPATIAL_RESOURCE_URI_H

#include "geospatial/common.h"

#include <string>

namespace sicnu::geo
{

enum class ResourceKind
{
  Invalid,           ///< unclassifiable input (reason carried)
  LocalFile,         ///< regular file on a local filesystem
  LocalDirectory,    ///< directory on a local filesystem
  DirectoryProduct,  ///< directory that is itself a product (SAFE / GRANULE / MTL scene dir)
  RemoteHttp,        ///< http(s):// URL (possibly with userinfo/query)
  VsiRemote,         ///< GDAL network VSI handle (/vsicurl/, /vsis3/, /vsigs/, /vsiaz/, ...)
  VsiVirtual,        ///< non-network VSI handle (/vsi zip/tar/gzip/subfile/...)
  VirtualDataset,    ///< virtual dataset reference ("vrt://" / "virtual://")
  Subdataset,        ///< GDAL subdataset selector ("NETCDF:"f.nc":var", "HDF5:"f.h5"://v")
  StacAsset,         ///< STAC item/asset reference (stac:// or explicit item href)
  InMemory           ///< in-memory / temporary artifact reference (memory://, /vsimem/)
};

const char *resourceKindName( ResourceKind kind );

/// Query/fragment-aware classification of a source string.
struct ResourceUri
{
  public:
    /// Parses + classifies. Total function: never throws; invalid input yields
    /// kind = Invalid with `parseReason` filled.
    static ResourceUri parse( const std::string &raw );

    ResourceKind kind = ResourceKind::Invalid;
    std::string raw;          ///< input, verbatim
    std::string parseReason;  ///< set only when kind == Invalid

    /// Network VSI prefix as written ("/vsicurl", "/vsis3", ...), no trailing
    /// slash. Empty when kind != VsiRemote/VsiVirtual.
    std::string vsiPrefix;
    /// For RemoteHttp: scheme ("http"/"https"), userinfo ("user:pass"),
    /// host, path (percent-encoding preserved), query (without '?'), fragment
    /// (without '#'). For other kinds these stay empty.
    std::string scheme;
    std::string host;
    std::string userinfo;
    std::string path;      ///< decoded-from-VSI-payload or URL path (raw bytes kept)
    std::string query;
    std::string fragment;

    /// True when opening this resource can hit the network.
    bool isRemote() const;
    /// True when the payload is a local filesystem path (after VSI/URL
    /// decoding). Drive/UNC/relative all count.
    bool isLocalPayload() const;

    /// Identity form: normalized separators ('/'), VSI spelling kept,
    /// percent-encoding kept, userinfo kept (identity is never displayed).
    /// A trailing slash on a directory/product does not change identity.
    std::string canonical() const;
    /// Human/log form: decoded percent-escapes where valid UTF-8 results,
    /// userinfo masked, credential-shaped query values masked.
    std::string display() const;

    /// For VsiRemote: the underlying remote URL (URL after the prefix).
    /// For RemoteHttp: the /vsicurl/ spelling. Otherwise empty.
    std::string remoteUrl() const;
    /// For VsiRemote/VsiVirtual/Subdataset: the embedded local payload when
    /// the handler wraps a local file ("/vsizip/C:/a.zip", 'NETCDF:"C:/x.nc":v'),
    /// otherwise empty.
    std::string embeddedLocalPath() const;

    /// True when the input text looks like a directory product container:
    /// an existing directory whose name/layout smells like a sensor product
    /// (".SAFE", ".GRP", "MTL.txt" sibling, "GRANULE", "*.hdf" sidecars...).
    /// A pure filesystem check — no GDAL, no network.
    static bool looksLikeDirectoryProduct( const std::string &path );

    /// Joins a relative `reference` against `baseDirectory` (the trust root)
    /// with lexical '.'/'..' resolution. Absolute/remote references pass
    /// through re-classified. Any '..' chain that would climb above
    /// `baseDirectory` is refused (Invalid, reason "escape") — traversal
    /// containment is the point. The result classifies by shape (LocalFile),
    /// not by existence: resolved paths may be export targets.
    static ResourceUri resolveAgainst( const std::string &baseDirectory, const std::string &reference );

    /// Windows long-path spelling ("\\?\" + absolute normalized path).
    /// Non-Windows inputs and non-local payloads return the input unchanged.
    static std::string toWindowsLongPath( const std::string &localPath );
};

/// Percent-decodes `text`; invalid escapes are kept verbatim. Returns false
/// when any escape was malformed (decoding still produced a best effort).
bool percentDecode( const std::string &text, std::string &out );

/// True when the query-string value under `keyName` looks like a credential
/// (signature/token/key/... patterns, case-insensitive).
bool isCredentialQueryKey( const std::string &keyName );

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_RESOURCE_URI_H
