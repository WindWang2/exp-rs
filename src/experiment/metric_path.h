// metric_path.h — dotted-path numeric lookup inside metric documents.
// Shared by the matrix aggregator (M5) and the promotion evaluator (M8):
// one lookup rule everywhere — only finite numbers count, everything else
// is "not recorded" (never zero-filled).
#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <cmath>
#include <optional>

namespace sicnu::experiment
{

inline std::optional<double> metricValueAtPath( const QJsonObject &document,
                                                const QString &path )
{
    const QStringList parts = path.split( QLatin1Char( '.' ) );
    QJsonObject cursor = document;
    for ( int i = 0; i < parts.size() - 1; ++i )
    {
        cursor = cursor.value( parts.at( i ) ).toObject();
        if ( cursor.isEmpty() )
            return std::nullopt;
    }
    const QJsonValue value = cursor.value( parts.last() );
    if ( !value.isDouble() )
        return std::nullopt;
    const double number = value.toDouble();
    if ( !std::isfinite( number ) )
        return std::nullopt;
    return number;
}

} // namespace sicnu::experiment
