/***************************************************************************
  geospatial/remote/remote_identity_resolver.h — default execution-identity
  resolver for REMOTE inputs (Execution Plane 8.0, WP-F).
  ---------------------------

  Activates the `sicnu::data::execution_identity_resolver` seam (Execution
  Plane 7.0, SEAM ONLY until now) for the one input class the platform had no
  identity source for: a resource behind a REMOTE service whose bytes can
  change server-side while the local cache still looks valid.

  Identity contract (fail-closed, mirrors remote_source_validator.h):
    * a STRONG ETag confirmed by probe/revalidate is the identity token
      ("etag:<opaque>") — stable across processes/sessions for unchanged
      content, guaranteed different when the content changed;
    * a WEAK ETag, Last-Modified-only identity, an offline/timeout probe, or
      an inconclusive answer yield an EMPTY token ⇒ the input is UNCACHEABLE
      (the caller must never fabricate identity from size or mtime alone);
    * only http(s) URLs resolve; every other path yields an empty token (the
      collector keeps its local-file handling untouched).

  The returned std::function is std::string-based (this layer is Qt-free);
  the installing layer (TaskCenter) adapts it to
  sicnu::data::InputIdentityResolver. Installation is a HOST-side decision —
  TaskCenter installs this factory's result when no other resolver is set.

  A bounded session cache (256 entries, insertion-order eviction) converts
  repeated submissions of the same origin into cheap lookups: an entry
  younger than a TTL (SICNU_REMOTE_IDENTITY_TTL_MS, default 5 s) is returned
  without network use; older entries are revalidated (one bounded round
  trip), so a changed resource is detected and its NEW strong ETag becomes
  the identity. The cache mutex is never held across network I/O, and the
  installing layer warms the cache before any lock-sensitive section.
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_REMOTE_IDENTITY_RESOLVER_H
#define SICNU_GEOSPATIAL_REMOTE_IDENTITY_RESOLVER_H

#include <functional>
#include <string>

namespace sicnu::geo
{

/// Builds the remote identity resolver function (see header comment for the
/// contract). Returns an independent resolver sharing the same process-global
/// session cache. Deliberately std::string-based: this layer is Qt-free;
/// the installing layer adapts to sicnu::data::InputIdentityResolver.
std::function<std::string( const std::string &path )> makeRemoteInputIdentityResolver();

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_REMOTE_IDENTITY_RESOLVER_H
