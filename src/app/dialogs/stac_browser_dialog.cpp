// stac_browser_dialog.cpp — STAC Catalog Browser Dialog
#include "stac_browser_dialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QUrlQuery>

#include <qgsmapcanvas.h>
#include <qgsrasterlayer.h>
#include <qgsproject.h>

StacBrowserDialog::StacBrowserDialog(QgsMapCanvas *canvas, QWidget *parent)
    : QDialog(parent)
    , m_canvas(canvas)
    , m_networkManager(new QNetworkAccessManager(this))
{
    setupUi();
    setWindowTitle(tr("STAC Catalog Browser"));
    resize(800, 600);

    connect(m_networkManager, &QNetworkAccessManager::finished,
            this, &StacBrowserDialog::onSearchCompleted);
}

void StacBrowserDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);

    // Search parameters
    auto *formLayout = new QFormLayout();
    m_endpointEdit = new QLineEdit(QStringLiteral("https://earth-search.aws.element84.com/v1"));
    m_collectionEdit = new QLineEdit(QStringLiteral("sentinel-2-l2a"));
    m_datetimeEdit = new QLineEdit;
    m_bboxEdit = new QLineEdit;
    m_bboxEdit->setPlaceholderText(tr("min_lon,min_lat,max_lon,max_lat"));

    formLayout->addRow(tr("STAC Endpoint:"), m_endpointEdit);
    formLayout->addRow(tr("Collection:"), m_collectionEdit);
    formLayout->addRow(tr("Datetime:"), m_datetimeEdit);
    formLayout->addRow(tr("BBox:"), m_bboxEdit);
    mainLayout->addLayout(formLayout);

    // Search button
    m_searchButton = new QPushButton(tr("Search"));
    connect(m_searchButton, &QPushButton::clicked, this, &StacBrowserDialog::searchCatalog);
    mainLayout->addWidget(m_searchButton);

    // Results table
    m_resultsTable = new QTableWidget;
    m_resultsTable->setColumnCount(4);
    m_resultsTable->setHorizontalHeaderLabels({tr("ID"), tr("Collection"), tr("Datetime"), tr("Assets")});
    m_resultsTable->horizontalHeader()->setStretchLastSection(true);
    m_resultsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_resultsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    mainLayout->addWidget(m_resultsTable);

    // Load button
    m_loadButton = new QPushButton(tr("Load Selected Asset"));
    m_loadButton->setEnabled(false);
    connect(m_loadButton, &QPushButton::clicked, this, &StacBrowserDialog::loadSelectedAsset);
    mainLayout->addWidget(m_loadButton);
}

void StacBrowserDialog::searchCatalog()
{
    QString endpoint = m_endpointEdit->text().trimmed();
    if (endpoint.isEmpty()) {
        QMessageBox::warning(this, tr("Error"), tr("STAC endpoint is required."));
        return;
    }

    QString url = endpoint + "/search";
    QUrlQuery query;
    if (!m_collectionEdit->text().trimmed().isEmpty())
        query.addQueryItem(QStringLiteral("collections"), m_collectionEdit->text().trimmed());
    if (!m_datetimeEdit->text().trimmed().isEmpty())
        query.addQueryItem(QStringLiteral("datetime"), m_datetimeEdit->text().trimmed());
    if (!m_bboxEdit->text().trimmed().isEmpty())
        query.addQueryItem(QStringLiteral("bbox"), m_bboxEdit->text().trimmed());

    QUrl requestUrl(url);
    requestUrl.setQuery(query);

    QNetworkRequest request(requestUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    m_searchButton->setEnabled(false);
    m_searchButton->setText(tr("Searching..."));
    m_networkManager->get(request);
}

void StacBrowserDialog::onSearchCompleted(QNetworkReply *reply)
{
    m_searchButton->setEnabled(true);
    m_searchButton->setText(tr("Search"));

    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        QMessageBox::warning(this, tr("Search Failed"), reply->errorString());
        return;
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &error);
    if (error.error != QJsonParseError::NoError) {
        QMessageBox::warning(this, tr("Parse Error"), error.errorString());
        return;
    }

    QJsonObject root = doc.object();
    QJsonArray features = root.value(QStringLiteral("features")).toArray();
    populateResults(features);
}

void StacBrowserDialog::populateResults(const QJsonArray &features)
{
    m_resultsTable->setRowCount(features.size());
    for (int i = 0; i < features.size(); ++i) {
        QJsonObject feature = features[i].toObject();
        m_resultsTable->setItem(i, 0, new QTableWidgetItem(feature.value(QStringLiteral("id")).toString()));
        m_resultsTable->setItem(i, 1, new QTableWidgetItem(feature.value(QStringLiteral("collection")).toString()));

        QJsonObject properties = feature.value(QStringLiteral("properties")).toObject();
        m_resultsTable->setItem(i, 2, new QTableWidgetItem(properties.value(QStringLiteral("datetime")).toString()));

        QJsonObject assets = feature.value(QStringLiteral("assets")).toObject();
        m_resultsTable->setItem(i, 3, new QTableWidgetItem(QString::number(assets.size()) + tr(" assets")));

        // Store assets data in the ID column for later retrieval
        m_resultsTable->item(i, 0)->setData(Qt::UserRole, assets);
    }

    m_loadButton->setEnabled(features.size() > 0);
}

void StacBrowserDialog::loadSelectedAsset()
{
    QList<QTableWidgetItem*> selected = m_resultsTable->selectedItems();
    if (selected.isEmpty()) {
        QMessageBox::information(this, tr("No Selection"), tr("Please select a dataset to load."));
        return;
    }

    int row = selected.first()->row();
    QJsonObject assets = m_resultsTable->item(row, 0)->data(Qt::UserRole).toJsonObject();

    // Try to find a COG asset (typically "visual" or first available)
    QString assetUrl;
    if (assets.contains(QStringLiteral("visual"))) {
        assetUrl = assets.value(QStringLiteral("visual")).toObject().value(QStringLiteral("href")).toString();
    } else if (assets.contains(QStringLiteral("overview"))) {
        assetUrl = assets.value(QStringLiteral("overview")).toObject().value(QStringLiteral("href")).toString();
    } else {
        // Use first available asset
        for (auto it = assets.begin(); it != assets.end(); ++it) {
            QJsonObject asset = it.value().toObject();
            QString href = asset.value(QStringLiteral("href")).toString();
            if (href.endsWith(QStringLiteral(".tif"), Qt::CaseInsensitive)) {
                assetUrl = href;
                break;
            }
        }
    }

    if (assetUrl.isEmpty()) {
        QMessageBox::warning(this, tr("No Asset"), tr("No suitable raster asset found in selected dataset."));
        return;
    }

    // Load as COG via GDAL vsicurl
    QString layerName = m_resultsTable->item(row, 0)->text();
    QgsRasterLayer *layer = new QgsRasterLayer(assetUrl, layerName, QStringLiteral("gdal"));
    if (layer->isValid()) {
        QgsProject::instance()->addMapLayer(layer);
        m_canvas->refresh();
        accept();
    } else {
        QMessageBox::warning(this, tr("Load Failed"), tr("Failed to load raster from: %1").arg(assetUrl));
        delete layer;
    }
}
