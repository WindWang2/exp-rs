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
 *
 * SchemaForm 4.0 (Professional Workbench 8.0) additions — all schema-driven,
 * never per-operator:
 *   - nested objects: a property with type:"object" AND "properties" becomes
 *     a recursive sub-group; values() nests under the key, validate()
 *     honors the nested "required" list; a non-required group whose fields
 *     are all empty is treated as absent (optional group).
 *   - object arrays: type:"array" whose items declare an object schema
 *     become repeatable item editors (add/remove, minItems/maxItems gated,
 *     import bounded by kMaxObjectArrayItems with honest truncation).
 *   - dynamic enum sources: x-ui-enum-source resolves through the injected
 *     SchemaEnumProvider at rebuild and refreshChoices() time (long-lived
 *     forms re-query instead of going stale). Without a provider (or with an
 *     unknown source id) the combo degrades to editable free text with a
 *     visible hint — never a silent dead list.
 *   - async value checks: x-ui-check ("path_exists") runs on the bounded
 *     RsScanPool with generation cancellation; results are non-blocking
 *     widget marks and survive neither a newer rebuild nor destruction.
 *   - accessibility: every editor (nested included) carries
 *     accessibleName (schema label) + accessibleDescription (tooltip).
 ***************************************************************************/
#pragma once

#include <QWidget>
#include <QStringList>
#include <QList>
#include <QVector>
#include <json/json.h>

class QVBoxLayout;
class QFormLayout;
class QLabel;
class QPlainTextEdit;
class QThreadPool;
class CrsSelector;

/// Source of dynamic enum choices (x-ui-enum-source). Implemented by the
/// shell over authoritative services (DataManager, ModelCatalog, views);
/// the form never derives choices from widget state or operator identity.
class SchemaEnumProvider
{
  public:
    struct Choice
    {
        QString id;    ///< value stored/collected for the parameter
        QString label; ///< user-facing text (id used when empty)
    };
    virtual ~SchemaEnumProvider() = default;
    /// Choices for @p sourceId. @p currentValues is the form's current
    /// values() snapshot so context-dependent sources (e.g. bands of the
    /// selected raster) can resolve. Empty = "no choices available" — the
    /// form degrades to editable free text.
    virtual QVector<Choice> choicesFor( const QString &sourceId,
                                        const Json::Value &currentValues ) = 0;
};

class SchemaFormBuilder : public QWidget
{
    Q_OBJECT
  public:
    /** One schema-violation found by validate(). */
    struct ValidationIssue
    {
      QString fieldName;  ///< dotted path ("clip.extent" / "points.0.lat")
      QString message;
      bool isError = true;  // false = warning (does not block run)
    };

    /// Safety bound for object-array item import when the schema carries no
    /// maxItems: past this, setValues truncates honestly (visible hint).
    static constexpr int kMaxObjectArrayItems = 256;
    /// Recursion cap for nested object schemas (deeper levels degrade to the
    /// JSON text editor — same behavior as SchemaForm 3.0).
    static constexpr int kMaxObjectDepth = 4;

    explicit SchemaFormBuilder( QWidget *parent = nullptr );
    ~SchemaFormBuilder() override;

    /**
     * Unified Help 6.0: operator id ("rs:sar_speckle") whose parameter help
     * (unit/recommended/trade-off from data/help) enriches field tooltips.
     * Purely additive presentation — schema facts stay authoritative.
     */
    void setHelpContext( const QString &operatorId );

    /** Build controls from RSOperator::schema() root object. */
    void rebuild( const Json::Value &schema );

    /** Collect current control values as a JSON object keyed by param name. */
    Json::Value values() const;

    /** Apply param values to existing controls (no rebuild). */
    void setValues( const Json::Value &params );

    /**
     * Validate current values against the schema: required fields, numeric
     * ranges, enum membership, minimum item counts, nested required lists.
     * Inline marks update via updateValidationUi(); errors block submission,
     * warnings do not.
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

    /**
     * SchemaForm 4.0: dynamic enum choices. The provider is not owned; the
     * caller must keep it alive (or set nullptr) for the form's lifetime.
     */
    void setEnumProvider( SchemaEnumProvider *provider );

    /**
     * Async check worker pool override (tests). Default: the bounded
     * RsScanPool. Not owned; pass nullptr to restore the default.
     */
    void setCheckPool( QThreadPool *pool );

