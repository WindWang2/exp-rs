/***************************************************************************
  geospatial/products/cn_product_adapters.h
  Remote Sensing I/O Foundation 5.0 — ProductAdapter implementations for the
  Chinese satellite families (GF-1/2/6 PMS/WFV, ZY-3 TLC/NAD/FWD/BWD,
  HJ-1A/1B CCD), ADR 0146.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_CN_PRODUCT_ADAPTERS_H
#define SICNU_GEOSPATIAL_CN_PRODUCT_ADAPTERS_H

#include "geospatial/products/product_registry.h"

#include <memory>

namespace sicnu::geo
{

/// ProductAdapter instances for the CN families, registered into the
/// ProductAdapterRegistry (first-match ordering keeps them ahead of the
/// GenericRaster fallback).
std::unique_ptr<ProductAdapter> makeGaofenAdapter();
std::unique_ptr<ProductAdapter> makeZy3Adapter();
std::unique_ptr<ProductAdapter> makeHjAdapter();

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_CN_PRODUCT_ADAPTERS_H
