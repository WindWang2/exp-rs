/***************************************************************************
 * help_system_controller.h — GUI-side composition root & help entry point
 *
 * One controller per process:
 *   - composes globalHelpRegistry() from embedded content + the live
 *     CommandRegistry + RSOperatorRegistry (via the source adapters);
 *   - owns the F1 HelpEventFilter and the singleton HelpCenterDialog;
 *   - answers availability explanations for disabled commands
 *     (rules stay in ContextRules — see AvailabilityFactsAdapter).
 *
 * UI code asks this controller, never the registry directly, so presentation
 * policy (concise tooltips, rich What's This, status hints) stays in one
 * place.
 ***************************************************************************/
#pragma once

#include "help/availability_facts.h"
#include "help/help_registry.h"

#include <QObject>
#include <QString>

#include <functional>

class QAction;
class QWidget;

namespace sicnu::app
{

class CommandRegistry;
class HelpCenterDialog;
class HelpEventFilter;
// NB: struct, matching the definition in selection_context.h — a mismatched
// class-key changes MSVC name mangling and breaks the link.
struct SelectionContextSnapshot;

class HelpSystemController : public QObject
{
    Q_OBJECT
  public:
    /// Process-wide instance (created lazily).
    static HelpSystemController &instance();

    /// Composes the knowledge base once from the authoritative catalogs.
    /// Safe to call again; the second call is a no-op. @p errors collects
    /// composition problems (content drift) — empty in a healthy tree.
    void compose( const CommandRegistry &commandRegistry, QStringList *errors = nullptr );

    /// Full shell attachment: binds every registry command's projection
    /// actions to help (tooltip/status/What's This/helpId property) and keeps
    /// disabled actions explainable — while a command is unavailable its
    /// tooltip carries the availability facts instead of going silent.
    /// @p snapshotProvider supplies the current selection projection
    /// (SelectionContext::snapshot — rules remain in ContextRules).
    void attachCommandRegistry(
        CommandRegistry &commandRegistry,
        std::function<SelectionContextSnapshot()> snapshotProvider );

    /// F1 handling: installs the event filter on the application object.
    void installF1Filter();

    /// Opens (or raises) the Help Center anchored at @p helpId.
    void openHelpCenter( const QString &helpId = QString() );

    /// Availability explanation for a command against the current snapshot
    /// (title of the suggested command resolved via @p registry).
    sicnu::help::AvailabilityExplanation
    explainAvailability( const CommandRegistry &registry,
                         const SelectionContextSnapshot &snapshot,
                         const QString &commandId ) const;

    /// Installs tooltip / status tip / What's This / "why unavailable" hint
    /// on @p action from the descriptor behind @p helpId. Missing ids fall
    /// back to the action's own texts (never blank out existing help).
    void bindAction( QAction *action, const QString &helpId ) const;

    /// Installs What's This + optional tooltip on @p widget.
    void bindWidget( QWidget *widget, const QString &helpId ) const;

    /// Current workbench context for F1 fallback ("workbench.<id>").
    void setWorkbenchContext( const QString &workbenchHelpId );

    /// Search index over the composed registry (shared with Help Center).
    const sicnu::help::HelpRegistry &registry() const { return sicnu::help::globalHelpRegistry(); }

  private:
    HelpSystemController() = default;

    HelpEventFilter *m_f1Filter = nullptr;
    HelpCenterDialog *m_helpCenter = nullptr;
};

} // namespace sicnu::app