    /**
     * Re-run async value checks immediately (setValues blocks widget
     * signals, so the debounced auto-run never fires for programmatic
     * value application — hosts and tests call this to force a pass).
     */
    void runAsyncChecksNow();

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
      Object,      ///< 4.0: nested object with its own properties
      ObjectArray, ///< 4.0: repeatable object item editors
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
      QString path;         ///< dotted path from the schema root (== name at top level)
      int depth = 0;        ///< nesting depth of this field's schema object
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
      // SchemaForm 4.0:
      QVector<Field> children; ///< Object: nested schema fields
      QString enumSource;      ///< x-ui-enum-source (dynamic combo)
      QStringList checks;      ///< x-ui-check entries ("path_exists")
      bool enumSourceResolved = true; ///< false = provider missing/unknown
      QLabel *rowLabel = nullptr; ///< label of this field's row (conditional visibility)
      // ObjectArray runtime state (widgets owned by arrayHost's layout):
      QWidget *arrayHost = nullptr;
      class QVBoxLayout *arrayLayout = nullptr;
      QLabel *arrayHint = nullptr;        ///< truncation / bounds hint line
      QVector<QVector<Field>> arrayItems; ///< live item editors
    };

    void clearFields();
    Field buildField( const QString &path, const Json::Value &prop, int depth );
    /// 4.0: builds the child fields of one object schema (nested object or
    /// object-array item) into @p form, parented to @p box; returns them in
    /// schema (x-ui-order) order.
    QVector<Field> buildChildFields( const QString &basePath,
                                     const Json::Value &objectProp,
                                     int depth, QWidget *box, QFormLayout *form );
    /// 4.0: recursive validation over nested structures. @p required is the
    /// owning object's "required" list.
    void validateFields( const QVector<Field> &fields, const QStringList &required,
                         QList<ValidationIssue> &issues ) const;
    /// 4.0: recursive error-mark pass; marks are matched by dotted path.
    void applyMarksFields( const QVector<Field> &fields,
                           const QList<ValidationIssue> &issues ) const;
    /// 4.0: mutable/const lookup by dotted path (async check delivery, array
    /// button handlers navigate by path — never by captured Field reference,
    /// which would dangle across m_fields reallocation).
    Field *findFieldMutable( QVector<Field> &fields, const QString &path );
    /// 4.0: whether an optional object group counts as "in use" (any visible
    /// child carries a value; unchecked boxes / empty arrays count as
    /// untouched — the Qt default convention).
    bool groupTouched( const QVector<Field> &fields ) const;
    /// 4.0: schema properties in x-ui-order (stable name tiebreak).
    static QVector<QPair<QString, Json::Value>> orderedProperties( const Json::Value &objectProp );
    /// 4.0: recursive collection/writing helpers shared by values()/setValues().
    void collectFields( const QVector<Field> &fields, Json::Value &out ) const;
    void applyFields( const QVector<Field> &fields, const Json::Value &params );
    void applyParameterHelp( Field &field, const QString &label );
    static FieldGroup classifyGroup( const QString &name, const Json::Value &prop );
    static FieldKind classifyKind( const QString &name, const Json::Value &prop, int depth );
    static QString fieldLabel( const QString &name, const Json::Value &prop );
    void connectValueSignals( Field &field );
    void refreshRasterCombos();
    void refreshComboChoices( FieldKind kind, const QStringList &ids,
                              const QStringList &names );
    /// 4.0: repopulate dynamic-enum combos from the provider (selection kept).
    void refreshEnumSources();
    QString readFieldValue( const Field &field ) const;
    void writeFieldValue( Field &field, const Json::Value &value );
    /// Milestone H: re-evaluate x-ui-visible-when dependencies; hidden fields
    /// are excluded from values()/validate() until their condition holds.
    void updateConditionalVisibility();
    void updateConditionalVisibility( QVector<Field> &fields );
    /// Review L #5: canonical tooltip text (schema description + the
    /// x-ui-recommended hint) shared by rebuild and validation-mark restore.
    QString tooltipFor( const Field &field ) const;
    /// 4.0: dotted-path lookup over all levels (top level first).
    QString leafValueText( const QVector<Field> &fields, const QString &name ) const;
    // ObjectArray item editors.
    QWidget *buildArrayItemRow( Field &field, int index );
    void appendArrayItem( Field &field, bool emitChange );
    void removeArrayItem( Field &field, int index );
    void updateArrayBoundsUi( Field &field );
    void clearArrayItems( Field &field );
    // Signal suppression for setValues (recursive over nested children).
    static void setFieldsSignalsBlocked( QVector<Field> &fields, bool blocked );
    // Async x-ui-check machinery (bounded pool + generation cancellation).
    void scheduleAsyncChecks();
    void applyAsyncCheckResult( quint64 generation, const QString &path,
                                const QString &check, bool ok );
    static QStringList parseChecks( const Json::Value &prop );

    QVBoxLayout *m_root = nullptr;
    QVector<Field> m_fields;
    Json::Value m_schema;
    QString m_helpOperatorId;
    QStringList m_layerIds;
    QStringList m_layerNames;
    QStringList m_vectorIds;
    QStringList m_vectorNames;
    QStringList m_assetIds;
    QStringList m_assetNames;
    QStringList m_modelNames;
    QLabel *m_validationLabel = nullptr;
    SchemaEnumProvider *m_enumProvider = nullptr;
    QThreadPool *m_checkPool = nullptr; // null = RsScanPool default
    class QTimer *m_checkDebounce = nullptr;
    quint64 m_checkGeneration = 0;
};
