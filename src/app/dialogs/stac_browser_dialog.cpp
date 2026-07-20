// stac_browser_dialog.cpp — STAC Catalog Browser Dialog
#include "stac_browser_dialog.h"
#include "agent/stac_client.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>

#include <qgsmapcanvas.h>
#include <qgsrasterlayer.h>
#include <qgsproject.h>

StacBrowserDialog::StacBrowserDialog(QgsMapCanvas *canvas, QWidget *parent)
    : QDialog(parent)
    , m_canvas(canvas)
    , m_stacClient(new StacClient(this))
{
    setupUi();
    setWindowTitle(tr("STAC Catalog Browser"));
    resize(800, 600);

    connect(m_stacClient, &StacClient::searchCompleted,
            this, &StacBrowserDialog::onSearchCompleted);
}

void StacBrowserDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);

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

    m_searchButton = new QPushButton(tr("Search"));
    connect(m_searchButton, &QPushButton::clicked, this, &StacBrowserDialog::searchCatalog);
    mainLayout->addWidget(m_searchButton);

    m_resultsTable = new QTableWidget;
    m_resultsTable->setColumnCount(4);
    m_resultsTable->setHorizontalHeaderLabels({tr("ID"), tr("Collection"), tr("Datetime"), tr("Assets")});
    m_resultsTable->horizontalHeader()->setStretchLastSection(true);
    m_resultsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_resultsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    mainLayout->addWidget(m_resultsTable);

    m_loadButton = new QPushButton(tr("Load Selected Asset"));
    m_loadButton->setEnabled(false);
    connect(m_loadButton, &QPushButton::clicked, this, &StacBrowserDialog::loadSelectedAsset);
    mainLayout->addWidget(m_loadButton);
}

void StacBrowserDialog::searchCatalog()
{
    const QString endpoint = m_endpointEdit->text().trimmed();
    if (endpoint.isEmpty()) {
        QMessageBox::warning(this, tr("Error"), tr("STAC endpoint is required."));
        return;
    }

    QStringList bbox;
    const QString bboxText = m_bboxEdit->text().trimmed();
    if (!bboxText.isEmpty())
        bbox = bboxText.split(QLatin1Char(','));

    m_searchButton->setEnabled(false);
    m_searchButton->setText(tr("Searching..."));
    m_stacClient->search(endpoint, m_collectionEdit->text().trimmed(),
                         m_datetimeEdit->text().trimmed(), bbox);
}

void StacBrowserDialog::onSearchCompleted(const QVariantList &features, const QString &error)
{
    m_searchButton->setEnabled(true);
    m_searchButton->setText(tr("Search"));

    if (!error.isEmpty()) {
        QMessageBox::warning(this, tr("Search Failed"), error);
        return;
    }

    populateResults(features);
}

void StacBrowserDialog::populateResults(const QVariantList &features)
{
    m_resultsTable->setRowCount(features.size());
    m_featureData.clear();

    for (int row = 0; row < features.size(); ++row) {
        const QVariantMap feature = features[row].toMap();
        m_featureData.append(feature);

        const QString id = feature.value(QStringLiteral("id")).toString();
        const QVariantMap props = feature.value(QStringLiteral("properties")).toMap();
        const QString datetime = props.value(QStringLiteral("datetime")).toString();
        const QVariantMap assets = feature.value(QStringLiteral("assets")).toMap();

        m_resultsTable->setItem(row, 0, new QTableWidgetItem(id));
        m_resultsTable->setItem(row, 1, new QTableWidgetItem(m_collectionEdit->text()));
        m_resultsTable->setItem(row, 2, new QTableWidgetItem(datetime));
        m_resultsTable->setItem(row, 3, new QTableWidgetItem(QString::number(assets.size())));
    }

    m_loadButton->setEnabled(!features.isEmpty());
}

void StacBrowserDialog::loadSelectedAsset()
{
    const int row = m_resultsTable->currentRow();
    if (row < 0 || row >= m_featureData.size()) {
        QMessageBox::warning(this, tr("Error"), tr("Select a STAC item first."));
        return;
    }

    const QVariantMap feature = m_featureData[row];
    const QVariantMap assets = feature.value(QStringLiteral("assets")).toMap();

    QString cogUrl;
    for (auto it = assets.constBegin(); it != assets.constEnd(); ++it) {
        const QVariantMap asset = it.value().toMap();
        const QString href = asset.value(QStringLiteral("href")).toString();
        if (href.endsWith(QStringLiteral(".tif"), Qt::CaseInsensitive) ||
            asset.value(QStringLiteral("type")).toString().contains(QStringLiteral("image/tiff"))) {
            cogUrl = href;
            break;
        }
    }

    if (cogUrl.isEmpty()) {
        QMessageBox::warning(this, tr("Error"), tr("No COG asset found in selected item."));
        return;
    }

    const QString hrefError = StacClient::validateAssetHref(cogUrl);
    if (!hrefError.isEmpty()) {
        QMessageBox::warning(this, tr("Error"),
                             tr("Rejected STAC asset href: %1").arg(hrefError));
        return;
    }

    const QString vsicurl = QStringLiteral("/vsicurl/") + cogUrl;
    auto *layer = new QgsRasterLayer(vsicurl, feature.value(QStringLiteral("id")).toString());
    if (!layer->isValid()) {
        delete layer;
        QMessageBox::warning(this, tr("Error"), tr("Failed to load COG from STAC asset."));
        return;
    }

    QgsProject::instance()->addMapLayer(layer);
    if (m_canvas)
        m_canvas->setExtent(layer->extent());

    accept();
}