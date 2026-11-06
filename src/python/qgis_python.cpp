// qgis_python.cpp — Python embedding for SICNU GEO RS
// IMPORTANT: Python.h must be included BEFORE any Qt headers
// because Qt defines 'slots' as a macro which conflicts with Python's PyType_Spec.slots
#include <Python.h>

#include "qgis_python.h"
#include "sicnu_python_api.h"
#include "sicnu_python_runner.h"
#include "qgspythonrunner.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextCodec>

// Custom Python stdout/stderr redirection
static QgisPython *s_pythonInstance = nullptr;

static PyObject *pythonPrint(PyObject *self, PyObject *args)
{
    Q_UNUSED(self);
    const char *message;
    if (!PyArg_ParseTuple(args, "s", &message))
        return nullptr;

    if (s_pythonInstance) {
        QString text = QString::fromUtf8(message);
        emit s_pythonInstance->outputReady(text);
    }
    Py_RETURN_NONE;
}

// #1647-1: the platform's SicnuPythonApi was dead code — never registered
// into the interpreter, while the pure-Python helper ADVERTISED
// sicnu.project_path() (which did not exist). Register the real C++ API as
// built-in functions so the advertised surface is backed by the platform.
static PyObject *sicnuProjectPath(PyObject *self, PyObject *args)
{
    Q_UNUSED(self);
    Q_UNUSED(args);
    const QString path = SicnuPythonApi::instance().projectPath();
    return PyUnicode_FromString(path.toUtf8().constData());
}

static PyMethodDef PrintMethods[] = {
    {"sicnu_print", pythonPrint, METH_VARARGS, "Print to SICNU console"},
    {"sicnu_project_path", (PyCFunction)sicnuProjectPath, METH_NOARGS,
     "Current SICNU project file path (empty when no project is open)"},
    {nullptr, nullptr, 0, nullptr}
};

QgisPython &QgisPython::instance()
{
    static QgisPython s_instance;
    return s_instance;
}

QgisPython::QgisPython(QObject *parent)
    : QObject(parent)
{
    s_pythonInstance = this;
}

QgisPython::~QgisPython()
{
    finalize();
    s_pythonInstance = nullptr;
}

bool QgisPython::initialize()
{
    QMutexLocker locker(&m_mutex);
    if (m_initialized)
        return true;

    // Initialize Python interpreter
    Py_Initialize();

    // Get main module and dictionary
    m_mainModule = PyImport_AddModule("__main__");
    if (!m_mainModule) {
        qWarning() << "Failed to get __main__ module";
        return false;
    }

    m_mainDict = PyModule_GetDict(static_cast<PyObject *>(m_mainModule));
    if (!m_mainDict) {
        qWarning() << "Failed to get __main__ dictionary";
        return false;
    }

    // Add custom print function
    PyModule_AddFunctions(static_cast<PyObject *>(m_mainModule), PrintMethods);

    // Mark as initialized so runString and addPath work during initialize()
    m_initialized = true;

    // Redirect stdout/stderr to our custom print
    QString redirectCode = R"(
import sys
class SICNUStdout:
    def write(self, text):
        if text.strip():
            from __main__ import sicnu_print
            sicnu_print(text)
    def flush(self):
        pass
sys.stdout = SICNUStdout()
sys.stderr = SICNUStdout()
)";

    QString error;
    runString(redirectCode, error);

    // Configure Python path to include system site-packages
    // This allows importing numpy, scipy, sklearn, gdal, etc.
    QString setupPaths = R"(
import sys
import os

# Add common site-packages paths
paths_to_add = []

# Python installation site-packages
for p in sys.path:
    if 'site-packages' in p:
        paths_to_add.append(p)

# Miniconda/Anaconda paths
conda_base = os.environ.get('CONDA_PREFIX', '')
if conda_base:
    site_pkg = os.path.join(conda_base, 'lib', 'python' + '.'.join(map(str, sys.version_info[:2])), 'site-packages')
    if os.path.isdir(site_pkg):
        paths_to_add.append(site_pkg)

# System paths
for base in ['/usr/lib/python3', '/usr/local/lib/python3']:
    for ver in ['14', '13', '12', '11', '10']:
        sp = base + '.' + ver + '/site-packages'
        if os.path.isdir(sp) and sp not in paths_to_add:
            paths_to_add.append(sp)

