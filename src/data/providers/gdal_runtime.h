#pragma once

class QString;

namespace sicnu::data::providers
{

/// Ensures GDAL/OGR drivers are registered once per process (thread-safe).
/// Providers call this before opening datasets so resolution works without
/// relying on an external GDALAllRegister() at process startup.
void ensureGdalRuntime();

} // namespace sicnu::data::providers
