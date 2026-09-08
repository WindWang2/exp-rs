/***************************************************************************
 * layer_sections.h — built-in layer inspector sections (Workbench 5.0 F)
 *
 * General: layer identity/type/validity/CRS/extent summary.
 * Metadata: provider metadata key/values (lazy — only when shown).
 *
 * Both read from the QgsMapLayer projection carried in the snapshot — the
 * authoritative source is the QGIS project/layers; no panel widget access.
 ***************************************************************************/
#pragma once

#include "inspector_host.h"

class QFormLayout;
class QLabel;

namespace sicnu::app
{

class LayerGeneralSection : public InspectorSection
{
    Q_OBJECT
  public:
    explicit LayerGeneralSection( QWidget *parent = nullptr );

    QString sectionId() const override { return QStringLiteral( "general" ); }
    QString title() const override { return tr( "常规" ); }
    int order() const override { return 0; }
    bool supports( const SelectionContextSnapshot &snapshot ) const override;
    void populate( const SelectionContextSnapshot &snapshot ) override;

  private:
    QLabel *m_summary = nullptr;
};

class LayerMetadataSection : public InspectorSection
{
    Q_OBJECT
  public:
    explicit LayerMetadataSection( QWidget *parent = nullptr );

    QString sectionId() const override { return QStringLiteral( "metadata" ); }
    QString title() const override { return tr( "元数据" ); }
    int order() const override { return 10; }
    bool supports( const SelectionContextSnapshot &snapshot ) const override;
    void populate( const SelectionContextSnapshot &snapshot ) override;
    void cancelPending() override;

  private:
    QLabel *m_body = nullptr;
};

} // namespace sicnu::app
