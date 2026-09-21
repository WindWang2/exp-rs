#pragma once

// store_data_provider.h — read-only DatasetStore → DatasetFacts adapter.
//
// A thin, stateless projection: it holds a borrowed store pointer, calls
// only store READ APIs and never owns, caches or writes. Schema-derived
// fields come from the version's canonical manifest; whatever the store
// does not carry stays unknown — a projection never invents evidence.

#include "suitability_provider.h"

namespace sicnu::dataset
{
class DatasetStore;
}

namespace sicnu::suitability
{

class StoreDataProvider final : public SuitabilityDataProvider
{
    public:
        /// Borrows @p store (not owned; the caller keeps it alive). A null
        /// store fails typed ("suitability.provider_unavailable").
        explicit StoreDataProvider( const sicnu::dataset::DatasetStore *store );

        /// Typed failures: "suitability.provider_unavailable" without a
        /// store, "suitability.dataset_unknown" for a versionId that does
        /// not resolve; store read errors pass through unchanged.
        sicnu::data::Result<DatasetFacts> datasetFacts(
            const QString &datasetVersionId, const FactsLimits &limits ) const override;

    private:
        const sicnu::dataset::DatasetStore *m_store = nullptr;
};

} // namespace sicnu::suitability
