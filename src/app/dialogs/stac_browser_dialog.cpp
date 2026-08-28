// stac_browser_dialog.cpp — STAC Catalog Browser Dialog
#include "stac_browser_dialog.h"
#include <gdal.h>
#include <QPointer>
#include <QApplication>
#include <QThreadPool>
#include "dialog_help_catalog.h"
#include "dialog_utils.h"
#include "main_window.h"
#include "stac_client.h"

#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QHeaderView>
#include <QMessageBox>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>

#include <qgsmapcanvas.h>
#include <qgsrasterlayer.h>
#include <qgsproject.h>

StacBrowserDialog::StacBrowserDialog( QgsMapCanvas *canvas, QWidget *parent )
  : QDialog( parent )
  , m_canvas( canvas )
  , m_stacClient( new StacClient( this ) )
{
  setWindowTitle( tr( "STAC 数据浏览" ) );
  SicnuUi::polishDialog( this, 720 );
  SicnuDialogHelp::applyDialogChrome( this, QStringLiteral( "stac_browser" ) );
  resize( 820, 620 );
  setupUi();
  connect( m_stacClient, &StacClient::searchCompleted,
           this, &StacBrowserDialog::onSearchCompleted );
}

void StacBrowserDialog::setupUi()
{
  auto *mainLayout = SicnuUi::makeDialogRootLayout( this );
  mainLayout->addWidget( SicnuUi::makeHintLabel(
    this, tr( "流程：填写目录与时空条件 → 检索 → 选中结果 → 加载资产到工程。" ) ) );

  QFrame *querySec = SicnuUi::makeSection( this, tr( "检索条件" ) );
  auto *formLayout = new QFormLayout();
  formLayout->setContentsMargins( 0, 0, 0, 0 );
  formLayout->setHorizontalSpacing( 12 );
  formLayout->setVerticalSpacing( 8 );
  m_endpointEdit = new QLineEdit( QStringLiteral( "https://earth-search.aws.element84.com/v1" ), querySec );
  m_collectionEdit = new QLineEdit( QStringLiteral( "sentinel-2-l2a" ), querySec );
  m_datetimeEdit = new QLineEdit( querySec );
  m_bboxEdit = new QLineEdit( querySec );
  m_bboxEdit->setPlaceholderText( tr( "min_lon,min_lat,max_lon,max_lat" ) );
  m_moreButton = new QPushButton( tr( "更多结果" ), querySec );
  m_moreButton->setToolTip( tr( "加载下一页检索结果。" ) );
  m_moreButton->hide();
  connect( m_moreButton, &QPushButton::clicked, this, [this]() {
      m_moreButton->setEnabled( false );
      m_stacClient->searchNext();
  } );
  SicnuDialogHelp::tip( m_endpointEdit, tr( "STAC API 根 URL。" ) );
  SicnuDialogHelp::tip( m_collectionEdit, tr( "集合 ID，如 sentinel-2-l2a。" ) );
  SicnuDialogHelp::tip( m_datetimeEdit, tr( "时间过滤（ISO）。" ) );
  SicnuDialogHelp::tip( m_bboxEdit, tr( "空间范围。" ) );
  formLayout->addRow( tr( "端点 Endpoint" ), m_endpointEdit );
  formLayout->addRow( tr( "集合 Collection" ), m_collectionEdit );
  formLayout->addRow( tr( "时间 Datetime" ), m_datetimeEdit );
  formLayout->addRow( tr( "范围 BBox" ), m_bboxEdit );
  qobject_cast<QVBoxLayout *>( querySec->layout() )->addLayout( formLayout );

  m_searchButton = new QPushButton( tr( "检索" ), querySec );
  SicnuUi::markPrimary( m_searchButton );
  SicnuDialogHelp::tip( m_searchButton, tr( "按条件检索 STAC 要素。" ) );
  connect( m_searchButton, &QPushButton::clicked, this, &StacBrowserDialog::searchCatalog );
  SicnuUi::markSecondary( m_moreButton );
  auto *searchRow = new QWidget( querySec );
  auto *searchRowLayout = new QHBoxLayout( searchRow );
  searchRowLayout->setContentsMargins( 0, 0, 0, 0 );
  searchRowLayout->addWidget( m_searchButton, 1 );
  searchRowLayout->addWidget( m_moreButton );
  qobject_cast<QVBoxLayout *>( querySec->layout() )->addWidget( searchRow );
  mainLayout->addWidget( querySec );

  QFrame *resSec = SicnuUi::makeSection( this, tr( "检索结果" ) );
  m_resultsTable = new QTableWidget( resSec );
  m_resultsTable->setColumnCount( 4 );
  m_resultsTable->setHorizontalHeaderLabels(
    { tr( "ID" ), tr( "集合" ), tr( "时间" ), tr( "资产" ) } );
  m_resultsTable->horizontalHeader()->setStretchLastSection( true );
  m_resultsTable->setSelectionBehavior( QAbstractItemView::SelectRows );
  m_resultsTable->setSelectionMode( QAbstractItemView::SingleSelection );
  m_resultsTable->setMinimumHeight( 200 );
  SicnuDialogHelp::tip( m_resultsTable, tr( "检索结果。选中一行后加载资产。" ) );
  qobject_cast<QVBoxLayout *>( resSec->layout() )->addWidget(  m_resultsTable );

  auto *actRow = new QHBoxLayout();
  m_loadButton = new QPushButton( tr( "加载选中资产" ), resSec );
  m_loadButton->setEnabled( false );
  SicnuUi::markPrimary( m_loadButton );
  SicnuDialogHelp::tip( m_loadButton, tr( "加载到当前工程（需网络）。" ) );
  connect( m_loadButton, &QPushButton::clicked, this, &StacBrowserDialog::loadSelectedAsset );
  actRow->addWidget( m_loadButton );
  actRow->addStretch();
  auto *helpBtn = new QPushButton( tr( "帮助" ), resSec );
  SicnuUi::markSecondary( helpBtn );
  connect( helpBtn, &QPushButton::clicked, this, [this]() {
    SicnuDialogHelp::showToolHelp( this, QStringLiteral( "stac_browser" ), windowTitle() );
  } );
  actRow->addWidget( helpBtn );
  qobject_cast<QVBoxLayout *>( resSec->layout() )->addLayout( actRow );
  mainLayout->addWidget( resSec, 1 );
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

void StacBrowserDialog::onSearchCompleted(const QVariantList &features, const QString &error,
                                          const QUrl &nextPage)
{
    m_searchButton->setEnabled(true);
    m_searchButton->setText(tr("Search"));
    m_moreButton->setEnabled(true);

    if (!error.isEmpty()) {
        QMessageBox::warning(this, tr("Search Failed"), error);
        return;
    }

    populateResults(features);

    // STAC pagination (#634): offer the next page when the server provides
    // a `next` link - the fixed limit previously hid everything past page 1.
    m_moreButton->setVisible( nextPage.isValid() && !nextPage.isEmpty() );
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
    const QString vsicurl = StacClient::selectCogHref(QJsonObject::fromVariantMap(feature));

    if (vsicurl.isEmpty()) {
        QMessageBox::warning(this, tr("Error"), tr("No COG asset found in selected item."));
        return;
    }

    // Route through main shell Data/Display seam (ADR 0010 Wave B) — no raw addMapLayer.
    // The COG VALIDATION open happens OFF the GUI thread (#634): a
    // /vsicurl/ source on a cold cache did synchronous network round trips
    // inside the click slot. The worker pre-opens the dataset (validates
    // + warms GDAL caches); registration still runs on the GUI thread
    // (DataManager is thread-affine).
    if ( auto *mw = qobject_cast<QgisDesktopWindow *>( parentWidget() ) )
    {
        QApplication::setOverrideCursor( Qt::WaitCursor );
        setEnabled( false );
        QPointer<QgisDesktopWindow> safeMw( mw );
        QPointer<StacBrowserDialog> safeSelf( this );
        QThreadPool::globalInstance()->start( [safeSelf, safeMw, vsicurl]() {
            GDALAllRegister();
            GDALDatasetH ds = GDALOpenEx( vsicurl.toUtf8().constData(),
                                          GDAL_OF_RASTER | GDAL_OF_READONLY,
                                          nullptr, nullptr, nullptr );
            const bool openable = ds != nullptr;
            if ( ds )
                GDALClose( ds );
            QMetaObject::invokeMethod( qApp, [safeSelf, safeMw, vsicurl, openable]() {
                QApplication::restoreOverrideCursor();
                if ( !safeSelf || !safeMw )
                    return;
                safeSelf->setEnabled( true );
                if ( !openable )
                {
                    QMessageBox::warning( safeSelf, safeSelf->tr( "Error" ),
                                          safeSelf->tr( "Failed to load COG from STAC asset." ) );
                    return;
                }
                if ( !safeMw->loadDataLayer( vsicurl ) )
                {
                    QMessageBox::warning( safeSelf, safeSelf->tr( "Error" ),
                                          safeSelf->tr( "Failed to register/display STAC COG via Data Manager." ) );
                    return;
                }
                if ( safeSelf->m_canvas )
                    safeSelf->m_canvas->zoomToFullExtent();
                safeSelf->accept();
            } );
        } );
        return;
    }

    // Headless / no main window: best-effort GDAL open (tests only).
    auto *layer = new QgsRasterLayer( vsicurl, feature.value( QStringLiteral( "id" ) ).toString() );
    if ( !layer->isValid() )
    {
        delete layer;
        QMessageBox::warning( this, tr( "Error" ), tr( "Failed to load COG from STAC asset." ) );
        return;
    }
    QgsProject::instance()->addMapLayer( layer );
    if ( m_canvas )
        m_canvas->setExtent( layer->extent() );
    accept();
}