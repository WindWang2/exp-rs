/***************************************************************************
 * io_fabric_operators.h — Cloud-Native Data Fabric / Data Cube 10.0 operator
 * family: catalog search, cube planning, window execution, cache prefetch.
 *
 * Thin JSON adapters over the Qt-free fabric/ module (same doctrine as the
 * io:* family): no catalog/cache/cube logic lives here — the operators
 * translate one JSON contract into one fabric call and report its own JSON.
 ***************************************************************************/
#pragma once

#include "io_operators.h"
#include "operators/framework/rs_operator.h"

namespace sicnu::operators::io
{

/**
 * io:catalog_search — unified catalog query over a local STAC tree, a
 * remote STAC API or in-memory record references (read-only, bounded).
 * Params: catalog (URI), bbox[], temporalStartUtc, temporalEndUtc,
 *         collections[], ids[], cloudCoverMax, platform, sensors[],
 *         assetRole, mediaType, limit, maxItems
 */
class IoCatalogSearchOperator final : public IoOperatorBase
{
  public:
    std::string name() const override { return "io:catalog_search"; }
    std::string displayName() const override { return "Search Catalog"; }
    std::string description() const override;
    std::string determinismGrade() const override { return "bit-exact"; }
    RSOperatorMemoryPolicy memoryPolicy() const override { return RSOperatorMemoryPolicy::Streaming; }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

/**
 * io:cube_plan — build an inspectable, costed execution plan from a
 * declarative intent (catalog query + grid + chunk shape + budget).
 * Params: catalog, records[] (ids of in-memory records are NOT accepted —
 *         records come from io:catalog_search output or a catalog URI),
 *         query{...}, sceneBudget, grid{...}, chunkShape{...},
 *         slice{...}, window{x,y,w,h}, executionBudgetBytes
 */
class IoCubePlanOperator final : public IoOperatorBase
{
  public:
    std::string name() const override { return "io:cube_plan"; }
    std::string displayName() const override { return "Plan Data Cube"; }
    std::string description() const override;
    std::string determinismGrade() const override { return "bit-exact"; }
    RSOperatorMemoryPolicy memoryPolicy() const override { return RSOperatorMemoryPolicy::Streaming; }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

/**
 * io:cube_window — execute a virtual-cube window read and publish the
 * result as a GeoTIFF (atomic publish; provenance JSON in the result).
 * Params: catalog, query{...}, sceneBudget, grid{...}, window{x,y,w,h},
 *         output, bandIndex, bandRole, mirrorDirectory
 */
class IoCubeWindowOperator final : public IoOperatorBase
{
  public:
    std::string name() const override { return "io:cube_window"; }
    std::string displayName() const override { return "Read Cube Window"; }
    std::string description() const override;
    std::string determinismGrade() const override { return "bit-exact"; }
    RSOperatorMemoryPolicy memoryPolicy() const override { return RSOperatorMemoryPolicy::UnsupportedForLargeRaster; }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

/**
 * io:cache_prefetch — warm the range cache for a chunk plan (bounded bytes,
 * cooperative cancel via the operator cancel flag when surfaced).
 * Params: catalog, query{...}, sceneBudget, grid{...}, chunkShape{...},
 *         slice{...}, maxBytes, chunkWindow, mirrorDirectory
 */
class IoCachePrefetchOperator final : public IoOperatorBase
{
  public:
    std::string name() const override { return "io:cache_prefetch"; }
    std::string displayName() const override { return "Prefetch Cache"; }
    std::string description() const override;
    std::string determinismGrade() const override { return "bit-exact"; }
    RSOperatorMemoryPolicy memoryPolicy() const override { return RSOperatorMemoryPolicy::Streaming; }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

} // namespace sicnu::operators::io
