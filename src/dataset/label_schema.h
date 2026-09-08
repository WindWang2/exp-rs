// label_schema.h — versioned label ontology (ADR 0135).
//
// A LabelClass is identified by its stable UUID + schema version; the string
// `code` is the interchange key; the row position in any UI or file is
// NEVER an identity. Hierarchies are parent-code trees validated acyclic;
// background/ignore/unknown flags carry the semantic weight evaluation
// needs. LabelMapping recodes between schemas as an EXPLICIT, named,
// versioned operation — implicit re-encoding is forbidden (goal §16).
#pragma once

#include "dataset_ids.h"
#include "dataset_types.h"

#include "../data/data_result.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

#include <optional>

namespace sicnu::dataset
{

/// Serialization schema version of label schema documents.
inline constexpr int kLabelSchemaSerializationVersion = 1;

class LabelClass
{
  public:
    LabelClass() = default;

    /// Stable UUID (never a row index).
    const QString &stableId() const { return m_stableId; }
    void setStableId( const QString &id ) { m_stableId = id; }
    /// Unique interchange code within one schema ("water", "lc:cropland").
    const QString &code() const { return m_code; }
    void setCode( const QString &code ) { m_code = code; }
    const QString &displayName() const { return m_displayName; }
    void setDisplayName( const QString &name ) { m_displayName = name; }
    const QString &description() const { return m_description; }
    void setDescription( const QString &text ) { m_description = text; }
    /// Parent code ("" = root). Hierarchy = taxonomy, validated acyclic.
    const QString &parentCode() const { return m_parentCode; }
    void setParentCode( const QString &code ) { m_parentCode = code; }
    /// "#rrggbb" (Qt-free; QtGui stays out of this layer).
    const QString &colorHex() const { return m_colorHex; }
    void setColorHex( const QString &color ) { m_colorHex = color; }

    bool isBackground() const { return m_background; }
    void setBackground( bool on ) { m_background = on; }
    bool isIgnore() const { return m_ignore; }
    void setIgnore( bool on ) { m_ignore = on; }
    bool isUnknown() const { return m_unknown; }
    void setUnknown( bool on ) { m_unknown = on; }

    const QStringList &aliases() const { return m_aliases; }
    QStringList &aliases() { return m_aliases; }
    const QJsonObject &metadata() const { return m_metadata; }
    QJsonObject &metadata() { return m_metadata; }
    /// Interop alias for legacy int-id vocabularies (e.g. RsClassDef ids);
    /// -1 = none. Identity NEVER derives from this value.
    int legacyIntId() const { return m_legacyIntId; }
    void setLegacyIntId( int id ) { m_legacyIntId = id; }

    QJsonObject toJson() const;
    static sicnu::data::Result<LabelClass> fromJson( const QJsonObject &json );

    friend bool operator==( const LabelClass &, const LabelClass & ) = default;

  private:
    QString m_stableId;
    QString m_code;
    QString m_displayName;
    QString m_description;
    QString m_parentCode;
    QString m_colorHex;
    bool m_background = false;
    bool m_ignore = false;
    bool m_unknown = false;
    QStringList m_aliases;
    QJsonObject m_metadata;
    int m_legacyIntId = -1;
};

class LabelSchema
{
  public:
    LabelSchema() = default;

    const QString &schemaId() const { return m_schemaId; }
    void setSchemaId( const QString &id ) { m_schemaId = id; }
    quint64 version() const { return m_version; }
    void setVersion( quint64 version ) { m_version = version; }
    const QString &name() const { return m_name; }
    void setName( const QString &name ) { m_name = name; }
    const QString &parentSchemaId() const { return m_parentSchemaId; }
    void setParentSchemaId( const QString &id ) { m_parentSchemaId = id; }
    const QString &description() const { return m_description; }
    void setDescription( const QString &text ) { m_description = text; }

    QVector<LabelClass> &classes() { return m_classes; }
    const QVector<LabelClass> &classes() const { return m_classes; }

    const LabelClass *classByCode( const QString &code ) const;
    const LabelClass *classByStableId( const QString &stableId ) const;

    /// Ancestor codes from the class upward (excluding itself), root first.
    QStringList ancestorsOf( const QString &code ) const;
    /// Direct + transitive descendant codes (excluding itself).
    QStringList descendantsOf( const QString &code ) const;
    /// Classes with no children — the set a flat classifier trains on.
    QStringList leafCodes() const;

    /// Structural validation: unique codes, unique stable ids, resolvable
    /// parents, acyclic hierarchy, sane flags. Diagnostics carry
    /// `dataset.label_schema_invalid` with the specific defect.
    sicnu::data::Result<void> validate() const;

    QJsonObject toJson() const;
    static sicnu::data::Result<LabelSchema> fromJson( const QJsonObject &json );

    friend bool operator==( const LabelSchema &, const LabelSchema & ) = default;

  private:
    QString m_schemaId;
    quint64 m_version = 1;
    QString m_parentSchemaId;
    QString m_name;
    QString m_description;
    QVector<LabelClass> m_classes;
};

/// One explicit recode rule: from-code → to-code under a target schema.
struct LabelMappingRule
{
    QString fromCode;
    QString toCode;
    friend bool operator==( const LabelMappingRule &, const LabelMappingRule & ) = default;
};

/// A named, versioned recode between label schemas. Applying a mapping is a
/// dataset-version event recorded in provenance — never a query-time
/// rewrite (ADR 0135).
class LabelMapping
{
  public:
    LabelMapping() = default;

    const QString &name() const { return m_name; }
    void setName( const QString &name ) { m_name = name; }
    const QString &fromSchemaId() const { return m_fromSchemaId; }
    void setFromSchemaId( const QString &id ) { m_fromSchemaId = id; }
    quint64 fromSchemaVersion() const { return m_fromSchemaVersion; }
    void setFromSchemaVersion( quint64 version ) { m_fromSchemaVersion = version; }
    const QString &toSchemaId() const { return m_toSchemaId; }
    void setToSchemaId( const QString &id ) { m_toSchemaId = id; }
    quint64 toSchemaVersion() const { return m_toSchemaVersion; }
    void setToSchemaVersion( quint64 version ) { m_toSchemaVersion = version; }

    QVector<LabelMappingRule> &rules() { return m_rules; }
    const QVector<LabelMappingRule> &rules() const { return m_rules; }

    /// Maps one code; nullopt when no rule covers it — callers decide
    /// whether unmapped codes are errors, drops or a fallback class; this
    /// function never guesses.
    std::optional<QString> map( const QString &code ) const;

    /// Totality check against @p from: every class code (except
    /// background/ignore/unknown-flagged ones) must have a rule. Unmapped
    /// codes are listed so the mapping author sees exactly what is missing.
    sicnu::data::Result<QStringList> covers( const LabelSchema &from ) const;

    QJsonObject toJson() const;
    static sicnu::data::Result<LabelMapping> fromJson( const QJsonObject &json );

    friend bool operator==( const LabelMapping &, const LabelMapping & ) = default;

  private:
    QString m_name;
    QString m_fromSchemaId;
    quint64 m_fromSchemaVersion = 1;
    QString m_toSchemaId;
    quint64 m_toSchemaVersion = 1;
    QVector<LabelMappingRule> m_rules;
};

} // namespace sicnu::dataset
