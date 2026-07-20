// raster_processing_dialog_base.cpp — Base class for raster processing dialogs
#include "raster_processing_dialog_base.h"
#include "async_gdal_runner.h"
#include "async_algorithm_runner.h"
#include "dialog_help_catalog.h"

#include "operators/framework/rs_operator_registry.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator.h"

#include <processing/qgsprocessingalgorithm.h>
#include <processing/qgsprocessingcontext.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QFrame>

#include <raster/qgsrasterlayer.h>
#include <qgsmessagelog.h>
#include <qgis.h>

RasterProcessingDialogBase::RasterProcessingDialogBase(QWidget *parent)
    : QDialog(parent)
{
}

void RasterProcessingDialogBase::reject()
{
    // Guard close/Cancel while a GDAL or algorithm task is in flight.
    if (isRunning())
        return;
    QDialog::reject();
}

void RasterProcessingDialogBase::setRasterLayer(QgsRasterLayer *layer)
{
    m_rasterLayer = layer;
}

QString RasterProcessingDialogBase::outputPath() const
{
    return m_outputEdit ? m_outputEdit->text().trimmed() : QString();
}

bool RasterProcessingDialogBase::validateInputs()
{
    QString path = outputPath();
    if (path.isEmpty()) {
        QMessageBox::warning(this, dialogTitle(), tr("Please specify an output file."));
        return false;
    }

    if (!m_rasterLayer || !m_rasterLayer->isValid()) {
        QMessageBox::warning(this, dialogTitle(), tr("No valid raster layer selected."));
        return false;
    }

    return true;
}

void RasterProcessingDialogBase::setupHelpBanner( QVBoxLayout *layout )
{
    if ( !layout )
        return;

    SicnuDialogHelp::applyDialogChrome( this, toolName() );

    auto *frame = new QFrame( this );
    frame->setObjectName( QStringLiteral( "rsDialogHelpBanner" ) );
    frame->setFrameShape( QFrame::StyledPanel );
    auto *hl = new QHBoxLayout( frame );
    hl->setContentsMargins( 8, 6, 8, 6 );
    auto *lbl = new QLabel( SicnuDialogHelp::shortForTool( toolName(), dialogTitle() ), frame );
    lbl->setWordWrap( true );
    lbl->setObjectName( QStringLiteral( "rsDialogHelpSummary" ) );
    SicnuDialogHelp::tip( lbl, tr( "本对话框功能简介。点击「帮助」查看完整说明。" ) );
    hl->addWidget( lbl, 1 );
    auto *more = new QPushButton( tr( "帮助…" ), frame );
    more->setObjectName( QStringLiteral( "rsDialogHelpBtn" ) );
    SicnuDialogHelp::tip( more, tr( "打开本功能的说明文档。" ) );
    connect( more, &QPushButton::clicked, this, [this]() {
        SicnuDialogHelp::showHelpBox( this, dialogTitle(), dialogHelpHtml() );
    } );
    hl->addWidget( more );
    layout->insertWidget( 0, frame );
}

QString RasterProcessingDialogBase::dialogHelpHtml() const
{
    return SicnuDialogHelp::htmlForTool( toolName(), dialogTitle() );
}

void RasterProcessingDialogBase::setupOutputRow(QVBoxLayout *layout)
{
    auto *outLayout = new QHBoxLayout();
    auto *outLabel = new QLabel(tr("Output:"), this);
    SicnuDialogHelp::tip( outLabel, tr( "输出 GeoTIFF 路径。运行前必须填写。" ) );
    outLayout->addWidget(outLabel);
    m_outputEdit = new QLineEdit(this);
    SicnuDialogHelp::tip( m_outputEdit, tr( "结果保存路径。建议使用 .tif 扩展名。" ) );
    outLayout->addWidget(m_outputEdit);
    auto *browseBtn = new QPushButton(tr("Browse..."), this);
    SicnuDialogHelp::tip( browseBtn, tr( "浏览选择输出文件位置。" ) );
    connect(browseBtn, &QPushButton::clicked, this, &RasterProcessingDialogBase::browseOutput);
    outLayout->addWidget(browseBtn);
    layout->addLayout(outLayout);
}

void RasterProcessingDialogBase::setupButtonBar(QVBoxLayout *layout)
{
    auto *btnLayout = new QHBoxLayout();
    auto *helpBtn = new QPushButton( tr( "帮助" ), this );
    helpBtn->setObjectName( QStringLiteral( "rsDialogHelpButton" ) );
    SicnuDialogHelp::tip( helpBtn, tr( "查看本功能的说明文档与使用提示。" ) );
    connect( helpBtn, &QPushButton::clicked, this, [this]() {
        SicnuDialogHelp::showHelpBox( this, dialogTitle(), dialogHelpHtml() );
    } );
    btnLayout->addWidget( helpBtn );
    btnLayout->addStretch();
    m_runButton = new QPushButton(tr("Run"), this);
    SicnuDialogHelp::tip( m_runButton, tr( "校验输入后开始处理。运行中请勿关闭对话框。" ) );
    connect(m_runButton, &QPushButton::clicked, this, [this]() {
        if (validateInputs()) {
            onRun();
        }
    });
    btnLayout->addWidget(m_runButton);
    auto *cancelBtn = new QPushButton(tr("Cancel"), this);
    SicnuDialogHelp::tip( cancelBtn, tr( "关闭对话框（任务运行中将忽略关闭）。" ) );
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(cancelBtn);
    layout->addLayout(btnLayout);
}

