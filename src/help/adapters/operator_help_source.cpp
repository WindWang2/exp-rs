/***************************************************************************
 * operator_help_source.cpp — RSOperatorRegistry → OperatorFact adapter
 ***************************************************************************/
#include "help/adapters/operator_help_source.h"

#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/rs/rs_operators_init.h"

#include <algorithm>

namespace sicnu::help::adapters
{

QVector<sicnu::help::OperatorFact> OperatorHelpSource::operators() const
{
    // Built-in registration is explicit (see rs_operators_init.h): a plugin/
    // shared-library boundary may have left the registry empty otherwise.
    sicnu::operators::rs::initBuiltinRsOperators();

    using sicnu::operators::RSOperator;
    using sicnu::operators::RSOperatorRegistry;

    QVector<sicnu::help::OperatorFact> out;
    const std::vector<std::string> names = RSOperatorRegistry::instance().operatorNames();
    out.reserve( static_cast<int>( names.size() ) );
    for ( const std::string &name : names ) {
        const std::unique_ptr<RSOperator> op = RSOperatorRegistry::instance().create( name );
        if ( !op )
            continue;

        sicnu::help::OperatorFact fact;
        fact.id = QString::fromStdString( op->name() );
        fact.displayName = QString::fromStdString( op->displayName() );
        fact.group = QString::fromStdString( op->group() );
        fact.description = QString::fromStdString( op->description() );
        fact.determinismGrade = QString::fromStdString( op->determinismGrade() );
        fact.memoryPolicy = QString::fromLatin1(
            sicnu::operators::memoryPolicyName( op->memoryPolicy() ) );
        fact.schema = op->schema();
        fact.metadata = op->metadata();
        out.push_back( fact );
    }
    std::sort( out.begin(), out.end(), []( const auto &a, const auto &b ) { return a.id < b.id; } );
    return out;
}

} // namespace sicnu::help::adapters