# Add all found paths
for p in paths_to_add:
    if p not in sys.path:
        sys.path.insert(0, p)
)";

    runString(setupPaths, error);

    // Add application paths to sys.path
    QString appDir = QCoreApplication::applicationDirPath();
    addPath(appDir);
    addPath(appDir + "/../python");
    addPath(appDir + "/../lib/python3");

    // Create SICNU helper module with platform access
    QString createHelper = R"(
import sys

class _SicnuHelper:
    """SICNU GEO RS Python helper utilities with full platform access."""

    @staticmethod
    def packages():
        """List available scientific packages and their versions."""
        import importlib
        pkgs = {
            'numpy': 'Numerical computing',
            'scipy': 'Scientific computing',
            'sklearn': 'Machine learning',
            'skimage': 'Image processing',
            'gdal': 'Geospatial data (via osgeo)',
            'matplotlib': 'Plotting',
            'pandas': 'Data analysis',
            'geopandas': 'Geospatial dataframes',
            'shapely': 'Computational geometry',
            'fiona': 'Geospatial file I/O',
            'rasterio': 'Raster I/O',
            'pyproj': 'Coordinate transformations'
        }
        print("Available packages:")
        print("-" * 50)
        for name, desc in pkgs.items():
            try:
                mod = importlib.import_module(name)
                ver = getattr(mod, '__version__', '?')
                print(f"  {name:12s} {ver:12s} {desc}")
            except ImportError:
                print(f"  {name:12s} {'N/A':12s} {desc}")

    @staticmethod
    def ndvi(nir, red):
        """Compute NDVI from NIR and red bands."""
        import numpy as np
        nir = np.asarray(nir, dtype=np.float32)
        red = np.asarray(red, dtype=np.float32)
        return np.where((nir + red) > 0, (nir - red) / (nir + red), np.nan)

    @staticmethod
    def load_raster(path):
        """Load a raster file as numpy array."""
        from osgeo import gdal
        import numpy as np
        ds = gdal.Open(path)
        if ds is None:
            raise FileNotFoundError(f"Cannot open: {path}")
        bands = []
        for i in range(1, ds.RasterCount + 1):
            bands.append(ds.GetRasterBand(i).ReadAsArray().astype(np.float32))
        ds = None
        return np.stack(bands, axis=0) if len(bands) > 1 else bands[0]

    @staticmethod
    def project_path():
        """Current project file path (real platform API, #1647-1)."""
        return sicnu_project_path()

    @staticmethod
    def project_info():
        """Get current project information."""
        return sicnu_project_path()

sicnu = _SicnuHelper()
sys.modules['sicnu'] = sicnu
print("SICNU helper loaded. Use 'sicnu.packages()' to list packages.")
)";

    runString(createHelper, error);

    // Provide a minimal qgis.utils stub. Vendored QGIS core calls
    // qgis.utils.clean_project_expression_functions() from QgsProject::clear();
    // real QGIS gets that module from its Python plugin environment, which our
    // embedded interpreter lacks — without the stub every project clear prints
    // a spurious NameError (#103).
    QString qgisStub = R"(
import sys, types
qgis = types.ModuleType('qgis')
qgis.utils = types.ModuleType('qgis.utils')
qgis.utils.clean_project_expression_functions = lambda: None
sys.modules['qgis'] = qgis
sys.modules['qgis.utils'] = qgis.utils
)";

    runString(qgisStub, error);

    QgsPythonRunner::setInstance( new SicnuPythonRunner() );
    qDebug() << "Python initialized:" << pythonVersion();
    m_mainThreadState = static_cast<void *>(PyEval_SaveThread());
    return true;
}

bool QgisPython::runString(const QString &command, QString &error)
{
    if (!m_initialized) {
        error = "Python not initialized";
        return false;
    }

    // Serialize interpreter access and hold the GIL for every C-API call:
    // these wrappers may be reached from JobEngine worker threads (#525).
    QMutexLocker locker(&m_mutex);
    if (!m_initialized) {
        error = "Python not initialized";
        return false;
    }
    PyGILState_STATE gilState = PyGILState_Ensure();

    QByteArray cmdBytes = command.toUtf8();
    int result = PyRun_SimpleStringFlags(cmdBytes.constData(), nullptr);

    if (result != 0) {
        if (PyErr_Occurred()) {
            PyErr_Print();
        }
        error = "Python execution error";
    }

    PyGILState_Release(gilState);

    if (result != 0) {
        return false;
    }

    return true;
}

