/***************************************************************************
 * help_registry.h — bounded registry/index of HelpDescriptors
 *
 * The registry is the single in-memory authority the help layer serves from.
 * It enforces:
 *   - id grammar (HelpId::isValid) and kind consistency;
 *   - duplicate rejection (first registration wins; a second is an error);
 *   - alias/deprecation registration (deprecated id → target, one hop);
 *   - reference validation (relatedIds / diagnosticIds / supersededBy must
 *     resolve, docRefs must point at tracked files — checked on demand);
 *   - deterministic iteration (sorted by id) for stable search/docs/tests.
 *
 * It never interprets business semantics: no availability evaluation, no
 * parameter validation, no error translation — the registry only stores and
 * validates the knowledge graph.
 ***************************************************************************/
#pragma once

#include "help/help_descriptor.h"

#include <QString>
#include <QStringList>

#include <QHash>
#include <QVector>

#include <memory>

namespace sicnu::help
{

class HelpRegistry
{
  public:
    HelpRegistry() = default;

    /// Registers @p descriptor. On rejection (bad id, kind mismatch, duplicate,
    /// unknown alias target) returns false and appends the reason to
    /// @p error when provided. Rejection is never silent in debug builds.
    bool registerDescriptor( HelpDescriptor descriptor, QString *error = nullptr );

    /// Inserts or replaces (by id) — used by providers that merge a derived
    /// descriptor with already-registered additive knowledge. Validation as
    /// registerDescriptor; an existing entry is replaced, not rejected.
    bool upsertDescriptor( HelpDescriptor descriptor, QString *error = nullptr );

    /// Registers an alias so legacy ids resolve to @p targetId. The alias is
    /// itself validated; target must be registered (or registered later —
    /// resolved lazily at query time; validateReferences() reports dangles).
    bool registerAlias( const QString &aliasId, const QString &targetId, QString *error = nullptr );

    /// Descriptor for @p id; follows at most one alias hop. nullptr when
    /// unknown. The returned pointer borrows from the registry.
    const HelpDescriptor *find( const QString &id ) const;

    /// All descriptors sorted by id (stable for tests/docs).
    QVector<const HelpDescriptor *> all() const;

    /// Descriptors of one kind, sorted by id.
    QVector<const HelpDescriptor *> byKind( HelpKind kind ) const;

    /// Alias map (deprecated id → target id), sorted by alias id.
    QList<QPair<QString, QString>> aliases() const;

    /// Count of real descriptors (aliases excluded).
    int count() const { return m_descriptors.size(); }
    int aliasCount() const { return m_aliases.size(); }

    /// Reference validation: returns one line per dangling reference
    /// ("operator.rs.x: related 'command.y' not registered"). Empty = clean.
    /// Cheap enough for per-registration debugging and drift tests.
    QStringList validateReferences() const;

    /// Merges @p other into this registry (used by tests and by the startup
    /// composition of derived + JSON descriptors). Duplicate ids are reported
    /// via @p errors, not overwritten.
    void mergeFrom( const HelpRegistry &other, QStringList *errors = nullptr );

  private:
    QHash<QString, HelpDescriptor> m_descriptors; // key: id
    QHash<QString, QString> m_aliases;            // alias id → target id
};

/// Process-wide registry instance. GUI startup composes it once (derived
/// descriptors + JSON knowledge); CLI/tests may build isolated registries.
HelpRegistry &globalHelpRegistry();

} // namespace sicnu::help
