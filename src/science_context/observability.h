// src/science_context/observability.h
#pragma once

//
// Lightweight observability structs for a future Control Center.
// No Control Center UI ships in this track.
//

#include <json/json.h>

#include <cstdint>
#include <string>
#include <vector>

namespace sicnu::science_context {

struct BrokerMetrics
{
    std::uint64_t synthesizeCount = 0;
    std::uint64_t cacheHits = 0;
    std::uint64_t cacheMisses = 0;
    std::uint64_t truncations = 0;
    std::uint64_t blockedPlans = 0;
    std::uint64_t conflictedAssets = 0;
    std::uint64_t unknownAssets = 0;
    std::vector<std::string> lastIntentPath; ///< recent intents (bounded)
};

class BrokerObservability
{
  public:
    void recordSynthesize( bool cacheHit, bool truncated, bool blocked,
                           const std::string &intent, bool conflicted, bool unknown );
    BrokerMetrics snapshot() const;
    Json::Value toJson() const;
    void reset();

  private:
    BrokerMetrics mMetrics;
};

} // namespace sicnu::science_context
