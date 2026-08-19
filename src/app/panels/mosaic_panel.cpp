// src/app/panels/mosaic_panel.cpp
#include "mosaic_panel.h"
#include "jobs/job_types.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "processing/framework/task_center.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_safe_call.h"
#include "processing/algorithms/mosaic.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QGroupBox>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>
#include <QProgressBar>

#include <qgsmessagelog.h>
#include <qgis.h>

#include <gdal_priv.h>
#include <cpl_string.h>
#include <qgscoordinatereferencesystem.h>

#include <cmath>
#include <limits>
#include <vector>

MosaicPanel::MosaicPanel(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle(tr("Mosaic"));
    setupUi();
}

void MosaicPanel::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);

    // --- Input files group ---
    auto *inputGroup = new QGroupBox(tr("Input Rasters"), this);
    auto *inputLayout = new QVBoxLayout(inputGroup);

    m_inputList = new QListWidget(this);
    inputLayout->addWidget(m_inputList);

    auto *inputBtnLayout = new QHBoxLayout();
    auto *addBtn = new QPushButton(tr("Add..."), this);
    connect(addBtn, &QPushButton::clicked, this, &MosaicPanel::addInputFile);
    inputBtnLayout->addWidget(addBtn);

    auto *removeBtn = new QPushButton(tr("Remove"), this);
    connect(removeBtn, &QPushButton::clicked, this, &MosaicPanel::removeInputFile);
    inputBtnLayout->addWidget(removeBtn);

    inputBtnLayout->addStretch();
    inputLayout->addLayout(inputBtnLayout);

    mainLayout->addWidget(inputGroup);

    // --- Output file ---
    auto *outLayout = new QHBoxLayout();
    outLayout->addWidget(new QLabel(tr("Output:")));
    m_outputEdit = new QLineEdit(this);
    outLayout->addWidget(m_outputEdit);
    auto *browseBtn = new QPushButton(tr("Browse..."), this);
    connect(browseBtn, &QPushButton::clicked, this, &MosaicPanel::browseOutput);
    outLayout->addWidget(browseBtn);
    mainLayout->addLayout(outLayout);

    // --- Progress bar ---
    m_progressBar = new QProgressBar(this);
    m_progressBar->setVisible(false);
    mainLayout->addWidget(m_progressBar);

    // --- Status label ---
    m_statusLabel = new QLabel(this);
    mainLayout->addWidget(m_statusLabel);

    // --- Buttons ---
    auto *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    m_runButton = new QPushButton(tr("Run"), this);
    connect(m_runButton, &QPushButton::clicked, this, &MosaicPanel::runMosaic);
    btnLayout->addWidget(m_runButton);
    mainLayout->addLayout(btnLayout);
}

QString MosaicPanel::outputPath() const
{
    return m_outputEdit ? m_outputEdit->text().trimmed() : QString();
}

QStringList MosaicPanel::inputFiles() const
{
    QStringList files;
    if (m_inputList) {
        for (int i = 0; i < m_inputList->count(); ++i) {
            files.append(m_inputList->item(i)->text());
        }
    }
    return files;
}

void MosaicPanel::addInputFile()
{
    QStringList paths = QFileDialog::getOpenFileNames(this, tr("Add Input Rasters"), QString(),
                                                      tr("Raster files (*.tif *.tiff *.img *.asc);;All files (*)"));
    for (const QString &path : paths) {
        if (!path.isEmpty())
            m_inputList->addItem(path);
    }
}

void MosaicPanel::removeInputFile()
{
    QList<QListWidgetItem *> selected = m_inputList->selectedItems();
    for (QListWidgetItem *item : selected) {
        delete m_inputList->takeItem(m_inputList->row(item));
    }
}

void MosaicPanel::browseOutput()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Output File"), QString(),
                                                tr("GeoTIFF (*.tif)"));
    if (!path.isEmpty())
        m_outputEdit->setText(path);
}

