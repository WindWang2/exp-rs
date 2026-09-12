// execution_identity_bridge.h — activates the remote-input identity seam
// (execution_identity_resolver.h) with the geospatial remote-identity
// contract (Cloud-Native Geospatial Data Fabric 8.0, task B).
//
// The seam (7.0) was contract-only: no host installed a resolver, so remote
// inputs could never participate in execution-cache reuse. This bridge
// installs the geospatial-backed resolver:
//
//   path → TTL session cache → sicnu::geo::remoteIdentityToken(path) on
//   miss/expiry — fail-closed (an empty token means "cannot identify"; the
//   input stays uncacheable, exactly the seam's documented conservative
//   verdict; failures are never cached). The TTL window is what makes the
//   TaskCenter warm-then-consult pattern work: the warm-up pass probes
//   lock-free BEFORE the scheduler mutex is taken, and a consult under it
//   hits the recent entry — no network under any lock.
//
// Only tokens carry identity into fingerprints: no URL, no credentials, no
// validator strings — a cache key derived from the token never leaks anything
// about the origin (credential-shaped query values are removed inside the
// token's basis by the geospatial layer).
//
// Install at host startup, before any execution starts — the same set-once
// discipline as the underlying seam. At HEAD this means the pipeline-host
// processes: sicnu_geo_rs (app/main.cpp, GUI + MCP) and sicnu_geo_rs_cli
// (cli/main_cli.cpp — the install there covers the pipeline runner and both
// local worker hosts, which run in-process). Any other process that touches
// TaskCenter gets the geospatial default installed by the TaskCenter
// constructor (Execution Plane 8.0) as a backstop. The isolated sicnu_worker
// process intentionally does NOT install it: the seam is process-global and
// no identity consumer runs in the worker (fingerprinting happens only on
// the host's submission/admission path), so a worker-side install would be
// inert — no identity needs to cross the worker handshake either.
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
