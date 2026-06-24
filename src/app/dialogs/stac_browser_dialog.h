// stac_browser_dialog.h — STAC Catalog Browser Dialog
#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>

class QgsMapCanvas;

/**
 * Dialog for browsing STAC (SpatioTemporal Asset Catalog) catalogs.
 * Allows users to search for remote sensing datasets by collection,
 * datetime, and spatial extent, then load COG assets directly.
 */
class StacBrowserDialog : public QDialog
{
    Q_OBJECT

public:
    explicit StacBrowserDialog(QgsMapCanvas *canvas, QWidget *parent = nullptr);

private slots:
    void searchCatalog();
    void onSearchCompleted(QNetworkReply *reply);
    void loadSelectedAsset();

private:
    void setupUi();
    void populateResults(const QJsonArray &features);

    QgsMapCanvas *m_canvas = nullptr;
    QLineEdit *m_endpointEdit = nullptr;
    QLineEdit *m_collectionEdit = nullptr;
    QLineEdit *m_datetimeEdit = nullptr;
    QLineEdit *m_bboxEdit = nullptr;
    QTableWidget *m_resultsTable = nullptr;
    QPushButton *m_searchButton = nullptr;
    QPushButton *m_loadButton = nullptr;
    QNetworkAccessManager *m_networkManager = nullptr;
};
