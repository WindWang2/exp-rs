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
  setWindowTitle( tr( "Layer Comparison" ) );
  SicnuUi::polishDialog( this, 800 );
  SicnuDialogHelp::applyDialogChrome( this, QStringLiteral( "comparison" ) );
  resize( 860, 640 );
  setupUi();
}

void ComparisonDialog::setupUi()
{
  auto *mainLayout = SicnuUi::makeDialogRootLayout( this );
  mainLayout->addWidget( SicnuUi::makeHintLabel(
    this, tr( "Select the left/right comparison layers and press 'Load Comparison'; supports a draggable swipe divider, side-by-side and quick blink toggling." ) ) );

  QGroupBox *bar = SicnuUi::makeGroup( this, tr( "Comparison Layer Configuration" ) );
  auto *layerLayout = new QHBoxLayout( bar );
  layerLayout->setContentsMargins( 10, 8, 10, 8 );
  layerLayout->setSpacing( 8 );

  layerLayout->addWidget( new QLabel( tr( "Left layer (earlier epoch / base)" ), bar ) );
  m_leftLayerCombo = new RasterLayerCombo( bar );
  m_leftLayerCombo->setObjectName( QStringLiteral( "compareLeftCombo" ) );
  m_leftLayerCombo->setMinimumWidth( 200 );
  SicnuDialogHelp::tip( m_leftLayerCombo, tr( "The left viewport shows the base or earlier-epoch raster." ) );
  layerLayout->addWidget( m_leftLayerCombo, 1 );

  layerLayout->addWidget( new QLabel( tr( "Right layer (later epoch / target)" ), bar ) );
  m_rightLayerCombo = new RasterLayerCombo( bar );
  m_rightLayerCombo->setObjectName( QStringLiteral( "compareRightCombo" ) );
  m_rightLayerCombo->setMinimumWidth( 200 );
  SicnuDialogHelp::tip( m_rightLayerCombo, tr( "The right viewport shows the target or later-epoch raster." ) );
  layerLayout->addWidget( m_rightLayerCombo, 1 );

  m_loadButton = new QPushButton( tr( "Load Comparison" ), bar );
  m_loadButton->setObjectName( QStringLiteral( "compareLoadButton" ) );
  SicnuUi::markPrimary( m_loadButton );
  SicnuDialogHelp::tip( m_loadButton, tr( "Renders the selected left/right layers into the comparison view below." ) );
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

  auto *helpBtn = buttonBox->addButton( tr( "Help" ), QDialogButtonBox::HelpRole );
  SicnuUi::markSecondary( helpBtn );
  connect( helpBtn, &QPushButton::clicked, this, [this]() {
    SicnuDialogHelp::showToolHelp( this, QStringLiteral( "comparison" ), windowTitle() );
  } );

  auto *closeBtn = buttonBox->addButton( tr( "Close" ), QDialogButtonBox::RejectRole );
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
        QMessageBox::warning(this, tr("Layer Comparison"),
                             tr("Select a valid raster layer on each side."));
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
