#ifndef PROGRESS_DIALOG_H
#define PROGRESS_DIALOG_H

#include <QDialog>
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>
#include <QElapsedTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>

class ProgressDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ProgressDialog(const QString &title = "Processing...", QWidget *parent = nullptr);

    int value() const;
    void setValue(int progress);

    int minimum() const;
    int maximum() const;
    void setRange(int min, int max);

    QString labelText() const;
    void setLabelText(const QString &text);

    bool isCancelled() const;
    void cancel();
    void reset();

    qint64 elapsedMs() const;

    bool autoClose() const;
    void setAutoClose(bool enabled);

signals:
    void cancelled();

private slots:
    void onCancelClicked();

private:
    void updateElapsedLabel();

    QProgressBar *m_progressBar;
    QLabel *m_label;
    QLabel *m_elapsedLabel;
    QPushButton *m_cancelButton;
    QElapsedTimer m_elapsedTimer;
    bool m_cancelled;
    bool m_autoClose;
};

#endif // PROGRESS_DIALOG_H
