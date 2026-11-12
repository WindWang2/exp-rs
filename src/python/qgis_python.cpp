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
#include <QVariant>

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

// #1647: SicnuPythonApi's Q_INVOKABLE methods are the platform Python API.
// project_path was the first one wired; the rest forward to the same
// singleton. Null ActiveViewHost follows the C++ methods (empty map / 0 /
// no-op / empty string) — it is not turned into a new error.

static PyObject *qstringToPy(const QString &text)
{
    const QByteArray utf8 = text.toUtf8();
    return PyUnicode_FromString(utf8.constData());
}

static PyObject *boolToPy(bool value)
{
    if (value)
        Py_RETURN_TRUE;
    Py_RETURN_FALSE;
}

static PyObject *stringListToPy(const QStringList &names)
{
    PyObject *list = PyList_New(names.size());
    if (!list)
        return nullptr;
    for (int i = 0; i < names.size(); ++i) {
        PyObject *item = qstringToPy(names.at(i));
        if (!item) {
            Py_DECREF(list);
            return nullptr;
        }
        PyList_SET_ITEM(list, i, item);
    }
    return list;
}

static PyObject *doubleListToPy(const QList<double> &values)
{
    PyObject *list = PyList_New(values.size());
    if (!list)
        return nullptr;
    for (int i = 0; i < values.size(); ++i) {
        PyObject *item = PyFloat_FromDouble(values.at(i));
        if (!item) {
            Py_DECREF(list);
            return nullptr;
        }
        PyList_SET_ITEM(list, i, item);
    }
    return list;
}

static PyObject *variantToPy(const QVariant &value)
{
    switch (value.typeId()) {
    case QMetaType::QString:
        return qstringToPy(value.toString());
    case QMetaType::Bool:
        return boolToPy(value.toBool());
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
        return PyLong_FromLongLong(value.toLongLong());
    case QMetaType::Double:
    case QMetaType::Float:
        return PyFloat_FromDouble(value.toDouble());
    case QMetaType::QStringList:
        return stringListToPy(value.toStringList());
    case QMetaType::QVariantList: {
        const QVariantList items = value.toList();
        PyObject *list = PyList_New(items.size());
        if (!list)
            return nullptr;
        for (int i = 0; i < items.size(); ++i) {
            PyObject *item = variantToPy(items.at(i));
            if (!item) {
                Py_DECREF(list);
                return nullptr;
            }
            PyList_SET_ITEM(list, i, item);
        }
        return list;
    }
    default:
        break;
    }
    if (!value.isValid())
        Py_RETURN_NONE;
    return qstringToPy(value.toString());
}

static PyObject *variantMapToPy(const QVariantMap &map)
{
    PyObject *dict = PyDict_New();
    if (!dict)
        return nullptr;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
        PyObject *value = variantToPy(it.value());
        if (!value) {
            Py_DECREF(dict);
            return nullptr;
        }
        const QByteArray key = it.key().toUtf8();
        const int rc = PyDict_SetItemString(dict, key.constData(), value);
        Py_DECREF(value);
        if (rc < 0) {
            Py_DECREF(dict);
            return nullptr;
        }
    }
    return dict;
}

// One level of list/tuple. Nested sequences are refused — the C++ map is a
// flat parameter bag (strings, numbers, bools, and lists of those).
static bool pyObjectToVariant(PyObject *value, QVariant &out)
{
    if (value == Py_None) {
        out = QVariant();
        return true;
    }
    if (PyBool_Check(value)) {
        out = value == Py_True;
        return true;
    }
    if (PyLong_Check(value)) {
        const long long number = PyLong_AsLongLong(value);
        if (PyErr_Occurred())
            return false;
        out = static_cast<qlonglong>(number);
        return true;
    }
    if (PyFloat_Check(value)) {
        const double number = PyFloat_AsDouble(value);
        if (PyErr_Occurred())
            return false;
        out = number;
        return true;
    }
    if (PyUnicode_Check(value)) {
        const char *text = PyUnicode_AsUTF8(value);
        if (!text)
            return false;
        out = QString::fromUtf8(text);
        return true;
    }
    if (PyList_Check(value) || PyTuple_Check(value)) {
        const Py_ssize_t count = PySequence_Size(value);
        QVariantList list;
        for (Py_ssize_t i = 0; i < count; ++i) {
            PyObject *item = PySequence_GetItem(value, i);
            if (!item)
                return false;
            QVariant converted;
            const bool ok = pyObjectToVariant(item, converted);
            Py_DECREF(item);
            if (!ok)
                return false;
            if (converted.typeId() == QMetaType::QVariantList) {
                PyErr_SetString(PyExc_TypeError, "nested sequences are not supported");
                return false;
            }
            list.append(converted);
        }
        out = list;
        return true;
    }
    PyErr_SetString(PyExc_TypeError,
                    "algorithm parameters must be str, int, float, bool, None, or a list of those");
    return false;
}

