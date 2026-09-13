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
    QString title() const override { return tr( "General" ); }
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
    QString title() const override { return tr( "Metadata" ); }
    int order() const override { return 10; }
    bool supports( const SelectionContextSnapshot &snapshot ) const override;
    void populate( const SelectionContextSnapshot &snapshot ) override;
    void cancelPending() override;

  private:
    QLabel *m_body = nullptr;
};

/// Inspector 2.0 (Milestone G): vector structure — fields, feature count,
/// CRS, editability and live selection count. Synchronous and bounded: the
/// field list is capped (first N fields) and counts come from provider
/// metadata, never a full scan.
class VectorStructureSection : public InspectorSection
{
    Q_OBJECT
  public:
    explicit VectorStructureSection( QWidget *parent = nullptr );

    QString sectionId() const override { return QStringLiteral( "vector" ); }
    QString title() const override { return tr( "Field" ); }
    int order() const override { return 20; }
    bool supports( const SelectionContextSnapshot &snapshot ) const override;
    void populate( const SelectionContextSnapshot &snapshot ) override;

  private:
    QLabel *m_body = nullptr;
};

/// Inspector 2.0 (Milestone G): SAR context — the detected product hint plus
/// whatever standard SAR facts the provider metadata exposes (polarisation,
/// orbit, incidence/look, calibration). Synchronous and bounded: metadata is
/// scanned once, in memory, for a fixed key whitelist.
class SarInfoSection : public InspectorSection
{
    Q_OBJECT
  public:
    explicit SarInfoSection( QWidget *parent = nullptr );

    QString sectionId() const override { return QStringLiteral( "sar" ); }
    QString title() const override { return tr( "SAR" ); }
    int order() const override { return 30; }
    bool supports( const SelectionContextSnapshot &snapshot ) const override;
    void populate( const SelectionContextSnapshot &snapshot ) override;

  private:
    QLabel *m_body = nullptr;
};

} // namespace sicnu::app
