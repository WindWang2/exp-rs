#include "preflight/rule.h"

namespace sicnu::preflight {

const SlotFactsResult *RuleFacts::slotByName( const std::string &name ) const
{
    for ( const auto &slot : slots )
        if ( slot.slot == name )
            return &slot.facts;
    return nullptr;
}

std::vector<const RuleFacts::Slot *> RuleFacts::slotsOfKind( const std::string &kind ) const
{
    std::vector<const Slot *> out;
    for ( const auto &slot : slots )
        if ( slot.facts.status == FactStatus::Available && slot.facts.facts.kind == kind )
            out.push_back( &slot );
    return out;
}

} // namespace sicnu::preflight