static PyObject *sicnuProjectPath(PyObject *self, PyObject *args)
{
    Q_UNUSED(self);
    Q_UNUSED(args);
    return qstringToPy(SicnuPythonApi::instance().projectPath());
}

static PyObject *sicnuSaveProject(PyObject *self, PyObject *args)
{
    Q_UNUSED(self);
    Q_UNUSED(args);
    return boolToPy(SicnuPythonApi::instance().saveProject());
}

static PyObject *sicnuOpenProject(PyObject *self, PyObject *args)
{
    Q_UNUSED(self);
    const char *path = nullptr;
    if (!PyArg_ParseTuple(args, "s", &path))
        return nullptr;
    return boolToPy(SicnuPythonApi::instance().openProject(QString::fromUtf8(path)));
}

static PyObject *sicnuLayerNames(PyObject *self, PyObject *args)
{
    Q_UNUSED(self);
    Q_UNUSED(args);
    return stringListToPy(SicnuPythonApi::instance().layerNames());
}

static PyObject *sicnuLayerCount(PyObject *self, PyObject *args)
{
    Q_UNUSED(self);
    Q_UNUSED(args);
    return PyLong_FromLong(SicnuPythonApi::instance().layerCount());
}

static PyObject *sicnuAddRasterLayer(PyObject *self, PyObject *args)
{
    Q_UNUSED(self);
    const char *path = nullptr;
    const char *name = nullptr;
    if (!PyArg_ParseTuple(args, "s|s", &path, &name))
        return nullptr;
    const QString added = SicnuPythonApi::instance().addRasterLayer(
        QString::fromUtf8(path), name ? QString::fromUtf8(name) : QString());
    return qstringToPy(added);
}

static PyObject *sicnuAddVectorLayer(PyObject *self, PyObject *args)
{
    Q_UNUSED(self);
    const char *path = nullptr;
    const char *name = nullptr;
    if (!PyArg_ParseTuple(args, "s|s", &path, &name))
        return nullptr;
    const QString added = SicnuPythonApi::instance().addVectorLayer(
        QString::fromUtf8(path), name ? QString::fromUtf8(name) : QString());
    return qstringToPy(added);
}

static PyObject *sicnuRemoveLayer(PyObject *self, PyObject *args)
{
    Q_UNUSED(self);
    const char *name = nullptr;
    if (!PyArg_ParseTuple(args, "s", &name))
        return nullptr;
    return boolToPy(SicnuPythonApi::instance().removeLayer(QString::fromUtf8(name)));
}

static PyObject *sicnuRasterInfo(PyObject *self, PyObject *args)
{
    Q_UNUSED(self);
    const char *name = nullptr;
    if (!PyArg_ParseTuple(args, "s", &name))
        return nullptr;
    return variantMapToPy(SicnuPythonApi::instance().rasterInfo(QString::fromUtf8(name)));
}

static PyObject *sicnuPixelValue(PyObject *self, PyObject *args)
{
    Q_UNUSED(self);
    const char *name = nullptr;
    double x = 0.0;
    double y = 0.0;
    if (!PyArg_ParseTuple(args, "sdd", &name, &x, &y))
        return nullptr;
    return doubleListToPy(SicnuPythonApi::instance().pixelValue(QString::fromUtf8(name), x, y));
}

static PyObject *sicnuBandStatistics(PyObject *self, PyObject *args)
{
    Q_UNUSED(self);
    const char *name = nullptr;
    int band = 0;
    if (!PyArg_ParseTuple(args, "si", &name, &band))
        return nullptr;
    return variantMapToPy(SicnuPythonApi::instance().bandStatistics(QString::fromUtf8(name), band));
}

