/***************************************************************************
 * schema_form_builder.h  —  schema JSON → Qt form widgets for task panel
 ***************************************************************************/
#pragma once

#include <QWidget>
#include <QStringList>
#include <json/json.h>

class QVBoxLayout;

/**
 * Builds a parameter form from an RSOperator::schema() root object.
 *
 * Sections (order): 输入 / 输出 / 参数 / 高级 (x-ui-advanced).
 * Heuristics map property type/format/name → appropriate Qt controls.
 */
class SchemaFormBuilder : public QWidget
{
    Q_OBJECT
  public:
    explicit SchemaFormBuilder( QWidget *parent = nullptr );

    /** Build controls from RSOperator::schema() root object. */
    void rebuild( const Json::Value &schema );

    /** Collect current control values as a JSON object keyed by param name. */
    Json::Value values() const;

    /** Apply param values to existing controls (no rebuild). */
    void setValues( const Json::Value &params );

    /**
     * Populate raster combos with layer id/name pairs.
     * Stores layer id (or path) as item userData; combo remains editable for paths.
     */
    void setRasterLayerChoices( const QStringList &layerIds,
                                const QStringList &layerNames );

  signals:
    void valuesChanged();

  private:
    enum class FieldKind
    {
      RasterCombo,
      OutputPath,
      Enum,
      Double,
      Integer,
      Boolean,
      String,
      Array,
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
      Json::Value prop; // original schema property for array item typing
    };

    void clearFields();
    Field buildField( const QString &name, const Json::Value &prop );
    static FieldGroup classifyGroup( const QString &name, const Json::Value &prop );
    static FieldKind classifyKind( const QString &name, const Json::Value &prop );
    static QString fieldLabel( const QString &name, const Json::Value &prop );
    void connectValueSignals( Field &field );
    void refreshRasterCombos();
    QString readFieldValue( const Field &field ) const;
    void writeFieldValue( Field &field, const Json::Value &value );

    QVBoxLayout *m_root = nullptr;
    QList<Field> m_fields;
    QStringList m_layerIds;
    QStringList m_layerNames;
};
