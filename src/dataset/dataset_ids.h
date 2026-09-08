// dataset_ids.h — strong identities for the dataset foundation (ADR 0134).
//
// Every scientific entity gets a stable logical identity that never derives
// from a file path, a row index or a UI selection. Ids are UUIDv4 strings in
// canonical form; parsing is strict (a malformed id text is an error, never
// a silent null). Physical storage locations are locators carried elsewhere;
// content identity is the fingerprint (dataset_fingerprint.h), deliberately
// a separate concept: a dataset version keeps its identity when it is copied
// to another store, and two versions with identical content still have
// distinct identities (provenance tracks which one a run actually used).
#pragma once

#include <QString>

#include <optional>

namespace sicnu::dataset
{

#define SICNU_DATASET_ID_TYPE( Name )                                                         \
    class Name                                                                                \
    {                                                                                         \
      public:                                                                                 \
        Name() = default;                                                                     \
        static Name generate();                                                               \
        static std::optional<Name> fromString( const QString &text );                          \
        bool isNull() const;                                                                   \
        QString toString() const;                                                              \
        friend bool operator==( const Name &, const Name & ) = default;                        \
                                                                                              \
      private:                                                                                \
        explicit Name( QString value );                                                        \
        QString m_value;                                                                       \
    };

SICNU_DATASET_ID_TYPE( DatasetId )          ///< logical dataset (the evolving object)
SICNU_DATASET_ID_TYPE( DatasetVersionId )   ///< one immutable version state
SICNU_DATASET_ID_TYPE( SampleId )           ///< one sample within a dataset version
SICNU_DATASET_ID_TYPE( AnnotationId )       ///< one annotation (revision chain tip or interior)
SICNU_DATASET_ID_TYPE( SplitManifestId )    ///< one persisted split manifest
SICNU_DATASET_ID_TYPE( LabelSchemaId )      ///< one versioned label schema

#undef SICNU_DATASET_ID_TYPE

} // namespace sicnu::dataset
