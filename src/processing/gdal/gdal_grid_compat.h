// src/processing/gdal/gdal_grid_compat.h — shared raster-grid builder
#pragma once

#include <QVector>

#include <array>
#include <optional>

class GdalDatasetWrapper;

namespace sicnu::data
{
struct RasterGrid;
}

namespace sicnu::processing
{

/**
 * Build a `data::RasterGrid` from an open GdalDatasetWrapper — the
 * operator-side grid description consumed by the shared grid-compatibility
 * service (ADR-0065 / A4). Shared by every operator that must preflight its
 * inputs against a reference grid (change detection, apply-mask,
 * post-classification comparison, ...) instead of re-deriving the fields.
 */
data::RasterGrid gridFromDataset( const GdalDatasetWrapper &ds );

} // namespace sicnu::processing
