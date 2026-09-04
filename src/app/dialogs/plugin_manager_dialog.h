// src/app/dialogs/plugin_manager_dialog.h — Plugin Manager (Phase W)
#ifndef PLUGINMANAGERDIALOG_H
#define PLUGINMANAGERDIALOG_H

#include <QDialog>
#include <QTableWidget>

class QLabel;
class QTextEdit;

/**
 * @brief Local Plugin Manager: installed / enabled / disabled / incompatible
 * / broken views over PluginRegistry records, with diagnostics and
 * enable-disable actions. Manifest-index based: opening this dialog never
 * loads plugin binaries.
 */
class PluginManagerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PluginManagerDialog( QWidget *parent = nullptr );

private slots:
    void refresh();
    void showDiagnostics();

private:
    void applyEnabled( bool enable );

private:
    void populate();

    QTableWidget *mTable = nullptr;
    QLabel *mSummary = nullptr;
    QPushButton *mEnableButton = nullptr;
    QPushButton *mDisableButton = nullptr;
    QTextEdit *mDiagnostics = nullptr;
};

#endif
