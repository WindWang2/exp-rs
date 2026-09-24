// operator_catalog.h — teacher-side view of the REAL operator registry.
//
// Truth lives in the repo's capability sidecars
// (data/processing/algorithm_meta/capability/rs-*.json, generated from
// RSOperatorRegistry by scripts/capability_knowledge_tool.cpp) — this module
// only reads them so authoring validation never hardcodes an operator
// allow-list. No second registry: ids and parameter names come verbatim from
// the sidecars.
#pragma once

#include "admin_types.h"

#include <QJsonObject>
#include <QSet>
#include <QString>

namespace sicnu::teaching_admin {

struct OperatorCatalog
{
    QSet<QString> operatorIds;   ///< sidecar "id" values (rs:ndvi, …)
    QJsonObject paramSchemas;    ///< id → {"properties": {param: {...}}}
    QVector<AdminIssue> issues;  ///< unreadable / id-less sidecars (fail-closed)

    QJsonObject toJson() const;
};

/// Load the operator catalog from a capability directory. A missing directory
/// yields an empty catalog plus a typed issue — callers must degrade to
/// fail-closed validation, never to a hardcoded demo allow-list.
OperatorCatalog loadOperatorCatalog( const QString &capabilityDir );

} // namespace sicnu::teaching_admin
