#ifndef PREFERENCES_DIALOG_H
#define PREFERENCES_DIALOG_H

#include <QDialog>
#include <QMap>
#include <QString>

#include <vector>

class QTabWidget;
class QComboBox;
class QLineEdit;
class QLabel;
class QCheckBox;
class QWidget;

class PreferencesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PreferencesDialog(QWidget *parent = nullptr);

    /// ExpRS Developer Platform 3.0: pages contributed by UI plugins. Pages
    /// are registered while plugins load (before the dialog is opened) and
    /// appended as tabs; the dialog takes ownership for its lifetime.
    static void registerExternalPage( const QString &title, QWidget *page );

    QString theme() const;
    void setTheme(const QString &theme);

    QString defaultCrs() const;
    void setDefaultCrs(const QString &crs);

    QString gdalPath() const;
    void setGdalPath(const QString &path);

    QString otbPath() const;
    void setOtbPath(const QString &path);

    bool logToFile() const;
    void setLogToFile(bool enabled);

    QString logFilePath() const;
    void setLogFilePath(const QString &path);

    void loadSettings();
    void saveSettings();

private slots:
    void onAccept();
    void onBrowseGdalPath();
    void onBrowseOtbPath();
    void onBrowseLogPath();
    void onLogToFileToggled(bool checked);

private:
    void setupGeneralTab();
    void setupToolsTab();
    void setupAboutTab();

    QTabWidget *m_tabWidget;

    // General tab
    QComboBox *m_themeCombo;
    QComboBox *m_crsCombo;

    // Tools tab
    QLineEdit *m_gdalPathEdit;
    QLineEdit *m_otbPathEdit;

    // Logging
    QCheckBox *m_logToFileCheck;
    QLineEdit *m_logFilePathEdit;
};

#endif // PREFERENCES_DIALOG_H
