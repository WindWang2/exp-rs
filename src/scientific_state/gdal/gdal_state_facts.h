/***************************************************************************
  scientific_state/gdal_state_facts.h
  RS14-01 Scientific Data Passport — GDAL facts collector.

  Thin, single-purpose adapter: one READ-ONLY open plus metadata queries.
  Never scans pixels, never writes, never "fixes" metadata. Everything it
  observes is handed to the resolver as source-tagged facts
  (source tags: "gdal:<KEY>").
 ***************************************************************************/

#ifndef SICNU_SCIENTIFIC_STATE_GDAL_STATE_FACTS_H
#define SICNU_SCIENTIFIC_STATE_GDAL_STATE_FACTS_H

#include "scientific_state/state_facts.h"

#include <gdal_priv.h>

#include <cstddef>
#include <optional>
#include <string>

namespace sicnu::state
{

/// Hard cap on metadata items collected per scope (dataset or band).
inline constexpr std::size_t kMaxCollectedMetadataItems = 512;

/// Typed facts-collection failure: a machine-readable code (surfaced through
/// passports/bundles so "GDAL could not open" never collapses into "asset
/// missing"), the requested path, and bounded human detail from CPL.
/// Codes: "gdal_open_failed" | "gdal_facts_failed".
struct GdalFactsError
{
    std::string code;
    std::string path;
    std::string detail;

    bool empty() const { return code.empty(); }
};

/// Opens @p path read-only and collects dataset facts. Returns nullopt with
/// the typed @p error set when the file cannot be opened as a raster dataset.
std::optional<DatasetFacts> collectDatasetFacts( const std::string &path,
                                                 GdalFactsError *error );

/// String-error overload (same outcome; @p error carries
/// "<code>: <detail> (<path>)").
std::optional<DatasetFacts> collectDatasetFacts( const std::string &path,
                                                 std::string &error );

/// Collects facts from an already-open dataset (borrowed; not closed).
/// GDALAllRegister() must have run in the process.
std::optional<DatasetFacts> collectDatasetFacts( GDALDatasetH dataset,
                                                 const std::string &sourcePath,
                                                 std::string &error );

} // namespace sicnu::state

#endif // SICNU_SCIENTIFIC_STATE_GDAL_STATE_FACTS_H