static PyObject *sicnuVectorInfo(PyObject *self, PyObject *args)
{
    Q_UNUSED(self);
    const char *name = nullptr;
    if (!PyArg_ParseTuple(args, "s", &name))
        return nullptr;
    return variantMapToPy(SicnuPythonApi::instance().vectorInfo(QString::fromUtf8(name)));
}

static PyObject *sicnuFeatureCount(PyObject *self, PyObject *args)
{
    Q_UNUSED(self);
    const char *name = nullptr;
    if (!PyArg_ParseTuple(args, "s", &name))
        return nullptr;
    return PyLong_FromLong(SicnuPythonApi::instance().featureCount(QString::fromUtf8(name)));
}

static PyObject *sicnuRunAlgorithm(PyObject *self, PyObject *args)
{
    Q_UNUSED(self);
    const char *algorithmId = nullptr;
    PyObject *paramsObj = nullptr;
    if (!PyArg_ParseTuple(args, "sO!", &algorithmId, &PyDict_Type, &paramsObj))
        return nullptr;

    QVariantMap parameters;
    PyObject *key = nullptr;
    PyObject *value = nullptr;
    Py_ssize_t pos = 0;
    while (PyDict_Next(paramsObj, &pos, &key, &value)) {
        if (!PyUnicode_Check(key)) {
            PyErr_SetString(PyExc_TypeError, "algorithm parameter names must be strings");
            return nullptr;
        }
        const char *keyUtf8 = PyUnicode_AsUTF8(key);
        if (!keyUtf8)
            return nullptr;
        QVariant converted;
        if (!pyObjectToVariant(value, converted))
            return nullptr;
        parameters.insert(QString::fromUtf8(keyUtf8), converted);
    }
    return boolToPy(SicnuPythonApi::instance().runAlgorithm(QString::fromUtf8(algorithmId), parameters));
}

static PyObject *sicnuCanvasExtent(PyObject *self, PyObject *args)
{
    Q_UNUSED(self);
    Q_UNUSED(args);
    return variantMapToPy(SicnuPythonApi::instance().canvasExtent());
}

static PyObject *sicnuSetCanvasExtent(PyObject *self, PyObject *args)
{
    Q_UNUSED(self);
    double xmin = 0.0;
    double ymin = 0.0;
    double xmax = 0.0;
    double ymax = 0.0;
    if (!PyArg_ParseTuple(args, "dddd", &xmin, &ymin, &xmax, &ymax))
        return nullptr;
    SicnuPythonApi::instance().setCanvasExtent(xmin, ymin, xmax, ymax);
    Py_RETURN_NONE;
}

static PyObject *sicnuRefreshCanvas(PyObject *self, PyObject *args)
{
    Q_UNUSED(self);
    Q_UNUSED(args);
    SicnuPythonApi::instance().refreshCanvas();
    Py_RETURN_NONE;
}

static PyObject *sicnuCanvasScale(PyObject *self, PyObject *args)
{
    Q_UNUSED(self);
    Q_UNUSED(args);
    return PyFloat_FromDouble(SicnuPythonApi::instance().canvasScale());
}

static PyObject *sicnuSetCanvasScale(PyObject *self, PyObject *args)
{
    Q_UNUSED(self);
    double scale = 0.0;
    if (!PyArg_ParseTuple(args, "d", &scale))
        return nullptr;
    SicnuPythonApi::instance().setCanvasScale(scale);
    Py_RETURN_NONE;
}

static PyObject *sicnuProjectCrs(PyObject *self, PyObject *args)
{
    Q_UNUSED(self);
    Q_UNUSED(args);
    return qstringToPy(SicnuPythonApi::instance().projectCrs());
}

static PyObject *sicnuSetProjectCrs(PyObject *self, PyObject *args)
{
    Q_UNUSED(self);
    const char *crs = nullptr;
    if (!PyArg_ParseTuple(args, "s", &crs))
        return nullptr;
    return boolToPy(SicnuPythonApi::instance().setProjectCrs(QString::fromUtf8(crs)));
}

static PyObject *sicnuApplicationPath(PyObject *self, PyObject *args)
{
    Q_UNUSED(self);
    Q_UNUSED(args);
    return qstringToPy(SicnuPythonApi::instance().applicationPath());
}

