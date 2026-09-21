/***************************************************************************
  scientific_state/state_facts.h
  RS14-01 Scientific Data Passport — source-tagged raw facts.

  Facts are what thin adapters (GDAL collector, catalog adapter, sidecar
  readers) hand to the resolver. They carry raw, undeclared-meaning
  observations plus the tag of where each observation came from; all
  vocabulary normalization and evidence reasoning lives in the resolver.

  Source tag conventions:
    "gdal:<KEY>"                 — dataset/band metadata item (declarative)
    "catalog:AssetSnapshot"      — catalog snapshot field (declarative)
    "catalog:structure"          — catalog raster structure mirror (declarative)
    "sensor_profile:<key>"       — declarative sensor registry (INFERENTIAL:
                                   truth about the product family, not about
                                   this particular file)
    "sidecar:<kind>"             — sidecar field (declarative)
 ***************************************************************************/

#ifndef SICNU_SCIENTIFIC_STATE_STATE_FACTS_H
#define SICNU_SCIENTIFIC_STATE_STATE_FACTS_H

#include "scientific_state/asset_state_types.h"

#include <string>
#include <utility>
#include <vector>

namespace sicnu::state
{

/// One raw observation of a logical field, tagged with its origin.
struct RawObservation
{
    std::string value;
    std::string source;
};

/// Multimap of metadata key → observations, preserving insertion order.
class MetadataItems
{
  public:
    void add( const std::string &key, const std::string &value, const std::string &source )
    {
        items_.emplace_back( key, RawObservation{ value, source } );
    }

    /// All observations recorded for @p key, in insertion order.
    std::vector<RawObservation> find( const std::string &key ) const
    {
        std::vector<RawObservation> out;
        for ( const auto &entry : items_ )
        {
            if ( entry.first == key )
                out.push_back( entry.second );
        }
        return out;
    }

    bool contains( const std::string &key ) const
    {
        for ( const auto &entry : items_ )
        {
            if ( entry.first == key )
                return true;
        }
        return false;
    }

    const std::vector<std::pair<std::string, RawObservation>> &all() const { return items_; }

  private:
    std::vector<std::pair<std::string, RawObservation>> items_;
};

/// Per-band raw facts as observed from one source (file or catalog mirror).
struct BandFacts
{
    int index = 0;  // 1-based
    std::string name;
    std::string dataType;
    MetadataItems metadata;
};

/// Dataset-level structural facts (single-source: the file itself).
struct DatasetFacts
{
    std::string sourcePath;
    std::string driverName;
    int bandCount = 0;
    MetadataItems metadata;  // dataset-level items (SICNU_* keys, acquisition, ...)
    std::vector<BandFacts> bands;
};

/// Band-level mirror as recorded in the catalog structure snapshot.
struct CatalogBandFacts
{
    int index = 0;
    std::string role;
    std::string dataType;
    bool hasNoData = false;
    double noDataValue = 0.0;
};

/// Catalog snapshot facts (declarative).
struct CatalogFacts
{
    std::string assetId;
    std::string revision;
    std::string displayName;
    std::string kind;          // asset-kind vocabulary ("raster", ...)
    std::string lifecycle;     // lifecycle vocabulary ("ready", ...)
    std::string persistence;
    std::string sourcePath;
    std::string acquisitionTimeIso;
    std::string acquisitionTimePrecision;
    int structureBandCount = 0;
    std::vector<CatalogBandFacts> bands;
    MetadataItems metadata;  // e.g. registered platform/sensor/product metadata
};

/// One band axis entry of a sensor profile (INFERENTIAL for a given file).
struct ProfileBand
{
    int index = 0;
    std::string role;
    bool hasWavelengthNm = false;
    double wavelengthNm = 0.0;
    bool hasFwhmNm = false;
    double fwhmNm = 0.0;
};

/// Sensor profile facts projected from the declarative sensor registry.
struct SensorProfileFacts
{
    std::string sensorKey;
    std::string platform;
    std::string instrument;
    Modality modality = Modality::Unknown;
    std::string productFamily;
    std::string qaVocabulary;
    std::vector<ProfileBand> bands;
};

/// Provenance facts projected from a DerivationRecord (declarative).
struct DerivationFacts
{
    std::string algorithmId;
    std::string algorithmVersion;
    struct Input
    {
        std::string assetId;
        std::string revision;
        std::vector<std::string> bandReferences;
        std::string valueDomain;
    };
    std::vector<Input> inputs;
    std::string completedAtUtc;
    std::string executionFingerprint;
    std::string softwareVersion;
    std::string workflowRef;
    bool cacheHit = false;
};

/// Model-derived product facts parsed from a model sidecar (declarative).
struct ModelSidecarFacts
{
    std::string modelKind;
    std::vector<std::string> labels;
    bool hasAccuracy = false;
    double accuracy = 0.0;
    std::string sidecarPath;
    std::string featureSchema;
};

} // namespace sicnu::state

#endif // SICNU_SCIENTIFIC_STATE_STATE_FACTS_H
