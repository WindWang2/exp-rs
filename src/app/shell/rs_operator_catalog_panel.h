/***************************************************************************
 * rs_operator_catalog_panel.h — Workbench 10.0 rs: operator catalog (WP-F)
 *
 * One searchable surface over EVERY registered rs:-family operator — the
 * missing catalog half: the QGIS Processing Toolbox covers gdal:/otb:/
 * native: (with its own recent/favorites), while rs: operators were only
 * reachable by knowing their ids. Sources are the authorities only:
 * RSOperatorRegistry (ids + schemas) and CapabilityCatalog (modality/family
 * sidecars from the capability-knowledge layer). Recent + favorites persist
 * under QSettings (workbench/processing/*).
 *
 * Selection routes through the workflow session: the shell wraps the chosen
 * operator as a one-step workflow definition and opens it in the
 * TaskPanelHost — the single tool-run surface. No second execution path.
 ***************************************************************************/
#pragma once

#include <qgsdockwidget.h>

#include <QString>
#include <QStringList>
#include <QVector>

class QComboBox;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace sicnu::app
{

class RsOperatorCatalogPanel : public QgsDockWidget
{
    Q_OBJECT
  public:
    explicit RsOperatorCatalogPanel( QWidget *parent = nullptr );

    /// Bounded projection of the visible (filtered) entries, catalog order.
    QStringList visibleOperatorIds() const;
    /// The persisted recent list (settings-backed, capped).
    QStringList recentOperators() const;
    /// The persisted favorites (settings-backed).
    QStringList favoriteOperators() const;

  signals:
    /// The user asked to open an operator for parameter editing/running.
    void operatorSelected( const QString &operatorId );

  public slots:
    /// Re-reads the registry and re-applies the current filter.
    void reloadCatalog();
    /// Records an operator as recently used (settings + list order).
    void noteOperatorRun( const QString &operatorId );

  private slots:
    void applyFilter();
    void onContextRequested( const QPoint &pos );
    void onOpenClicked();

  private:
    struct Entry
    {
        QString id;
        QString displayName;
        QString description;
        QString family;    ///< capability sidecar family ("" unknown)
        QString modality;  ///< first declared input modality ("" unknown)
    };

    void rebuildEntries();
    void toggleFavorite( const QString &operatorId );

    QLineEdit *m_search = nullptr;
    QComboBox *m_modalityFilter = nullptr;
    QListWidget *m_list = nullptr;
    QPushButton *m_openBtn = nullptr;
    QVector<Entry> m_entries;
    QStringList m_recent;
    QStringList m_favorites;
};

} // namespace sicnu::app
