/***************************************************************************
  geospatial/remote/vsi_object_identity.h
  Cloud Data Fabric 11.0 — identity facts of one network VSI object.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  One bounded HEAD-equivalent probe for network VSI payloads
  ("/vsis3/bucket/key", "/vsigs/…", "/vsiaz/…", …): VSIGetFileMetadata is
  the VSI-stack HEAD — GDAL signs it and answers from the ambient
  credential window (the same D-1003 process-serialization contract as
  every /vsi* open). The ETag is what makes an object PROVABLE; absent,
  weak, or S3-multipart-"null" validators are unprovable — the fail-closed
  rule every cache and mirror in this repo keys on.

  This lives in remote/ (the transport layer, next to the http validator)
  so fabric keeps calling downward; object_store re-exports the same facts
  under its object-store vocabulary.
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_REMOTE_VSI_OBJECT_IDENTITY_H
#define SICNU_GEOSPATIAL_REMOTE_VSI_OBJECT_IDENTITY_H

#include "geospatial/common.h"

#include <cstdint>
#include <string>

namespace sicnu::geo
{

/// Identity facts of one network VSI object. `probed=false` covers every
/// failure mode (offline gate, refused auth, unknown handler) — probing is
/// never a throw path for callers; the typed text explains why.
struct VsiObjectIdentityFacts
{
    bool probed = false;
    std::string etag;               ///< strong ETag ("" when absent/weak/null)
    std::uintmax_t sizeBytes = 0;
    bool hasSize = false;
    std::string errorText;          ///< typed text when !probed

    bool provable() const { return probed && !etag.empty(); }
};

/// Probes HEAD-equivalent facts for one network VSI path (must start with
/// a network VSI prefix; not validated here — the caller owns input
/// classification). The probe re-answers from GDAL's own handle cache when
/// the object was just opened, so it is cheap after a metadata open.
VsiObjectIdentityFacts probeVsiObjectIdentity( const std::string &vsiPath );

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_REMOTE_VSI_OBJECT_IDENTITY_H
