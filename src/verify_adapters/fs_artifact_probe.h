// src/verify_adapters/fs_artifact_probe.h — real IArtifactProbe over the
// local filesystem (ADR 0172 provider seam, verify_context.h).
//
// Projection discipline: the probe reports what the filesystem and bytes
// actually say — existence, size, sha256, a SHALLOW kind sniff — and never
// judges. A size over the digest budget yields an empty digest (the engine
// turns that into verify:i_digest_unavailable, never a guess); kind sniffing
// is by name only — deep structure stays with artifact.schema/artifact.grid
// checks over the same path.
#pragma once

#include "verify/verify_context.h"

#include <json/json.h>

#include <cstdint>
#include <optional>
#include <string>

namespace sicnu::verify_adapters
{

class FsArtifactProbe final : public sicnu::verify::IArtifactProbe
{
  public:
    /// Whole-file sha256 is only attempted up to this many bytes (the lab
    /// grade byte-budget precedent); larger artifacts report "" — the
    /// engine's reproducibility digest checks then read Indeterminate
    /// instead of allocating an unbounded buffer.
    static constexpr std::uint64_t kDefaultDigestBudgetBytes = 64ull * 1024ull * 1024ull;

    explicit FsArtifactProbe( std::uint64_t digestBudgetBytes = kDefaultDigestBudgetBytes );

    std::optional<sicnu::verify::ArtifactInfo> probe( const std::string &path ) override;
    std::optional<Json::Value> readJson( const std::string &path ) override;

  private:
    std::uint64_t mDigestBudgetBytes;
};

} // namespace sicnu::verify_adapters