bool QgisPython::evalString(const QString &expression, QString &result, QString &error)
{
    if (!m_initialized) {
        error = "Python not initialized";
        return false;
    }

    QMutexLocker locker(&m_mutex);
    if (!m_initialized) {
        error = "Python not initialized";
        return false;
    }
    PyGILState_STATE gilState = PyGILState_Ensure();

    QByteArray exprBytes = expression.toUtf8();
    PyObject *mainDict = static_cast<PyObject *>(m_mainDict);

    PyObject *pyResult = PyRun_String(exprBytes.constData(), Py_eval_input, mainDict, mainDict);
    if (!pyResult) {
        PyErr_Print();
        PyGILState_Release(gilState);
        error = "Python evaluation error";
        return false;
    }

    // Convert result to string
    PyObject *strResult = PyObject_Str(pyResult);
    if (strResult) {
        const char *str = PyUnicode_AsUTF8(strResult);
        if (str) {
            result = QString::fromUtf8(str);
        }
        Py_DECREF(strResult);
    }
    Py_DECREF(pyResult);

    PyGILState_Release(gilState);
    return true;
}

bool QgisPython::runFile(const QString &filePath, QString &error)
{
    if (!m_initialized) {
        error = "Python not initialized";
        return false;
    }

    if (!QFileInfo::exists(filePath)) {
        error = "File not found: " + filePath;
        return false;
    }

    // R28-T03 (WP10): the old path handed a host-CRT FILE* to
    // PyRun_SimpleFile — undefined behavior whenever the host and the CPython
    // runtime link different CRTs (Windows), and deprecated upstream since
    // CPython 3.13. Read the source here and run it through the same
    // in-memory entry point runString uses; no FILE* ever crosses the
    // interpreter boundary.
    QFile scriptFile(filePath);
    if (!scriptFile.open(QIODevice::ReadOnly)) {
        error = "Cannot open file: " + filePath;
        return false;
    }
    constexpr qint64 kMaxScriptBytes = 16 * 1024 * 1024;
    const QByteArray source = scriptFile.read(kMaxScriptBytes + 1);
    scriptFile.close();
    if (source.size() > kMaxScriptBytes) {
        error = "Script exceeds the 16 MiB bound: " + filePath;
        return false;
    }

    // PEP 263: honor an explicit coding declaration (first two lines only)
    // exactly like the file tokenizer would; undecodable content is a typed
    // refusal, never mojibake executed as code.
    QByteArray utf8Source = source;
    // The file tokenizer skips a UTF-8 BOM before anything else; without
    // this the BOM bytes would decode to U+FEFF and the first line would
    // become a SyntaxError (R1: BOM-bearing scripts must keep working).
    if ( utf8Source.startsWith( "\xEF\xBB\xBF" ) )
        utf8Source.remove( 0, 3 );
    // PEP 263: the declaration lives in a COMMENT on one of the first two
    // lines — anchored so a plain `coding: str = ...` annotation is not
    // mistaken for an encoding declaration (R2).
    const QRegularExpression cookieRE( "^[ \\t\\f]*#.*?coding[:=]\\s*([-\\w.]+)" );
    const QStringList headLines = QString::fromLatin1( utf8Source.left( 4096 ) ).split( '\n' );
    const int cookieLines = qMin( headLines.size(), 2 );
    for ( int line = 0; line < cookieLines; ++line )
    {
        const QRegularExpressionMatch match = cookieRE.match( headLines.at( line ) );
        if ( !match.hasMatch() )
            continue;
        const QString codecName = match.captured( 1 );
        if ( codecName.compare( QStringLiteral( "utf-8" ), Qt::CaseInsensitive ) == 0 ||
             codecName.compare( QStringLiteral( "utf8" ), Qt::CaseInsensitive ) == 0 )
            break;
        QTextCodec *codec = QTextCodec::codecForName( codecName.toLatin1() );
        if ( !codec )
        {
            error = "Unknown script encoding '" + codecName + "': " + filePath;
            return false;
        }
        QTextCodec::ConverterState state( Qt::ConvertInvalidToNull );
        const QString decoded = codec->toUnicode( utf8Source, &state );
        if ( state.invalidChars > 0 )
        {
            error = "Script is not valid " + codecName + ": " + filePath;
            return false;
        }
        utf8Source = decoded.toUtf8();
        break;
    }

    // R1 (security): an embedded NUL would silently truncate whatever
    // PyRun_SimpleString-style entry sees a bare const char* — the file
    // tokenizer rejects NULs with "source code string cannot contain null
    // bytes", and so does this path (typed refusal, never partial execution).
    if ( utf8Source.contains( '\0' ) )
    {
        error = "Script contains an embedded NUL byte: " + filePath;
        return false;
    }

    // Run through the in-memory entry point with the FILE'S NAME carried as
    // the code object filename: tracebacks point at the real file, and the
    // interpreter boundary still never sees a host-CRT FILE*.
    QMutexLocker locker(&m_mutex);
    PyGILState_STATE gilState = PyGILState_Ensure();
    PyObject *codeObject = Py_CompileString( utf8Source.constData(),
                                             filePath.toUtf8().constData(),
                                             Py_file_input );
    if ( !codeObject )
    {
        PyErr_Print();
        PyGILState_Release(gilState);
        error = "Python file compile error";
        return false;
    }
    PyObject *resultObject = PyEval_EvalCode( codeObject, static_cast<PyObject *>(m_mainDict),
                                               static_cast<PyObject *>(m_mainDict) );
    Py_DECREF( codeObject );
    const bool failed = resultObject == nullptr;
    if ( failed )
    {
        PyErr_Print();
        error = "Python file execution error";
    }
    else
    {
        Py_XDECREF( resultObject );
    }
    PyGILState_Release(gilState);

    return !failed;
}

