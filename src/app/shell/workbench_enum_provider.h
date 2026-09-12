/***************************************************************************
 * workbench_enum_provider.h — Workbench 9.0 M6: production enum provider
 *
 * SchemaForm 4.0 shipped the injected-SchemaEnumProvider seam with a free-
 * text degradation contract, but no shell-side provider existed: every
 * `x-ui-enum-source` parameter degraded to free text and no producer
 * annotated one. This is the production resolution half — dynamic choices
 * read from the authoritative services (canvas layer lists, DataManager
 * assets, ModelCatalog models) at refresh time.
 *
 * Bounds: every source resolves at most kMaxChoices entries. A truncated
 * source says so in the last choice's label (the form renders it as-is);
 * an unavailable source resolves empty and the form degrades to free text
 * (8.0 contract unchanged). The provider never blocks: everything it reads
 * is an in-memory catalog snapshot.
 ***************************************************************************/
#pragma once

#include "schema_form_builder.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace sicnu::data
{
class DataManager;
}

namespace sicnu::app
{

/// Workbench 9.0 M6 (review B2): bind dynamic enum sources at the shell
/// boundary. Recursively annotates schema properties so x-ui-type model/asset
/// ports carry an x-ui-enum-source the form resolves through the production
/// provider; ports already annotated are respected. Returns the annotated
/// schema (same value, mutated in place and returned for chaining).
Json::Value applyEnumSourceAnnotations( Json::Value schema );

class WorkbenchEnumProvider : public QObject, public SchemaEnumProvider
{
    Q_OBJECT
  public:
    /// Hard per-source resolution cap. 200 keeps the RESOLVED list (and the
    /// rendered combo) bounded for any catalog size; sources without a
    /// native limit (AssetQuery today) are read as a snapshot and truncated
    /// here, with label building stopping at the cap.
    static constexpr int kMaxChoices = 200;

    explicit WorkbenchEnumProvider( QObject *parent = nullptr );

    /// Layer choices (canvas truth). The shell pushes these on layer churn —
    /// same lists TaskPanelHost::setRasterLayerChoices consumes — so the
    /// provider and the push channel can never disagree.
    void setRasterLayers( const QStringList &ids, const QStringList &names );
    void setVectorLayers( const QStringList &ids, const QStringList &names );

    /// Asset choices come straight from the authoritative store snapshot.
    void attachDataManager( sicnu::data::DataManager *manager );

    /// SchemaEnumProvider — source vocabulary:
    ///   "layers:raster" | "layers:vector" — pushed canvas layer lists
    ///   "assets"                          — DataManager asset snapshot
    ///   "models"                          — ModelCatalog model ids
    ///   anything else                     — empty (free-text degradation)
    QVector<Choice> choicesFor( const QString &sourceId,
                                const Json::Value &currentValues ) override;

  private:
    static QVector<Choice> capChoices( QVector<Choice> choices, const QString &sourceId );

    QStringList m_rasterIds;
    QStringList m_rasterNames;
    QStringList m_vectorIds;
    QStringList m_vectorNames;
    sicnu::data::DataManager *m_dataManager = nullptr;
};

} // namespace sicnu::app
