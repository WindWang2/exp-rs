// execution_identity_bridge.h — activates the remote-input identity seam
// (execution_identity_resolver.h) with the geospatial remote-identity
// contract (Cloud-Native Geospatial Data Fabric 8.0, task B).
//
// The seam (7.0) was contract-only: no host installed a resolver, so remote
// inputs could never participate in execution-cache reuse. This bridge
// installs the geospatial-backed resolver:
//
//   path → sicnu::geo::remoteIdentityToken(path) — fail-closed (an empty
//   token means "cannot identify"; the input stays uncacheable, exactly the
//   seam's documented conservative verdict).
//
// Only tokens carry identity into fingerprints: no URL, no credentials, no
// validator strings — a cache key derived from the token never leaks anything
// about the origin (credential-shaped query values are removed inside the
// token's basis by the geospatial layer).
//
// Install at host startup (app/CLI/worker), before any execution starts —
// the same set-once discipline as the underlying seam.
#pragma once

#include "execution_identity_resolver.h"

#include <QString>

namespace sicnu::data
{

/// Installs the geospatial remote-identity resolver (idempotent: replacing an
/// existing resolver returns the previous one, mirroring
/// setExecutionIdentityResolver). Hosts that manage their own resolver should
/// NOT call this.
InputIdentityResolver *installGeospatialInputIdentityResolver();

} // namespace sicnu::data
