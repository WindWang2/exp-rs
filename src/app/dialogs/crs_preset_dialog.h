// src/app/dialogs/crs_preset_dialog.h
#pragma once

#include <QDialog>

class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QPushButton;

/**
 * Dialog for selecting a CRS from a list of presets organized by category.
 * Provides a tree view with category nodes, a search filter, and a details panel.
 */
class CrsPresetDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CrsPresetDialog( QWidget *parent = nullptr );

    /**
     * Returns the EPSG code of the selected CRS preset.
     * Returns -1 if no preset was selected.
     */
    int selectedEpsg() const;

    /**
     * Repopulates the tree, reflecting any recent CRS changes.
     */
    void refreshTree();

private slots:
    void onSearchTextChanged( const QString &text );
    void onTreeItemChanged( QTreeWidgetItem *current, QTreeWidgetItem *previous );
    void onTreeItemDoubleClicked( QTreeWidgetItem *item, int column );

private:
    void setupUi();
    void populateTree();
    void updateDetails( int epsgCode );
    void filterTree( const QString &text );

    QTreeWidget *m_treeWidget = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QLabel *m_nameLabel = nullptr;
    QLabel *m_epsgLabel = nullptr;
    QLabel *m_categoryLabel = nullptr;
    QLabel *m_descriptionLabel = nullptr;
    QLabel *m_wktLabel = nullptr;
    QPushButton *m_okButton = nullptr;
    QPushButton *m_cancelButton = nullptr;

    int m_selectedEpsg = -1;
};
