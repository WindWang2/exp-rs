// src/app/panels/mosaic_panel.cpp
#include "mosaic_panel.h"
#include "widgets/rs_empty_state_widget.h"
#include "jobs/job_types.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QGroupBox>
#include <QListWidget>
#include <QStackedWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>
#include <QProgressBar>

#include <qgsmessagelog.h>

MosaicPanel::MosaicPanel(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle(tr("影像镶嵌"));
    setupUi();
}

void MosaicPanel::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);

    // --- Input files group ---
    auto *inputGroup = new QGroupBox(tr("输入栅格影像"), this);
    auto *inputLayout = new QVBoxLayout(inputGroup);

    m_inputStack = new QStackedWidget(inputGroup);
    m_inputStack->setObjectName(QStringLiteral("rsMosaicInputStack"));

    m_inputList = new QListWidget(m_inputStack);
    m_inputStack->addWidget(m_inputList); // Index 0: List

    m_emptyState = new sicnu::RsEmptyStateWidget(
        QStringLiteral("l_yer_st_ck"),
        tr("暂无镶嵌输入影像"),
        tr("添加需要拼接镶嵌的遥感影像条带或分幅切片数据。"),
        tr("添加影像..."),
        m_inputStack);
    connect(m_emptyState, &sicnu::RsEmptyStateWidget::actionClicked, this, &MosaicPanel::addInputFile);
    m_inputStack->addWidget(m_emptyState); // Index 1: Empty
    m_inputStack->setCurrentIndex(1); // Initially empty

    inputLayout->addWidget(m_inputStack);

    auto *inputBtnLayout = new QHBoxLayout();
    auto *addBtn = new QPushButton(tr("添加..."), this);
    connect(addBtn, &QPushButton::clicked, this, &MosaicPanel::addInputFile);
    inputBtnLayout->addWidget(addBtn);

    auto *removeBtn = new QPushButton(tr("移除"), this);
    connect(removeBtn, &QPushButton::clicked, this, &MosaicPanel::removeInputFile);
    inputBtnLayout->addWidget(removeBtn);

    inputBtnLayout->addStretch();
    inputLayout->addLayout(inputBtnLayout);

    mainLayout->addWidget(inputGroup);

    // --- Output file ---
    auto *outLayout = new QHBoxLayout();
    outLayout->addWidget(new QLabel(tr("输出路径：")));
    m_outputEdit = new QLineEdit(this);
    outLayout->addWidget(m_outputEdit);
    auto *browseBtn = new QPushButton(tr("浏览..."), this);
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
    m_runButton = new QPushButton(tr("运行镶嵌"), this);
    m_runButton->setProperty("primary", true);
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
    QStringList paths = QFileDialog::getOpenFileNames(this, tr("添加输入栅格影像"), QString(),
                                                      tr("栅格文件 (*.tif *.tiff *.img *.asc);;所有文件 (*)"));
    for (const QString &path : paths) {
        if (!path.isEmpty())
            m_inputList->addItem(path);
    }
    if (m_inputStack)
        m_inputStack->setCurrentIndex(m_inputList->count() > 0 ? 0 : 1);
}

void MosaicPanel::removeInputFile()
{
    QList<QListWidgetItem *> selected = m_inputList->selectedItems();
    for (QListWidgetItem *item : selected) {
        delete m_inputList->takeItem(m_inputList->row(item));
    }
    if (m_inputStack)
        m_inputStack->setCurrentIndex(m_inputList->count() > 0 ? 0 : 1);
}

void MosaicPanel::browseOutput()
{
    QString path = QFileDialog::getSaveFileName(this, tr("选择输出文件"), QString(),
                                                tr("GeoTIFF (*.tif)"));
    if (!path.isEmpty())
        m_outputEdit->setText(path);
}

void MosaicPanel::runMosaic()
{
    // --- Validate ---
    if (m_inputList->count() < 2) {
        QMessageBox::warning(this, tr("影像镶嵌"), tr("至少需要 2 个输入栅格影像。"));
        return;
    }

    QString outPath = outputPath();
    if (outPath.isEmpty()) {
        QMessageBox::warning(this, tr("影像镶嵌"), tr("请指定输出文件路径。"));
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

    // Same operator contract as MosaicDialog: the panel is a client of
    // rs:mosaic (the legacy panel lambda duplicated the union math,
    // band reads and GDAL write of the operator — deleted, #663 family).
    sicnu::jobs::JobRequest req;
    req.algorithmId = "rs:mosaic";
    req.title = "Mosaic";
    req.source = "dialog";
    req.exclusive = true;
    req.params = Json::Value( Json::objectValue );
    req.params["inputs"] = Json::Value( Json::arrayValue );
    for ( const QString &path : inputPaths )
        req.params["inputs"].append( path.toStdString() );
    req.params["output"] = outPath.toStdString();

    // autoLoad=false preserves the deleted lambda path's behavior: the host
    // of this embedded panel decides what happens with the output (the
    // dialog workflow, by contrast, auto-loads into the map).
    m_jobHandle.submitJob(
      req,
      sicnu::JobExecutor{},
      std::function<void()>{},
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
