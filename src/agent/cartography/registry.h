// src/agent/cartography/registry.h
#pragma once

//
// Cartographic Component Library (Phase I) + Template Library (Phase J).
//
// Components: declarative descriptors for the recurring cartographic
// furniture (north arrows, scale bars, legends, titles, …). QGIS-native
// objects stay the single rendering truth; a component only parameterizes
// how MapSpecCompiler creates one.
//
// Templates: compositions of semantic slots + recommended components +
// layout/style policy. `instantiateTemplate` produces a concrete MapSpec
// draft the agent then patches.
//
// Registries load `data/cartography/{components,templates}/*.json` from
// a resolvable directory and fall back to a small embedded safety set so
// headless runs never face an empty catalog.
//

#include <json/json.h>

#include <QMap>
#include <QMutex>
#include <QString>
#include <QStringList>

namespace sicnu::agent::cartography {

class ComponentRegistry
{
  public:
    static ComponentRegistry &instance();

    /// Directory override (scans *.json); reloads lazily.
    void setDirectory( const QString &dir );
    QString directory() const;

    /// All components as a JSON array of descriptors.
    Json::Value components() const;

    /// Components of one category (empty category = all).
    Json::Value byCategory( const QString &category ) const;

    Json::Value find( const QString &id ) const; ///< Null when unknown.

    /// Registers one component descriptor programmatically (validated).
    bool registerComponent( Json::Value descriptor, QString *error = nullptr );

    /// Reload from disk (directory() or default resolution).
    void reload();

  private:
    ComponentRegistry();
    /// Caller must hold mMutex.
    void ensureLoadedLocked() const;
    void loadEmbeddedDefaults();

    mutable QMutex mMutex;
    mutable bool mLoaded = false;
    QString mDirectory;
    mutable QMap<QString, Json::Value> mComponents; // id -> descriptor
};

class TemplateRegistry
{
  public:
    static TemplateRegistry &instance();

    void setDirectory( const QString &dir );
    QString directory() const;

    Json::Value templates() const;
    Json::Value find( const QString &id ) const;

    bool registerTemplate( Json::Value descriptor, QString *error = nullptr );

    void reload();

    /// Instantiates a template into a MapSpec draft:
    /// slots → concrete items (semantic_role stamped, rect from the slot or
    /// template layout policy), recommended components appended.
    /// `params` may carry {layout_name, title, source_note, layers: []}.
    /// Returns an empty value with *error on unknown template.
    Json::Value instantiateTemplate( const QString &id, const Json::Value &params,
                                     QString *error = nullptr ) const;

  private:
    TemplateRegistry();
    /// Caller must hold mMutex.
    void ensureLoadedLocked() const;
    void loadEmbeddedDefaults();

    mutable QMutex mMutex;
    mutable bool mLoaded = false;
    QString mDirectory;
    mutable QMap<QString, Json::Value> mTemplates; // id -> descriptor
};

} // namespace sicnu::agent::cartography
