// src/app/dialogs/comparison_dialog.cpp
#include "comparison_dialog.h"
#include "dialog_help_catalog.h"
#include "dialog_utils.h"
#include "widgets/comparison_widget.h"
#include "widgets/raster_layer_combo.h"

#include <qgsmaprendererparalleljob.h>
#include <qgsmapsettings.h>
#include <qgsrasterlayer.h>
#include <qgsproject.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QGroupBox>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QMessageBox>

ComparisonDialog::ComparisonDialog( QWidget *parent )
  : QDialog( parent )
{
  setWindowTitle( tr( "图层对比" ) );
  SicnuUi::polishDialog( this, 800 );
  SicnuDialogHelp::applyDialogChrome( this, QStringLiteral( "comparison" ) );
  resize( 860, 640 );
  setupUi();
}

void ComparisonDialog::setupUi()
{
  auto *mainLayout = SicnuUi::makeDialogRootLayout( this );
  mainLayout->addWidget( SicnuUi::makeHintLabel(
    this, tr( "选择左右对比图层后点击「加载对比」，支持分割线卷帘 (Swipe)、并排对比与快速闪烁切换。" ) ) );

  QGroupBox *bar = SicnuUi::makeGroup( this, tr( "对比图层配置" ) );
  auto *layerLayout = new QHBoxLayout( bar );
  layerLayout->setContentsMargins( 10, 8, 10, 8 );
  layerLayout->setSpacing( 8 );

  layerLayout->addWidget( new QLabel( tr( "左侧图层 (前时相/基准)" ), bar ) );
  m_leftLayerCombo = new RasterLayerCombo( bar );
  m_leftLayerCombo->setObjectName( QStringLiteral( "compareLeftCombo" ) );
  m_leftLayerCombo->setMinimumWidth( 200 );
  SicnuDialogHelp::tip( m_leftLayerCombo, tr( "左侧视口显示的基准或较早时相栅格。" ) );
  layerLayout->addWidget( m_leftLayerCombo, 1 );

  layerLayout->addWidget( new QLabel( tr( "右侧图层 (后时相/目标)" ), bar ) );
  m_rightLayerCombo = new RasterLayerCombo( bar );
  m_rightLayerCombo->setObjectName( QStringLiteral( "compareRightCombo" ) );
  m_rightLayerCombo->setMinimumWidth( 200 );
  SicnuDialogHelp::tip( m_rightLayerCombo, tr( "右侧视口显示的目标或较晚时相栅格。" ) );
  layerLayout->addWidget( m_rightLayerCombo, 1 );

  m_loadButton = new QPushButton( tr( "加载对比" ), bar );
  m_loadButton->setObjectName( QStringLiteral( "compareLoadButton" ) );
  SicnuUi::markPrimary( m_loadButton );
  SicnuDialogHelp::tip( m_loadButton, tr( "将所选左右图层渲染加载到下方对比视图中。" ) );
  connect( m_loadButton, &QPushButton::clicked, this, &ComparisonDialog::onLoadLayers );
  layerLayout->addWidget( m_loadButton );

  mainLayout->addWidget( bar );

  m_comparisonWidget = new ComparisonWidget( this );
  mainLayout->addWidget( m_comparisonWidget, 1 );

  // Shared raster picker (C5, ADR 0107): list project rasters once.
  m_leftLayerCombo->populate();
  m_rightLayerCombo->populate();
  if ( m_rightLayerCombo->count() > 1 )
    m_rightLayerCombo->setCurrentIndex( 1 );

  auto *buttonBox = new QDialogButtonBox( this );
  buttonBox->setObjectName( QStringLiteral( "compareButtonBox" ) );

  auto *helpBtn = buttonBox->addButton( tr( "帮助" ), QDialogButtonBox::HelpRole );
  SicnuUi::markSecondary( helpBtn );
  connect( helpBtn, &QPushButton::clicked, this, [this]() {
    SicnuDialogHelp::showToolHelp( this, QStringLiteral( "comparison" ), windowTitle() );
  } );

  auto *closeBtn = buttonBox->addButton( tr( "关闭" ), QDialogButtonBox::RejectRole );
  SicnuUi::markSecondary( closeBtn );
  connect( closeBtn, &QPushButton::clicked, this, &QDialog::accept );

  mainLayout->addWidget( buttonBox );
}

