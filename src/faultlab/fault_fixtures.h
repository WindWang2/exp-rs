// fault_fixtures.h — the deterministic base-fixture factory.
//
// Every fixture is closed-form (plus seeded noise through the framework's
// own PCG32 stream) so a scenario's expected observable deltas are exactly
// computable and the same (fixture, seed) pair always materializes the same
// bytes. Fixtures are teaching-scale (16x16); nothing here touches disk —
// the GDAL adapter (src/faultlab/gdal) materializes them as GeoTIFFs when a
// scenario needs an artifact-level oracle.
#pragma once

#include "fault_types.h"

#include <string>

namespace sicnu::faultlab
{

/// Materializes the fixture `id` deterministically from `seed`. Unknown ids
/// fail with the typed `faultlab.fixture_unknown`.
FaultResult<FaultGrid> makeFixture( const std::string &id, const Json::Value &params,
                                    std::uint32_t seed );

/// Ids the factory can materialize.
const std::vector<std::string> &fixtureIds();

} // namespace sicnu::faultlab
