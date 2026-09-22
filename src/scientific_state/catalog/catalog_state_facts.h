/***************************************************************************
  scientific_state/catalog/catalog_state_facts.h
  RS14-01 Scientific Data Passport — catalog adapter (Qt-typed).

  Thin, side-effect-free mapping from the catalog's own value types
  (AssetSnapshot, DerivationRecord) into source-tagged passport facts.
  Lives behind a small static lib so Qt-linked targets can use it while the
  core stays Qt-free.
 ***************************************************************************/

#ifndef SICNU_SCIENTIFIC_STATE_CATALOG_STATE_FACTS_H
#define SICNU_SCIENTIFIC_STATE_CATALOG_STATE_FACTS_H

#include "scientific_state/state_facts.h"

#include <data/data_asset.h>
#include <data/derivation_record.h>

namespace sicnu::state
{

/// Projects a catalog snapshot into declarative catalog facts
/// (source tags: "catalog:AssetSnapshot", "catalog:structure").
CatalogFacts makeCatalogFacts( const sicnu::data::AssetSnapshot &snapshot );

/// Projects a derivation record into declarative provenance facts
/// (source tag: "catalog:DerivationRecord").
DerivationFacts makeDerivationFacts( const sicnu::data::DerivationRecord &record );

} // namespace sicnu::state

#endif // SICNU_SCIENTIFIC_STATE_CATALOG_STATE_FACTS_H