void RasterProcessingDialogBase::browseOutput()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Output File"), QString(),
                                                tr("GeoTIFF (*.tif)"));
    if (!path.isEmpty())
        m_outputEdit->setText(path);
}

void RasterProcessingDialogBase::startRun()
{
    m_running = true;
    if (m_runButton)
        m_runButton->setEnabled(false);
    // Prevent Esc / window-close from destroying the dialog mid-run.
    setCursor(Qt::WaitCursor);
}

void RasterProcessingDialogBase::finishRun()
{
    m_running = false;
    if (m_runButton)
        m_runButton->setEnabled(true);
    unsetCursor();
}

void RasterProcessingDialogBase::runGdalTask(const std::function<QString()> &task)
{
    if (isRunning())
        return;

    if (!m_runner) {
        m_runner = new AsyncGdalRunner(this, this);
        connect(m_runner, &AsyncGdalRunner::completed, this, &RasterProcessingDialogBase::onCompleted);
        connect(m_runner, &AsyncGdalRunner::failed, this, &RasterProcessingDialogBase::onFailed);
    }

    startRun();
    m_runner->run(task);
}

void RasterProcessingDialogBase::runAlgorithmTask(const QgsProcessingAlgorithm *algorithm,
                                                const QVariantMap &parameters,
                                                QgsProcessingContext &context)
{
    if (isRunning())
        return;

    if (!m_algorithmRunner) {
        m_algorithmRunner = new AsyncAlgorithmRunner(this, this);
        connect(m_algorithmRunner, &AsyncAlgorithmRunner::completed, this,
                [this](const QVariantMap &results) {
                    Q_UNUSED(results);
                    onCompleted(outputPath());
                });
        connect(m_algorithmRunner, &AsyncAlgorithmRunner::failed, this,
                &RasterProcessingDialogBase::onFailed);
    }

    startRun();
    m_algorithmRunner->run(algorithm, parameters, context);
}

void RasterProcessingDialogBase::runOperatorTask(const QString &operatorId,
                                                 const Json::Value &params)
{
    if (isRunning())
        return;

    // Capture by value: params and operatorId must outlive the UI thread call.
    const std::string opId = operatorId.toStdString();
    const Json::Value paramsCopy = params;
    const QString errMarker = AsyncGdalRunner::errorMarker();

    runGdalTask([opId, paramsCopy, errMarker]() -> QString {
        try {
            auto op = sicnu::operators::RSOperatorRegistry::instance().create(opId);
            if (!op) {
                return errMarker + QStringLiteral("Operator not registered: %1")
                                       .arg(QString::fromStdString(opId));
            }

            sicnu::operators::RSOperatorContext context;
            const Json::Value result = op->execute(paramsCopy, context);

            if (result.isMember("output") && result["output"].isString()) {
                return QString::fromStdString(result["output"].asString());
            }
            return errMarker + QStringLiteral("Operator '%1' did not return an output path")
                                   .arg(QString::fromStdString(opId));
        } catch (const sicnu::operators::RSOperatorError &e) {
            return errMarker + QString::fromStdString(e.message());
        } catch (const std::exception &e) {
            return errMarker + QString::fromUtf8(e.what());
        } catch (...) {
            return errMarker + QStringLiteral("Unknown operator error");
        }
    });
}

void RasterProcessingDialogBase::handleCompleted(const QString &outputPath)
{
    cleanupRunResources();
    finishRun();
    QgsMessageLog::logMessage(tr("%1 completed! Output: %2").arg(toolName(), outputPath),
                              toolName(), Qgis::MessageLevel::Success);
    accept();
}

void RasterProcessingDialogBase::handleFailed(const QString &error)
{
    cleanupRunResources();
    finishRun();
    QgsMessageLog::logMessage(error, toolName(), Qgis::MessageLevel::Critical);
    QMessageBox::critical(this, dialogTitle(),
                          error.isEmpty() ? tr("Operation failed. See log for details.")
                                          : error);
}

void RasterProcessingDialogBase::onCompleted(const QString &outputPath)
{
    handleCompleted(outputPath);
}

void RasterProcessingDialogBase::onFailed(const QString &errorMessage)
{
    handleFailed(errorMessage);
}
