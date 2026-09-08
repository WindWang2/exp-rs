/***************************************************************************
 * command_registry.h — Professional Workbench 5.0 command surface
 *
 * One definition per capability: id, labels, icon, shortcut owner, category,
 * keywords, availability/checked predicates and a single handler. The
 * registry is a UI *projection* layer only — it never executes business
 * logic itself; handlers forward to the existing shell slots/services.
 *
 * Surfaces (ribbon, hidden menu host, context menus, command palette)
 * consume the same definition via action()/definition lookup, so a
 * capability keeps one handler, one availability contract and one shortcut
 * owner no matter how many places expose it.
 ***************************************************************************/
#pragma once

#include <QKeySequence>
#include <QList>
#include <QMap>
#include <QObject>
#include <QSet>

#include <functional>

#include "selection_context.h"

class QAction;

namespace sicnu::app
{

struct CommandDefinition
{
    /// Stable, dotted id (e.g. "project.open", "map.zoomIn", "rs.bandMath").
    QString id;
    /// User-facing label (中文, no "..." suffix — surfaces add ellipsis).
    QString title;
    /// Tooltip / palette subtitle.
    QString description;
    /// Resource alias under :/icons (e.g. "new_project").
    QString iconName;
    QKeySequence shortcut;      ///< canonical binding; registry enforces uniqueness
    QString category;           ///< grouping (ribbon tab / palette section)
    QStringList keywords;       ///< palette search terms (中/EN)
    bool checkable = false;
    bool destructive = false;   ///< confirm-on-trigger surfaces may consult
    /// Availability against the current selection context (default: true).
    std::function<bool( const SelectionContextSnapshot & )> availability;
    /// For checkable commands: current state (default: false).
    std::function<bool( const SelectionContextSnapshot & )> checkedState;
    /// Human reason when unavailable (default: ContextRules::unavailabilityReason).
    std::function<QString( const SelectionContextSnapshot & )> explain;
    /// The ONE execution path (forwards to an existing shell slot/service).
    std::function<void()> handler;
};

/**
 * Registry of CommandDefinitions with projection QActions whose enabled /
 * checked state follows the attached SelectionContext. Shortcut uniqueness
 * is enforced at registration (duplicate id or shortcut → rejected, warning
 * logged) — the machine-checkable form of the "no two actions claim one
 * binding" rule.
 */
class CommandRegistry : public QObject
{
    Q_OBJECT
  public:
    explicit CommandRegistry( QObject *parent = nullptr );

    /// Registers a definition; rejects empty id/handler and duplicates.
    /// @return false (with qWarning) when rejected; contract tests assert.
    bool registerCommand( CommandDefinition definition );

    const CommandDefinition *definition( const QString &id ) const;
    /// All definitions sorted by id (stable for tests/palette).
    QList<const CommandDefinition *> definitions() const;
    QStringList commandIds() const;
    int count() const { return m_commands.size(); }

    /// Projection QAction for @p id. Registry-owned and cached: repeated calls
    /// return the same action. Disabled/checked text follow the context.
    /// When @p installShortcut the canonical shortcut is set on the action —
    /// exactly ONE projection per registry may do this (enforced).
    QAction *action( const QString &id, bool installShortcut = false );

    /// Recompute enabled/checked for every projection (coalesced upstream by
    /// SelectionContext; surfaces may also call this after programmatic
    /// state changes that bypass the context).
    void refreshAll();

    /// Last unavailability reason computed for @p id (empty = available).
    QString unavailabilityReason( const QString &id ) const;

    /// Triggers @p id through its handler (palette / tests / agent surfaces).
    /// Returns false when the command is unknown or unavailable.
    bool trigger( const QString &id );

    /// Selection snapshot source; registry refreshes projections on change.
    void setSnapshotProvider( std::function<SelectionContextSnapshot()> provider );

  signals:
    void commandRegistered( const QString &id );
    /// Emitted from refreshAll() so surfaces can update derived chrome.
    void availabilityChanged();

  private:
    SelectionContextSnapshot currentSnapshot() const;
    void updateAction( const QString &id );

    QMap<QString, CommandDefinition> m_commands; // sorted by id
    QMap<QString, QAction *> m_actions;
    QSet<QString> m_shortcuts; // stringified non-empty canonical shortcuts
    QString m_shortcutOwner;   // projection id holding the installed shortcut
    std::function<SelectionContextSnapshot()> m_snapshotProvider;
};

} // namespace sicnu::app
