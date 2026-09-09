/***************************************************************************
 * schema_form_builder.h  —  schema JSON → validated Qt form widgets
 *
 * Desktop Workbench UX 4.0 (Milestone B): the form-generation layer converts
 * the authoritative operator schema (or an AlgorithmDescriptor's
 * toInputSchema()) into parameter editors with inline validation. The schema
 * is the single source of truth for defaults, ranges and required fields —
 * the builder never invents its own defaults.
 *
 * Contract:
 *   rebuild(schema) → user edits → validate() issues → run/cancel decision
 *   values() collects a JSON object matching the schema; advanced fields
 *   (x-ui-advanced) collapse but stay reachable.
 *
 * Recognized x-ui-type hints (AlgorithmDescriptor ports emit these):
 *   raster | vector | table | bbox | crs | asset | model | color |
 *   expression | json — plus x-ui-widget / type/enum fallbacks.
 ***************************************************************************/
#pragma once

#include <QWidget>
#include <QStringList>
#include <QList>
#include <json/json.h>

class QVBoxLayout;
class QLabel;
class QPlainTextEdit;
class CrsSelector;

class SchemaFormBuilder : public QWidget
{
    Q_OBJECT
  public:
    /** One schema-violation found by validate(). */
    struct ValidationIssue
    {
      QString fieldName;
      QString message;
      bool isError = true;  // false = warning (does not block run)
    };

    explicit SchemaFormBuilder( QWidget *parent = nullptr );

    /** Build controls from RSOperator::schema() root object. */
    void rebuild( const Json::Value &schema );

    /** Collect current control values as a JSON object keyed by param name. */
    Json::Value values() const;

    /** Apply param values to existing controls (no rebuild). */
    void setValues( const Json::Value &params );

    /**
     * Validate current values against the schema: required fields, numeric
     * ranges, enum membership, minimum item counts. Inline marks update via
     * updateValidationUi(); errors block submission, warnings do not.
     */
    QList<ValidationIssue> validate() const;
    bool hasErrors() const;

    /**
     * Populate raster/vector combos with layer id/name pairs.
     * Stores layer id (or path) as item userData; raster combo remains
     * editable for paths.
     */
    void setRasterLayerChoices( const QStringList &layerIds,
                                const QStringList &layerNames );
    void setVectorLayerChoices( const QStringList &layerIds,
                                const QStringList &layerNames );

    /** Governed Data Asset choices (stable asset ids as item data). */
    void setAssetChoices( const QStringList &assetIds, const QStringList &assetNames );

    /** Registered model names (ModelCatalog). */
    void setModelChoices( const QStringList &modelNames );

    /** Re-populate all choice-based combos from the current choice sets. */
    void refreshChoices();

    /** Paint per-field error marks + the summary line for @p issues. */
    void applyValidationMarks( const QList<ValidationIssue> &issues );
    void clearValidationMarks();

  signals:
    void valuesChanged();
    /** Emitted after any re-validation with hasErrors(). */
    void validationChanged( bool hasBlockingErrors );

  public slots:
    /** Re-validate and refresh inline marks (already wired to valuesChanged). */
    void updateValidationUi();

  private:
    enum class FieldKind
    {
      RasterCombo,
      VectorCombo,
      AssetCombo,
      ModelCombo,
      Crs,
      OutputPath,
      Enum,
      Double,
      Integer,
      Boolean,
      String,
      Array,
      Json,
      Color,
    };

    enum class FieldGroup
    {
      Input,
      Output,
      Params,
      Advanced,
    };

    struct Field
    {
      QString name;
      FieldKind kind = FieldKind::String;
      FieldGroup group = FieldGroup::Params;
      QWidget *widget = nullptr;   // primary value widget
      class QComboBox *combo = nullptr;
      class QLineEdit *lineEdit = nullptr;
      class QDoubleSpinBox *doubleSpin = nullptr;
      class QSpinBox *spin = nullptr;
      class QCheckBox *check = nullptr;
      QPlainTextEdit *plainEdit = nullptr;
      CrsSelector *crsSelector = nullptr;
      Json::Value prop; // original schema property for array item typing
      // Milestone H (SchemaFormBuilder 3.0):
      QString unit;         ///< x-ui-unit — appended to the label
      QString recommended;  ///< x-ui-recommended — tooltip + accessible hint
      /// x-ui-visible-when: {param: expectedValue} — the field is hidden and
      /// excluded from values()/validate() while the condition does not hold.
      Json::Value visibleWhen;
      bool condHidden = false; ///< last computed visibility (see visibleWhen)
    };

    void clearFields();
    Field buildField( const QString &name, const Json::Value &prop );
    static FieldGroup classifyGroup( const QString &name, const Json::Value &prop );
    static FieldKind classifyKind( const QString &name, const Json::Value &prop );
    static QString fieldLabel( const QString &name, const Json::Value &prop );
    void connectValueSignals( Field &field );
    void refreshRasterCombos();
    void refreshComboChoices( FieldKind kind, const QStringList &ids,
                              const QStringList &names );
    QString readFieldValue( const Field &field ) const;
    void writeFieldValue( Field &field, const Json::Value &value );
    /// Milestone H: re-evaluate x-ui-visible-when dependencies; hidden fields
    /// are excluded from values()/validate() until their condition holds.
    void updateConditionalVisibility();

    QVBoxLayout *m_root = nullptr;
    QList<Field> m_fields;
    Json::Value m_schema;
    QStringList m_layerIds;
    QStringList m_layerNames;
    QStringList m_vectorIds;
    QStringList m_vectorNames;
    QStringList m_assetIds;
    QStringList m_assetNames;
    QStringList m_modelNames;
    QLabel *m_validationLabel = nullptr;
};
