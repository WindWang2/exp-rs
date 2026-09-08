// dataset_ids.cpp — strong id implementation (shared macro expansion).
#include "dataset_ids.h"

#include <QUuid>

namespace sicnu::dataset
{

#define SICNU_DATASET_ID_IMPL( Name )                                                         \
    Name Name::generate()                                                                     \
    {                                                                                         \
        return Name( QUuid::createUuid().toString( QUuid::WithoutBraces ) );                   \
    }                                                                                         \
                                                                                              \
    std::optional<Name> Name::fromString( const QString &text )                               \
    {                                                                                         \
        const QUuid parsed = QUuid::fromString( text );                                        \
        if ( parsed.isNull() || text.isEmpty() )                                               \
            return std::nullopt;                                                               \
        /* Re-stringify so the stored form is always canonical (lowercase, no braces):         \
           two spellings of one uuid must not become two identities. */                        \
        return Name( parsed.toString( QUuid::WithoutBraces ) );                                \
    }                                                                                         \
                                                                                              \
    bool Name::isNull() const                                                                 \
    {                                                                                         \
        return m_value.isEmpty() || QUuid::fromString( m_value ).isNull();                     \
    }                                                                                         \
                                                                                              \
    QString Name::toString() const                                                            \
    {                                                                                         \
        return m_value;                                                                       \
    }                                                                                         \
                                                                                              \
    Name::Name( QString value )                                                               \
      : m_value( std::move( value ) )                                                         \
    {                                                                                         \
    }

SICNU_DATASET_ID_IMPL( DatasetId )
SICNU_DATASET_ID_IMPL( DatasetVersionId )
SICNU_DATASET_ID_IMPL( SampleId )
SICNU_DATASET_ID_IMPL( AnnotationId )
SICNU_DATASET_ID_IMPL( SplitManifestId )
SICNU_DATASET_ID_IMPL( LabelSchemaId )

#undef SICNU_DATASET_ID_IMPL

} // namespace sicnu::dataset
