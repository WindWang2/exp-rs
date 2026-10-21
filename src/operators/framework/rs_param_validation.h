/***************************************************************************
 * rs_param_validation.h — schema-declared numeric range gate (#1578)
 *
 * Lives in the sicnu_operators_core leaf (std + jsoncpp only, same
 * dependency budget as rs_operator_error) so that funnels which call
 * RSOperator::run() directly and link ONLY the leaf — the processing
 * algorithm adapter, the job engine — can enforce setRange declarations
 * without linking the full sicnu_operators shared library.
 ***************************************************************************/
#pragma once

#include "rs_operator_error.h"

#include <json/json.h>

namespace sicnu::operators
{

/**
 * Refuses params that violate their schema-declared numeric ranges with a
 * typed InvalidParameter error. Pass the operator's schema root (the
 * makeRootSchema object: {"properties": {...}}).
 *
 * Opt-in per declaration: params without minimum/maximum, non-numeric
 * values, non-numeric bounds and absent params are not touched — typed
 * accessors inside run() keep handling those.
 *
 * @throws RSOperatorError(InvalidParameter) on the first violation.
 */
void validateDeclaredRanges(const Json::Value &schemaRoot, const Json::Value &params);

} // namespace sicnu::operators
