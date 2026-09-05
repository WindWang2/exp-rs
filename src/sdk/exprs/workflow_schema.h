/***************************************************************************
 * exprs/workflow_schema.h — public workflow document schema (v1)
 *
 * The workflow document is a public, versioned contract shared by the C++
 * engine, the CLI and the Python SDK. See docs/schemas/workflow-schema-v1.md
 * for the reference description. Rules:
 *
 *   - "schema_version" MUST be an integer. Documents without the field are
 *     treated as version 1 (legacy tolerance: the pre-SDK engine format).
 *   - Unknown optional fields are ignored; unknown required structures are
 *     validation errors.
 *   - A version greater than the highest supported one is rejected with a
 *     diagnostic naming the supported range; migrateWorkflowDocument() is
 *     the upgrade path.
 ***************************************************************************/
#pragma once

#include <json/json.h>

#include "exprs/plugin_diagnostics.h"

namespace exprs {

/// Highest workflow schema version this build understands.
constexpr int kWorkflowSchemaVersion = 1;

/// Returns true when @p document is a structurally valid public workflow
/// document (schema_version gate + step shape + unique ids + operator ids).
/// Full semantic validation (topology, port types, gates) stays with the
/// workflow engine; this validator is the portable public contract.
bool validateWorkflowDocument( const Json::Value &document, PluginDiagnosticLog &diagnostics );

/// Returns the document's schema version; documents without the field
/// report 1. Returns 0 and adds a diagnostic when the field is present but
/// not a positive integer.
int workflowSchemaVersion( const Json::Value &document, PluginDiagnosticLog &diagnostics );

/// Migrates @p document to @p targetVersion. Returns the migrated document.
/// Version 1 is the baseline: identity. Future versions append steps here.
Json::Value migrateWorkflowDocument( const Json::Value &document, int targetVersion,
                                     PluginDiagnosticLog &diagnostics );

/// Convenience: fills @p document with {"schema_version": 1} if absent.
void stampWorkflowSchemaVersion( Json::Value &document );

} // namespace exprs
