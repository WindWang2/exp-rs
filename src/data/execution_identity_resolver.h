// execution_identity_resolver.h — remote input identity seam (Execution
// Plane 7.0, package F).
//
// The execution fingerprint identifies every input by stable identity:
// in-pipeline producers chain their fingerprints, registered sources use
// (assetId, revision) via the DataManager catalog, and out-of-band content
// is guarded by stat+digest binding (#749). What the platform has NO
// identity source for today is an input that lives behind a REMOTE
// service (a COG behind a signed URL, a STAC item asset, a model weight
// artifact in object storage): its bytes can change server-side while the
// local cache still holds a valid-looking registration.
//
// This seam is the single authoritative extension point for that: a host
// that owns a remote identity source (ETag, content hash from a STAC
// checksum field, object-store version id) installs a resolver; the
// fingerprint input collector consults it AFTER the local resolution
// chain missed. The contract is fail-closed:
//   - an empty return means "cannot identify" ⇒ the input is
//     uncacheable (no fingerprint), never a guessed identity;
//   - the returned token must be STABLE across processes and sessions
//     for identical content identity, and must CHANGE when the remote
//     content changes (that is the entire point).
//
// The default resolver (no installation) keeps master behavior: remote
// inputs resolve only through the registered-asset chain and otherwise
// fail conservative.
#pragma once

#include <QString>

#include <functional>

namespace sicnu::data
{

/// Returns a stable identity token for @p canonicalPath (a remote or local
/// input path/URI the local resolution chain could not identify), or an
/// empty string when the input cannot be identified (uncacheable — the
/// caller must not fabricate an identity).
using InputIdentityResolver = std::function<QString( const QString &canonicalPath )>;

/// The installed resolver (nullptr when none — the default). Process-global,
/// set once by the host at startup; read from admission/fingerprint paths.
/// Not thread-mutable on purpose: install before any execution starts.
InputIdentityResolver *executionIdentityResolver();
/// Installs @p resolver (nullptr restores the default). Returns the
/// previously installed resolver (for layered hosts).
InputIdentityResolver *setExecutionIdentityResolver( InputIdentityResolver resolver );

} // namespace sicnu::data
