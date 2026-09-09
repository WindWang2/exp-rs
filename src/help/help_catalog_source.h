/***************************************************************************
 * help_catalog_source.h — read-only seams over the authoritative catalogs
 *
 * sicnu_help must not link the app or operator layers (no cycles, no business
 * logic). Consumers with access to the real registries (GUI shell, CLI, tests)
 * implement these narrow interfaces and hand the snapshots to the providers.
 * Every field is copied at query time — the help layer keeps no live handles
 * into business objects.
 ***************************************************************************/
#pragma once

#include <QString>
#include <QStringList>

#include <json/json.h>

#include <QVector>

namespace sicnu::help
{

/// One command fact snapshot (mirrors CommandDefinition's user-facing fields).
struct CommandFact
{
    QString id;        ///< stable command id, e.g. "layer.toggleEditing"
    QString title;     ///< 中文 label
    QString description; ///< one-line description (tooltip level)
    QString category;
    QStringList keywords;
    QString shortcut;    ///< canonical binding text (may be empty)
    bool destructive = false;
    bool checkable = false;
};

class CommandCatalogSource
{
  public:
    virtual ~CommandCatalogSource() = default;
    /// All commands, in registry order (registry is already sorted by id).
    virtual QVector<CommandFact> commands() const = 0;
};

/// One operator fact snapshot (mirrors RSOperator's descriptive surface).
struct OperatorFact
{
    QString id;          ///< e.g. "rs:sar_speckle"
    QString displayName;
    QString group;
    QString description;
    QString determinismGrade; ///< "bit_exact" | "tolerance"
    QString memoryPolicy;     ///< memoryPolicyName()
    Json::Value schema;       ///< authoritative parameter/output schema
    Json::Value metadata;     ///< purpose/prerequisites/limitations/tags JSON
};

class OperatorCatalogSource
{
  public:
    virtual ~OperatorCatalogSource() = default;
    /// All operators, sorted by id.
    virtual QVector<OperatorFact> operators() const = 0;
};

} // namespace sicnu::help
