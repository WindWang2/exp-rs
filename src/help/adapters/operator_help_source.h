/***************************************************************************
 * operator_help_source.h — OperatorCatalogSource over RSOperatorRegistry
 *
 * Instantiates each registered operator through its factory and snapshots
 * schema()/metadata()/policy declarations. Kept in src/app/help (consumers
 * of both layers) so sicnu_help never links sicnu_operators.
 ***************************************************************************/
#pragma once

#include "help/help_catalog_source.h"

namespace sicnu::help::adapters
{

class OperatorHelpSource : public sicnu::help::OperatorCatalogSource
{
  public:
    QVector<sicnu::help::OperatorFact> operators() const override;
};

} // namespace sicnu::help::adapters
