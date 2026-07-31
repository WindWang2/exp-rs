// src/agent/env_flag.h
#pragma once

#include <QByteArray>
#include <QString>

/// Parses a SICNU_* boolean environment flag: enabled for "1", "true", "yes",
/// "on" (case-insensitive, trimmed); anything else (including unset) is
/// disabled. Shared home for the MCP server and STAC client so flag semantics
/// cannot drift apart.
inline bool envFlagEnabled(const char *name)
{
    const QByteArray v = qgetenv(name);
    if (v.isEmpty())
        return false;
    const QString s = QString::fromUtf8(v).trimmed().toLower();
    return s == QLatin1String("1") || s == QLatin1String("true") || s == QLatin1String("yes")
           || s == QLatin1String("on");
}
