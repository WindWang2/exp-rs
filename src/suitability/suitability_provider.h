#pragma once

// suitability_provider.h — the read-only facts channel for scale queries.
//
// The assessor core never opens a data source itself: dataset statistics
// arrive through this interface, bounded by FactsLimits. A provider that
// touches any limit MUST report factsTruncated — silent truncation is a bug.

#include "../data/data_result.h"
#include "dataset_facts.h"

#include <QString>

namespace sicnu::suitability
{

/// Caps a provider must honor while collecting facts. Reaching a cap is
/// reported through DatasetFacts::factsTruncated, never by silently folding
/// the remainder away.
struct FactsLimits
{
    int maxClassValues = 64;      ///< facet value cap per distribution
    qint64 maxSampleScan = 50000; ///< row-scan cap for statistics no facet carries
};

class SuitabilityDataProvider
{
    public:
        virtual ~SuitabilityDataProvider() = default;

        /// Projects one dataset version's statistics. Failures are typed and
        /// carry the provider's own diagnostic vocabulary unchanged. Const:
        /// a provider is a read-only channel (the assessor borrows it as
        /// a const pointer).
        virtual sicnu::data::Result<DatasetFacts> datasetFacts(
            const QString &datasetVersionId, const FactsLimits &limits ) const = 0;
};

/// Test/offline fake: hands out one held facts value unchanged, or one
/// injected failure when constructed with a diagnostic.
class InMemoryDataProvider final : public SuitabilityDataProvider
{
    public:
        explicit InMemoryDataProvider( DatasetFacts facts );
        explicit InMemoryDataProvider( sicnu::data::Diagnostic failure );

        sicnu::data::Result<DatasetFacts> datasetFacts(
            const QString &datasetVersionId, const FactsLimits &limits ) const override;

    private:
        DatasetFacts m_facts;
        sicnu::data::Diagnostic m_failure;
        bool m_fails = false;
};

} // namespace sicnu::suitability
