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
 *   for programmatic submits (algorithm id after the prefix; simple JSON params).
 * - Toolbox / SicnuAlgorithmDialog uses per-job submit(callable) so full
 *   QVariantMap parameters and main-thread prepare/postProcess stay correct.
 */
namespace ProcessingJobAdapter {

/** Register JobEngine::registerExecutor("processing:", …). Safe to call once. */
void registerProcessingJobExecutor();

/** Best-effort QVariantMap → Json for JobRecord.result (string/number/bool). */
Json::Value resultsToJson( const QVariantMap &results );

/** Best-effort Json object → QVariantMap for prefix-executor params. */
QVariantMap jsonToVariantMap( const Json::Value &params );

/** Build algorithmId for the job panel, e.g. "processing:qgis_algorithms:buffer". */
std::string processingAlgorithmId( const QString &qgisAlgorithmId );

} // namespace ProcessingJobAdapter
