// studio_json_numeric.h — one version-independent JSON numeric shape test.
//
// Qt 6.12 (verified locally) stores integer JSON tokens as QJsonValue::Double
// and `isDouble()` returns true for them; other Qt 6.x minors may expose a
// distinct integer representation. Relying on `isDouble()` alone therefore
// makes digit identity, metric cells and submission ids toolchain-dependent.
// The exclusion-of-non-numeric-shapes test is stable across those versions.
#pragma once

#include <QJsonValue>

namespace sicnu::experiment_studio
{

inline bool isNumericJsonValue( const QJsonValue &value )
{
    return !value.isNull() && !value.isUndefined() && !value.isBool()
           && !value.isString() && !value.isArray() && !value.isObject();
}

} // namespace sicnu::experiment_studio
