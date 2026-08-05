#include "progress_dialog.h"

#include <QTimer>

ProgressDialog::ProgressDialog(const QString &title, QWidget *parent)
    : QDialog(parent)
    , m_cancelled(false)
    , m_autoClose(true)
{
    setWindowTitle(title);
    setMinimumWidth(400);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto *mainLayout = new QVBoxLayout(this);

    m_label = new QLabel(this);
    m_label->setText("Initializing...");
    mainLayout->addWidget(m_label);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    mainLayout->addWidget(m_progressBar);

    auto *bottomLayout = new QHBoxLayout();
    m_elapsedLabel = new QLabel("Elapsed: 0:00", this);
    bottomLayout->addWidget(m_elapsedLabel);
    bottomLayout->addStretch();

    m_cancelButton = new QPushButton(tr("Cancel"), this);
    m_cancelButton->setToolTip(tr("取消当前操作（操作可能不会立即停止）。"));
    connect(m_cancelButton, &QPushButton::clicked, this, &ProgressDialog::onCancelClicked);
    bottomLayout->addWidget(m_cancelButton);

    mainLayout->addLayout(bottomLayout);

    m_elapsedTimer.start();

    // Update elapsed time display every second
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &ProgressDialog::updateElapsedLabel);
    timer->start(1000);
}

int ProgressDialog::value() const
{
    return m_progressBar->value();
}

void ProgressDialog::setValue(int progress)
{
    int clamped = qBound(m_progressBar->minimum(), progress, m_progressBar->maximum());
    m_progressBar->setValue(clamped);

    if (m_autoClose && clamped >= m_progressBar->maximum()) {
        QTimer::singleShot(500, this, &QDialog::accept);
    }
}

int ProgressDialog::minimum() const
{
    return m_progressBar->minimum();
}

int ProgressDialog::maximum() const
{
    return m_progressBar->maximum();
}

void ProgressDialog::setRange(int min, int max)
{
    m_progressBar->setRange(min, max);
}

QString ProgressDialog::labelText() const
{
    return m_label->text();
}

void ProgressDialog::setLabelText(const QString &text)
{
    m_label->setText(text);
}

bool ProgressDialog::isCancelled() const
{
    return m_cancelled;
}

void ProgressDialog::cancel()
{
    if (!m_cancelled) {
        m_cancelled = true;
        m_cancelButton->setEnabled(false);
        m_cancelButton->setText("Cancelling...");
        emit cancelled();
    }
}

void ProgressDialog::reset()
{
    m_cancelled = false;
    m_progressBar->setValue(0);
    m_cancelButton->setEnabled(true);
    m_cancelButton->setText("Cancel");
    m_elapsedTimer.restart();
    updateElapsedLabel();
}

qint64 ProgressDialog::elapsedMs() const
{
    return m_elapsedTimer.elapsed();
}

bool ProgressDialog::autoClose() const
{
    return m_autoClose;
}

void ProgressDialog::setAutoClose(bool enabled)
{
    m_autoClose = enabled;
}

void ProgressDialog::onCancelClicked()
{
    cancel();
}

void ProgressDialog::updateElapsedLabel()
{
    qint64 ms = m_elapsedTimer.elapsed();
    qint64 seconds = ms / 1000;
    qint64 minutes = seconds / 60;
    seconds %= 60;

    m_elapsedLabel->setText(QString("Elapsed: %1:%2")
        .arg(minutes)
        .arg(seconds, 2, 10, QChar('0')));
}