void ComparisonDialog::setLeftLayer(QgsRasterLayer *layer)
{
    m_leftLayer = layer;
    if (layer) {
        loadLayerToWidget(layer, true);
    }
}

void ComparisonDialog::setRightLayer(QgsRasterLayer *layer)
{
    m_rightLayer = layer;
    if (layer) {
        loadLayerToWidget(layer, false);
    }
}

void ComparisonDialog::onBrowseLeft()
{
    // Find and set left layer from combo
    setLeftLayer( m_leftLayerCombo->currentRasterLayer() );
}

void ComparisonDialog::onBrowseRight()
{
    // Find and set right layer from combo
    setRightLayer( m_rightLayerCombo->currentRasterLayer() );
}

void ComparisonDialog::onLoadLayers()
{
    // Load both layers
    onBrowseLeft();
    onBrowseRight();

    if (!m_leftLayer || !m_rightLayer) {
        QMessageBox::warning(this, tr("图层对比"),
                             tr("请在左右两侧各选择一个有效的栅格图层。"));
        return;
    }
}

ComparisonDialog::~ComparisonDialog()
{
    // In-flight preview renders must be cancelled and drained before the
    // member job pointers die; destroying a RUNNING QgsMapRendererParallelJob
    // crashed (#634 follow-up).
    for ( QgsMapRendererParallelJob *job : { m_leftJob, m_rightJob } )
    {
        if ( job )
        {
            job->cancel();
            job->waitForFinished();
            delete job;
        }
    }
    m_leftJob = m_rightJob = nullptr;
}

void ComparisonDialog::loadLayerToWidget(QgsRasterLayer *layer, bool isLeft)
{
    if (!layer || !layer->isValid()) return;
    startPreviewRender(layer, isLeft);
}

void ComparisonDialog::startPreviewRender(QgsRasterLayer *layer, bool isLeft)
{
    // Asynchronous preview (#634): the old QgsMapRendererParallelJob +
    // waitForFinished() ran ON the GUI thread - on /vsicurl/ or
    // overview-less sources both 400x400 renders froze the dialog. The
    // finished signal delivers the image (SwipeMapTool pattern); a stale
    // job for a superseded load is cancelled and discarded.
    const QSize size(400, 400);
    QgsMapSettings mapSettings;
    mapSettings.setDestinationCrs(layer->crs());
    mapSettings.setExtent(layer->extent());
    mapSettings.setOutputSize(size);
    mapSettings.setLayers({layer});

    QgsMapRendererParallelJob *&job = isLeft ? m_leftJob : m_rightJob;
    if (job)
    {
        job->cancel();
        job->deleteLater();
    }
    job = new QgsMapRendererParallelJob(mapSettings);
    job->setParent(this);

    const QSize previewSize = size;
    connect(job, &QgsMapRendererParallelJob::finished, this,
            [this, job, isLeft, previewSize]() {
                if ((isLeft ? m_leftJob : m_rightJob) != job)
                    return;  // superseded
                QPixmap pixmap;
                const QImage image = job->renderedImage();
                if (!image.isNull())
                    pixmap = QPixmap::fromImage(image);

                if (pixmap.isNull())
                {
                    pixmap = QPixmap(previewSize);
                    pixmap.fill(Qt::darkGray);
                    QPainter painter(&pixmap);
                    painter.setPen(Qt::white);
                    painter.setFont(QFont("Arial", 12));
                    painter.drawText(pixmap.rect(), Qt::AlignCenter,
                                     tr("preview unavailable"));
                    painter.end();
                }

                if (isLeft)
                    m_comparisonWidget->setLeftImage(pixmap);
                else
                    m_comparisonWidget->setRightImage(pixmap);
                job->deleteLater();
                if (isLeft) m_leftJob = nullptr; else m_rightJob = nullptr;
            });

    job->start();

    // Placeholder while rendering (keeps the panes responsive-looking).
    QPixmap placeholder(size);
    placeholder.fill(Qt::darkGray);

    if (isLeft)
        m_comparisonWidget->setLeftImage(placeholder);
    else
        m_comparisonWidget->setRightImage(placeholder);
}
