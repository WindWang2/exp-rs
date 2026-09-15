/***************************************************************************
 * contextual_help_resolver.cpp — contextual guidance composition
 ***************************************************************************/
#include "app/help/contextual_help_resolver.h"

#include "app/help/availability_facts_adapter.h"
#include "help/help_presenter.h"
#include "help/help_registry.h"
#include "workbench/command_registry.h"
#include "workbench/selection_context.h"

namespace sicnu::app
{

ContextualGuidance ContextualHelpResolver::resolve( const SelectionContextSnapshot &snapshot,
                                                    const CommandRegistry *registry,
                                                    const QString &focusCommandId )
{
    ContextualGuidance guidance;

    const sicnu::help::HelpDescriptor *descriptor = nullptr;
    if ( !focusCommandId.isEmpty() ) {
        const QString helpId = QStringLiteral( "command.%1" ).arg( focusCommandId );
        descriptor = sicnu::help::globalHelpRegistry().find( helpId );
        guidance.detailHelpId = descriptor ? helpId : QString();
    }

    const sicnu::help::AvailabilityExplanation explanation =
        AvailabilityFactsAdapter::explain( snapshot, focusCommandId );
    guidance.available = explanation.available;

    if ( explanation.available ) {
        if ( descriptor )
            guidance.shortTip = sicnu::help::HelpPresenter::tooltip( *descriptor );
        return guidance;
    }

    guidance.unavailableReason = explanation.flatReason;
    guidance.unavailableReasonCode = explanation.reasonCode;
    guidance.suggestedCommandId = explanation.suggestedCommandId;
    if ( registry && !guidance.suggestedCommandId.isEmpty() ) {
        if ( const CommandDefinition *def = registry->definition( guidance.suggestedCommandId ) )
            guidance.suggestedCommandText = def->title;
    }
    // Short tip for a disabled command: the reason plus the way out.
    guidance.shortTip = guidance.unavailableReason;
    if ( !guidance.suggestedCommandText.isEmpty() )
        guidance.shortTip += QStringLiteral( " → " ) + guidance.suggestedCommandText;
    return guidance;
}

ContextualGuidance ContextualHelpResolver::resolveForSurface( const SelectionContextSnapshot &snapshot,
                                                              const QString &surfaceHelpId )
{
    ContextualGuidance guidance;
    guidance.detailHelpId = surfaceHelpId;

    const ContextRules::NextAction next = ContextRules::suggestedNextAction( snapshot );
    if ( !next.commandId.isEmpty() ) {
        guidance.suggestedCommandId = next.commandId;
        guidance.suggestedCommandText = next.text;
        guidance.shortTip = next.text;
    } else if ( !surfaceHelpId.isEmpty() ) {
        if ( const sicnu::help::HelpDescriptor *d = sicnu::help::globalHelpRegistry().find( surfaceHelpId ) )
            guidance.shortTip = sicnu::help::HelpPresenter::tooltip( *d );
    }
    return guidance;
}

} // namespace sicnu::app