void MosaicPanel::runMosaic()
{
    // --- Validate ---
    if (m_inputList->count() < 2) {
        QMessageBox::warning(this, tr("Mosaic"), tr("At least 2 input rasters are required."));
        return;
    }

    QString outPath = outputPath();
    if (outPath.isEmpty()) {
        QMessageBox::warning(this, tr("Mosaic"), tr("Please specify an output file."));
        return;
    }

    if ( m_jobHandle.isRunning() )
        return;

    // Capture input paths for async execution
    QStringList inputPaths;
    for (int i = 0; i < m_inputList->count(); ++i) {
        inputPaths.append(m_inputList->item(i)->text());
    }

    m_runButton->setEnabled(false);
    m_statusLabel->setText(tr("Processing..."));
    m_progressBar->setVisible(true);
    m_progressBar->setRange(0, 0); // Indeterminate progress

    // Prefer the rs:mosaic operator path (MosaicDialog). This panel keeps a
    // lightweight Task Center callable for embedded use; prefer MosaicDialog when
    // available so CRS/write paths stay consistent.
    sicnu::jobs::JobRequest req;
    req.algorithmId = "callable:mosaic_panel";
    req.title = "Mosaic";
    req.source = "dialog";
    req.exclusive = true;

    m_jobHandle.submitJob(
      req,
      [inputPaths, outPath]( const sicnu::jobs::JobRequest &,
                             sicnu::operators::RSOperatorContext &ctx ) -> Json::Value {
        ctx.logInfo( "Mosaic panel task" );
        try {
            const int inputCount = inputPaths.size();
            struct InputInfo {
                QString path;
                std::vector<float> data;
                int width = 0;
                int height = 0;
                std::array<double, 6> geotransform{};
                QString projection;
            };

            std::vector<InputInfo> inputs( static_cast<size_t>( inputCount ) );

            for ( int i = 0; i < inputCount; ++i ) {
                inputs[static_cast<size_t>( i )].path = inputPaths[i];

                GdalDatasetWrapper ds;
                if ( !ds.open( inputs[static_cast<size_t>( i )].path ) )
                {
                    throw sicnu::operators::RSOperatorError(
                      sicnu::operators::ErrorCode::FileNotReadable,
                      "Failed to open input raster" );
                }

                inputs[static_cast<size_t>( i )].width = ds.width();
                inputs[static_cast<size_t>( i )].height = ds.height();
                inputs[static_cast<size_t>( i )].geotransform = ds.geoTransform();
                inputs[static_cast<size_t>( i )].projection = ds.projection();

                size_t pixelCount = static_cast<size_t>( inputs[static_cast<size_t>( i )].width )
                                    * static_cast<size_t>( inputs[static_cast<size_t>( i )].height );
                constexpr size_t kMaxPixels = 500000000ULL;
                if ( pixelCount == 0 || pixelCount > kMaxPixels )
                {
                    throw sicnu::operators::RSOperatorError(
                      sicnu::operators::ErrorCode::InvalidInputData,
                      "Input raster too large or empty" );
                }
                inputs[static_cast<size_t>( i )].data.resize( pixelCount );

                if ( !ds.readBandData( 1, inputs[static_cast<size_t>( i )].data.data(),
                                       inputs[static_cast<size_t>( i )].width,
                                       inputs[static_cast<size_t>( i )].height ) )
                {
                    throw sicnu::operators::RSOperatorError(
                      sicnu::operators::ErrorCode::FileNotReadable,
                      "Failed to read band 1 data" );
                }
            }

            const auto &gt0 = inputs[0].geotransform;
            double unionMinX = gt0[0];
            double unionMaxY = gt0[3];
            double unionMaxX = gt0[0] + inputs[0].width * gt0[1];
            double unionMinY = gt0[3] + inputs[0].height * gt0[5];

            for ( int i = 1; i < inputCount; ++i ) {
                const auto &gt = inputs[static_cast<size_t>( i )].geotransform;
                double minX = gt[0];
                double maxY = gt[3];
                double maxX = gt[0] + inputs[static_cast<size_t>( i )].width * gt[1];
                double minY = gt[3] + inputs[static_cast<size_t>( i )].height * gt[5];

                unionMinX = ( std::min )( unionMinX, minX );
                unionMaxY = ( std::max )( unionMaxY, maxY );
                unionMaxX = ( std::max )( unionMaxX, maxX );
                unionMinY = ( std::min )( unionMinY, minY );
            }

            double refPixelW = std::abs( gt0[1] );
            double refPixelH = std::abs( gt0[5] );

            int outWidth = static_cast<int>( std::ceil( ( unionMaxX - unionMinX ) / refPixelW ) );
            int outHeight = static_cast<int>( std::ceil( ( unionMaxY - unionMinY ) / refPixelH ) );

            if ( outWidth <= 0 || outHeight <= 0 ) {
                throw sicnu::operators::RSOperatorError(
                  sicnu::operators::ErrorCode::InvalidInputData, "Calculated mosaic dimensions invalid" );
            }

            size_t outPixelCount = static_cast<size_t>( outWidth ) * static_cast<size_t>( outHeight );
            constexpr size_t kMaxPixels = 500000000ULL;
            if ( outPixelCount > kMaxPixels ) {
                throw sicnu::operators::RSOperatorError(
                  sicnu::operators::ErrorCode::InvalidInputData, "Calculated output raster too large" );
            }

            std::array<double, 6> outGT = { unionMinX, refPixelW, 0.0, unionMaxY, 0.0, -refPixelH };

            std::vector<Mosaic::MosaicSource> sources( static_cast<size_t>( inputCount ) );
            for ( int i = 0; i < inputCount; ++i ) {
                const auto &gt = inputs[static_cast<size_t>( i )].geotransform;
                size_t offX = static_cast<size_t>(
                  std::round( ( gt[0] - unionMinX ) / std::abs( refPixelW ) ) );
                size_t offY = static_cast<size_t>(
                  std::round( ( unionMaxY - gt[3] ) / std::abs( refPixelH ) ) );

                sources[static_cast<size_t>( i )].data = inputs[static_cast<size_t>( i )].data.data();
                sources[static_cast<size_t>( i )].width =
                  static_cast<size_t>( inputs[static_cast<size_t>( i )].width );
                sources[static_cast<size_t>( i )].height =
                  static_cast<size_t>( inputs[static_cast<size_t>( i )].height );
                sources[static_cast<size_t>( i )].offsetX = offX;
                sources[static_cast<size_t>( i )].offsetY = offY;
                sources[static_cast<size_t>( i )].nodata = std::numeric_limits<float>::quiet_NaN();
            }

            std::vector<float> outBuf( outPixelCount, std::numeric_limits<float>::quiet_NaN() );
            if ( !Mosaic::merge( sources.data(), sources.size(), outBuf.data(),
                                 static_cast<size_t>( outWidth ), static_cast<size_t>( outHeight ) ) )
            {
                throw sicnu::operators::RSOperatorError(
                  sicnu::operators::ErrorCode::ComputationError, "Mosaic merge failed" );
            }

            GDALAllRegister();
            GDALDriverH driver = GDALGetDriverByName( "GTiff" );
            if ( !driver )
            {
                throw sicnu::operators::RSOperatorError(
                  sicnu::operators::ErrorCode::GdalError, "GTiff driver unavailable" );
            }
            GDALDatasetH dst = GDALCreate( driver, outPath.toUtf8().constData(),
                                           outWidth, outHeight, 1, GDT_Float32, nullptr );
            if ( !dst )
            {
                throw sicnu::operators::RSOperatorError(
                  sicnu::operators::ErrorCode::FileNotWritable, "Failed to create mosaic output" );
            }
            GDALSetGeoTransform( dst, outGT.data() );
            if ( !inputs[0].projection.isEmpty() )
                GDALSetProjection( dst, inputs[0].projection.toUtf8().constData() );

            GDALRasterBandH band = GDALGetRasterBand( dst, 1 );
            CPLErr err = GDALRasterIO( band, GF_Write, 0, 0, outWidth, outHeight,
                                       outBuf.data(), outWidth, outHeight, GDT_Float32, 0, 0 );
            float nodata = std::numeric_limits<float>::quiet_NaN();
            GDALSetRasterNoDataValue( band, nodata );
            GDALClose( dst );
            if ( err != CE_None )
            {
                throw sicnu::operators::RSOperatorError(
                  sicnu::operators::ErrorCode::GdalError, "Failed to write mosaic output" );
            }

            Json::Value result( Json::objectValue );
            result["output"] = outPath.toStdString();
            return result;
        } catch ( const sicnu::operators::RSOperatorError & ) {
            throw;
        } catch ( const std::exception &e ) {
            throw sicnu::operators::RSOperatorError(
              sicnu::operators::ErrorCode::ComputationError, e.what() );
        }
      },
      /*cancelCallback=*/nullptr,
      /*autoLoad=*/false,
      [this]( const QString &outPath, const Json::Value & ) {
        onCompleted( outPath );
      },
      [this]( const QString &err, bool isCanceled ) {
        onFailed( isCanceled ? tr( "已取消" ) : err );
      }
    );
}

void MosaicPanel::onCompleted(const QString &outputPath)
{
    m_runButton->setEnabled(true);
    m_statusLabel->setText(tr("Mosaic completed!"));
    m_progressBar->setVisible(false);
    emit mosaicCompleted(outputPath);
}

void MosaicPanel::onFailed(const QString &error)
{
    m_runButton->setEnabled(true);
    m_statusLabel->setText(tr("Failed: %1").arg(error));
    m_progressBar->setVisible(false);
    emit mosaicFailed(error);
}
