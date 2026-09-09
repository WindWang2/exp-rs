/***************************************************************************
 * help_system_controller.cpp — controller implementation
 ***************************************************************************/
#include "app/help/help_system_controller.h"

#include "app/help/availability_facts_adapter.h"
#include "app/help/command_help_source.h"
#include "app/help/help_center_dialog.h"
#include "app/help/help_event_filter.h"
#include "help/adapters/operator_help_source.h"
#include "workbench/command_registry.h"
#include "workbench/selection_context.h"

#include <help/help_composition.h>
#include <help/help_presenter.h>

#include <QAction>
#include <QApplication>
#include <QWidget>

#include <QPointer>

namespace sicnu::app
{

HelpSystemController &HelpSystemController::instance()
{
    static HelpSystemController controller;
    return controller;
}

void HelpSystemController::compose( const CommandRegistry &commandRegistry, QStringList *errors )
{
    // idempotent composition: the registry content is deterministic, so a
    // second call would produce identical descriptors; skip the work.
    if ( sicnu::help::globalHelpRegistry().count() > 0 )
        return;

    const CommandHelpSource commandSource( commandRegistry );
    const sicnu::help::adapters::OperatorHelpSource operatorSource;
    const sicnu::help::CompositionReport report =
        sicnu::help::composeHelpSystem( sicnu::help::globalHelpRegistry(), &commandSource, &operatorSource );
    if ( errors ) {
        *errors = report.errors;
        for ( const QString &dangling : report.dangling )
            errors->append( QStringLiteral( "dangling reference: %1" ).arg( dangling ) );
    }
#ifndef NDEBUG
    for ( const QString &error : report.errors )
        qWarning( "help composition: %s", qPrintable( error ) );
#endif
}

void HelpSystemController::installF1Filter()
{
    if ( m_f1Filter )
        return;
    m_f1Filter = new HelpEventFilter( this );
    if ( qApp )
        qApp->installEventFilter( m_f1Filter );
    connect( m_f1Filter, &HelpEventFilter::helpRequested, this,
             [this]( const QString &helpId ) { openHelpCenter( helpId ); } );
}

void HelpSystemController::openHelpCenter( const QString &helpId )
{
    // F1 while the Help Center itself has focus: keep the user's place
    // instead of navigating away to the workbench fallback topic.
    if ( m_helpCenter && QApplication::activeWindow() == m_helpCenter ) {
        m_helpCenter->raise();
        return;
    }
    // A parentless modeless window opened over an app-modal dialog would be
    // blocked from all input while sitting on screen — suppress until the
    // modal closes.
    if ( QApplication::activeModalWidget() != nullptr )
        return;

    if ( !m_helpCenter ) {
        m_helpCenter = new HelpCenterDialog( nullptr );
        m_helpCenter->setAttribute( Qt::WA_DeleteOnClose );
    }
    if ( helpId.isEmpty() )
        m_helpCenter->show();
    else
        m_helpCenter->navigateTo( helpId );
    m_helpCenter->raise();
    m_helpCenter->activateWindow();
}

sicnu::help::AvailabilityExplanation HelpSystemController::explainAvailability(
    const CommandRegistry &registry, const SelectionContextSnapshot &snapshot,
    const QString &commandId ) const
{
    sicnu::help::AvailabilityExplanation explanation =
        AvailabilityFactsAdapter::explain( snapshot, commandId );

    // resolve the suggested action's display title via the registry
    if ( !explanation.available && !explanation.suggestedCommandId.isEmpty() ) {
        if ( const CommandDefinition *def = registry.definition( explanation.suggestedCommandId ) )
            explanation.suggestedCommandTitle = def->title;
    }
    return explanation;
}

void HelpSystemController::attachCommandRegistry(
    CommandRegistry &commandRegistry,
    std::function<SelectionContextSnapshot()> snapshotProvider )
{
    // Bind help texts on every projection action now; the registry keeps the
    // actions alive, so later calls to action(id) return the same instance.
    for ( const QString &id : commandRegistry.commandIds() ) {
        const QString helpId = QStringLiteral( "command.%1" ).arg( id );
        bindAction( commandRegistry.action( id ), helpId );
    }

    // While a command is unavailable, its tooltip explains why (facts view).
    // When it becomes available again the bound tooltip is restored.
    CommandRegistry *registryPointer = &commandRegistry;
    auto refreshAvailability = [this, registryPointer, snapshotProvider]() {
        CommandRegistry &registry = *registryPointer;
        const SelectionContextSnapshot snapshot = snapshotProvider();
        for ( const QString &id : registry.commandIds() ) {
            QAction *action = registry.action( id );
            if ( !action )
                continue;
            const sicnu::help::HelpDescriptor *d = sicnu::help::globalHelpRegistry().find(
                QStringLiteral( "command.%1" ).arg( id ) );
            const QString boundTooltip = d ? sicnu::help::HelpPresenter::tooltip( *d ) : action->toolTip();
            if ( action->isEnabled() ) {
                action->setToolTip( boundTooltip );
                continue;
            }
            const sicnu::help::AvailabilityExplanation explanation =
                explainAvailability( registry, snapshot, id );
            QString tooltip = boundTooltip;
            if ( !explanation.toConciseLine().isEmpty() )
                tooltip += QStringLiteral( "\n⛔ %1" ).arg( explanation.toConciseLine() );
            action->setToolTip( tooltip );
        }
    };
    connect( &commandRegistry, &CommandRegistry::availabilityChanged, this, refreshAvailability );
    refreshAvailability();
}

void HelpSystemController::bindAction( QAction *action, const QString &helpId ) const
{
    if ( !action )
        return;
    const sicnu::help::HelpDescriptor *d = sicnu::help::globalHelpRegistry().find( helpId );
    if ( !d ) {
        action->setProperty( "helpId", helpId ); // F1 still resolves once registered
        return;
    }
    action->setToolTip( sicnu::help::HelpPresenter::tooltip( *d ) );
    action->setStatusTip( sicnu::help::HelpPresenter::statusTip( *d ) );
    action->setWhatsThis( sicnu::help::HelpPresenter::whatsThis( *d ) );
    action->setProperty( "helpId", helpId );
}

void HelpSystemController::bindWidget( QWidget *widget, const QString &helpId ) const
{
    if ( !widget )
        return;
    if ( const sicnu::help::HelpDescriptor *d = sicnu::help::globalHelpRegistry().find( helpId ) )
        widget->setWhatsThis( sicnu::help::HelpPresenter::whatsThis( *d ) );
    widget->setProperty( "helpId", helpId );
}

void HelpSystemController::setWorkbenchContext( const QString &workbenchHelpId )
{
    if ( m_f1Filter )
        m_f1Filter->setWorkbenchContext( workbenchHelpId );
}

} // namespace sicnu::app
