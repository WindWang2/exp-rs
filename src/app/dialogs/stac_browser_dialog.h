// stac_browser_dialog.h — STAC Catalog Browser Dialog
#pragma once

#include <QDialog>
#include <QUrl>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVariantList>
#include <QVariantMap>

class QgsMapCanvas;
class StacClient;

class StacBrowserDialog : public QDialog
{
    Q_OBJECT

public:
    explicit StacBrowserDialog(QgsMapCanvas *canvas, QWidget *parent = nullptr);

private slots:
    void searchCatalog();
    void onSearchCompleted(const QVariantList &features, const QString &error,
                           const QUrl &nextPage = QUrl());
    void loadSelectedAsset();

private:
    void setupUi();
    void populateResults(const QVariantList &features);

    QgsMapCanvas *m_canvas = nullptr;
    QLineEdit *m_endpointEdit = nullptr;
    QLineEdit *m_collectionEdit = nullptr;
    QLineEdit *m_datetimeEdit = nullptr;
    QLineEdit *m_bboxEdit = nullptr;
    QTableWidget *m_resultsTable = nullptr;
    QPushButton *m_searchButton = nullptr;
    QPushButton *m_moreButton = nullptr;  // STAC pagination (#634)
    QPushButton *m_loadButton = nullptr;
    StacClient *m_stacClient = nullptr;
    QList<QVariantMap> m_featureData;
};