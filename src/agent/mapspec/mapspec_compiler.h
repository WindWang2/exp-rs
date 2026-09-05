// src/agent/mapspec/mapspec_compiler.h
#pragma once

//
// MapSpecCompiler / MapSpecExtractor — the MapSpec ⇄ QgsPrintLayout bridge
// (ADR 0127). Compilation goes through LayoutService's item factories so the
// compiled layout is exactly what the Layout Designer and layout:* tools
// would have produced (same undo command ids, same property layer).
//

#include <json/json.h>

#include <QString>

class QgsPrintLayout;

namespace sicnu::agent::mapspec {

class MapSpecCompiler
{
  public:
    /// Creates (or replaces) a layout named `spec.layout_name` on
    /// QgsProject::instance() and materializes every MapSpec item. Returns
    /// null with *error on invalid specs or compilation failures.
    /// LayoutService item ids are the MapSpec item ids (stable referents).
    static QgsPrintLayout *compile( const Json::Value &spec, QString *error = nullptr );

    /// Best-effort extraction of a MapSpec from an existing layout. Non-
    /// mappable QGIS items are surfaced as annotations with a `qgis_type`
    /// marker; the extracted document revalidates but may not roundtrip
    /// 1:1 (documented divergence).
    static Json::Value extract( QgsPrintLayout *layout );

    /// Compiles a MapSpec to a fresh layout and runs the cartography
    /// preflight on it, returning {layout, quality report}.
    /// (Declared here so the compile→preflight loop keeps one entry point.)
    static Json::Value compileAndAssess( const Json::Value &spec, QString *error = nullptr );
};

} // namespace sicnu::agent::mapspec
