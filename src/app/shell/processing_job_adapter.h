/***************************************************************************
 * processing_job_adapter.h  —  QgsProcessingAlgorithm → JobEngine bridge
 ***************************************************************************/
#pragma once

#include <json/json.h>

#include <QString>
#include <QVariantMap>

#include <string>

/**
 * Helpers for running QgsProcessing algorithms via JobEngine.
 *
 * - registerProcessingJobExecutor() installs the "processing:" prefix executor
 *   which delegates to the AtomicAlgorithmRegistry adapter (ADR 0062): the
 *   prefix is stripped and ProviderAlgorithmAdapter runs the full
 *   prepare/runPrepared/postProcess lifecycle.
 * - Toolbox / SicnuAlgorithmDialog uses per-job submit(callable) so full
 *   QVariantMap parameters and main-thread prepare/postProcess stay correct.
 * - resultsToJson / processingAlgorithmId are used by those dialog callables.
 */
namespace ProcessingJobAdapter {

/** Register JobEngine::registerExecutor("processing:", …). Safe to call once. */
void registerProcessingJobExecutor();

/** Best-effort QVariantMap -> Json for JobRecord.result (string/number/bool). */
Json::Value resultsToJson( const QVariantMap &results );

/** Best-effort QVariant -> Json (string/number/bool). */
Json::Value variantToJson( const QVariant &v );

/** Build algorithmId for the job panel, e.g. "processing:qgis_algorithms:buffer". */
std::string processingAlgorithmId( const QString &qgisAlgorithmId );

} // namespace ProcessingJobAdapter