static PyObject *sicnuVersion(PyObject *self, PyObject *args)
{
    Q_UNUSED(self);
    Q_UNUSED(args);
    return qstringToPy(SicnuPythonApi::instance().version());
}

static PyMethodDef PrintMethods[] = {
    {"sicnu_print", pythonPrint, METH_VARARGS, "Print to SICNU console"},
    {"sicnu_project_path", (PyCFunction)sicnuProjectPath, METH_NOARGS,
     "Current SICNU project file path (empty when no project is open)"},
    {"sicnu_save_project", (PyCFunction)sicnuSaveProject, METH_NOARGS,
     "Save the current project via the host preflight. False when no save delegate is installed"},
    {"sicnu_open_project", (PyCFunction)sicnuOpenProject, METH_VARARGS,
     "Open a project file. Argument: path (str)"},
    {"sicnu_layer_names", (PyCFunction)sicnuLayerNames, METH_NOARGS,
     "Names of layers in the current project"},
    {"sicnu_layer_count", (PyCFunction)sicnuLayerCount, METH_NOARGS,
     "Number of layers in the current project"},
    {"sicnu_add_raster_layer", (PyCFunction)sicnuAddRasterLayer, METH_VARARGS,
     "Add a raster through ActiveViewHost. Arguments: path (str), name (str, optional). Empty string on failure"},
    {"sicnu_add_vector_layer", (PyCFunction)sicnuAddVectorLayer, METH_VARARGS,
     "Add a vector through ActiveViewHost. Arguments: path (str), name (str, optional). Empty string on failure"},
    {"sicnu_remove_layer", (PyCFunction)sicnuRemoveLayer, METH_VARARGS,
     "Remove a layer by name. Argument: name (str)"},
    {"sicnu_raster_info", (PyCFunction)sicnuRasterInfo, METH_VARARGS,
     "Raster metadata dict for a layer name, or {} when the layer is absent"},
    {"sicnu_pixel_value", (PyCFunction)sicnuPixelValue, METH_VARARGS,
     "Per-band sample at (x, y). Arguments: name (str), x (float), y (float)"},
    {"sicnu_band_statistics", (PyCFunction)sicnuBandStatistics, METH_VARARGS,
     "Band statistics dict. Arguments: name (str), band (int)"},
    {"sicnu_vector_info", (PyCFunction)sicnuVectorInfo, METH_VARARGS,
     "Vector metadata dict for a layer name, or {} when the layer is absent"},
    {"sicnu_feature_count", (PyCFunction)sicnuFeatureCount, METH_VARARGS,
     "Feature count for a vector layer name, or 0 when it is absent"},
    {"sicnu_run_algorithm", (PyCFunction)sicnuRunAlgorithm, METH_VARARGS,
     "Run a processing algorithm. Arguments: algorithm id (str), parameters (dict)"},
    {"sicnu_canvas_extent", (PyCFunction)sicnuCanvasExtent, METH_NOARGS,
     "Map extent dict, or {} when no view host is installed"},
    {"sicnu_set_canvas_extent", (PyCFunction)sicnuSetCanvasExtent, METH_VARARGS,
     "Set the map extent. Arguments: xmin, ymin, xmax, ymax (float). No-op without a view host"},
    {"sicnu_refresh_canvas", (PyCFunction)sicnuRefreshCanvas, METH_NOARGS,
     "Refresh the map canvas. No-op without a view host"},
    {"sicnu_canvas_scale", (PyCFunction)sicnuCanvasScale, METH_NOARGS,
     "Map scale, or 0.0 when no view host is installed"},
    {"sicnu_set_canvas_scale", (PyCFunction)sicnuSetCanvasScale, METH_VARARGS,
     "Set the map scale. Argument: scale (float). No-op without a view host"},
    {"sicnu_project_crs", (PyCFunction)sicnuProjectCrs, METH_NOARGS,
     "Project CRS auth id"},
    {"sicnu_set_project_crs", (PyCFunction)sicnuSetProjectCrs, METH_VARARGS,
     "Set the project CRS. Argument: auth id (str). False when the CRS is invalid"},
    {"sicnu_application_path", (PyCFunction)sicnuApplicationPath, METH_NOARGS,
     "Application directory"},
    {"sicnu_version", (PyCFunction)sicnuVersion, METH_NOARGS,
     "Application version string"},
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