void QgisPython::finalize()
{
    QMutexLocker locker(&m_mutex);
    if (m_initialized) {
        if (m_mainThreadState) {
            PyEval_RestoreThread(static_cast<PyThreadState *>(m_mainThreadState));
            m_mainThreadState = nullptr;
        }
        Py_Finalize();
        m_initialized = false;
        m_mainModule = nullptr;
        m_mainDict = nullptr;
    }
}

QString QgisPython::pythonVersion() const
{
    if (!m_initialized)
        return "Not initialized";

    QString result, error;
    // Expression form only — evalString uses Py_eval_input (no statements).
    const_cast<QgisPython *>(this)->evalString("__import__('sys').version", result, error);
    return result;
}

void QgisPython::addPath(const QString &path)
{
    if (!m_initialized)
        return;

    // Escape for a Python single-quoted string (defense against path injection).
    QString escaped = path;
    escaped.replace(QLatin1String("\\"), QLatin1String("\\\\"));
    escaped.replace(QLatin1Char('\''), QLatin1String("\\'"));
    // Also neutralize newlines that could break out of the string literal.
    escaped.replace(QLatin1Char('\n'), QLatin1String("\\n"));
    escaped.replace(QLatin1Char('\r'), QLatin1String("\\r"));

    QString code = QStringLiteral("import sys; sys.path.insert(0, '%1')").arg(escaped);
    QString error;
    runString(code, error);
}

void QgisPython::setOutputCallback(OutputCallback callback)
{
    m_outputCallback = callback;
}

bool QgisPython::importPackage(const QString &packageName)
{
    if (!m_initialized)
        return false;

    QString code = QString("import %1").arg(packageName);
    QString error;
    return runString(code, error);
}

bool QgisPython::isPackageAvailable(const QString &packageName) const
{
    if (!m_initialized)
        return false;

    // Expression form only — evalString uses Py_eval_input (no statements/imports).
    // fromlist ensures __import__ returns importlib.util, not the top-level importlib package.
    QString code = QStringLiteral(
        "__import__('importlib.util', fromlist=['find_spec']).find_spec('%1') is not None")
        .arg(packageName);
    QString result, error;
    const_cast<QgisPython *>(this)->evalString(code, result, error);
    return result.trimmed() == "True";
}

QStringList QgisPython::availablePackages() const
{
    QStringList packages;
    QStringList candidates = {
        "numpy", "scipy", "sklearn", "skimage",
        "osgeo", "gdal",
        "matplotlib", "pandas", "geopandas",
        "shapely", "fiona", "rasterio", "pyproj"
    };

    for (const QString &pkg : candidates) {
        if (isPackageAvailable(pkg)) {
            packages.append(pkg);
        }
    }
    return packages;
}
