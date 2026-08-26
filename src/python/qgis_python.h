// qgis_python.h — Python embedding for SICNU GEO RS
#pragma once

#include <QString>
#include <QObject>
#include <QMutex>

/**
 * Singleton class that manages the embedded Python interpreter.
 * Provides methods to run Python code and access the interpreter state.
 */
class QgisPython : public QObject
{
    Q_OBJECT

public:
    static QgisPython &instance();

    /**
     * Initialize the Python interpreter.
     * Must be called before any other methods.
     * Returns true if initialization succeeded.
     */
    bool initialize();

    /**
     * Check if Python is initialized.
     */
    bool isInitialized() const { return m_initialized; }

    /**
     * Run a Python string in the interpreter.
     * Returns true if execution succeeded.
     */
    bool runString(const QString &command, QString &error);

    /**
     * Evaluate a Python expression and return the result as a string.
     * Returns true if evaluation succeeded.
     */
    bool evalString(const QString &expression, QString &result, QString &error);

    /**
     * Run a Python script file.
     * Returns true if execution succeeded.
     */
    bool runFile(const QString &filePath, QString &error);

    /**
     * Finalize the Python interpreter.
     * Called automatically on destruction.
     */
    void finalize();

    /**
     * Get the Python version string.
     */
    QString pythonVersion() const;

    /**
     * Add a directory to Python's sys.path.
     */
    void addPath(const QString &path);

    /**
     * Import a Python package and return true if successful.
     */
    bool importPackage(const QString &packageName);

    /**
     * Check if a Python package is available.
     */
    bool isPackageAvailable(const QString &packageName) const;

    /**
     * Get a list of available scientific packages.
     */
    QStringList availablePackages() const;

    /**
     * Install a print callback to capture Python output.
     */
    using OutputCallback = std::function<void(const QString &)>;
    void setOutputCallback(OutputCallback callback);

signals:
    void outputReady(const QString &text);

private:
    QgisPython(QObject *parent = nullptr);
    ~QgisPython() override;

    QgisPython(const QgisPython &) = delete;
    QgisPython &operator=(const QgisPython &) = delete;

    bool m_initialized = false;
    mutable QRecursiveMutex m_mutex;
    OutputCallback m_outputCallback;

    // Python state
    void *m_mainModule = nullptr;    // PyObject*
    void *m_mainDict = nullptr;      // PyObject*
    void *m_mainThreadState = nullptr; // PyThreadState*
};
